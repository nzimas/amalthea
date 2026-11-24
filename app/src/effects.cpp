#include "effects.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
InsertEffect::InsertEffect(bool enableSpace){if(enableSpace)space=std::make_unique<LushReverb>();}
void InsertEffect::configureSpace(){
 if(!space)return;MasterSettings s;
 s.extra[9]={settings.space[0],settings.space[1],settings.space[2]};
 s.reverbSpace={settings.space[3],settings.space[4],settings.space[5],settings.space[6]};
 space->configure(s);
}
static float soft(float x) { return x / (1 + std::abs(x)); }
void InsertEffect::release(){releasing=true;if(effect.type==FxType::Clouds){auto s=settings;s.clouds[7]=0;nornios.configure(effect.type,s);}}
void InsertEffect::clear() {
  level.reset();
  releasing=false;stutterTail=1;tails.fill({});guard={};
  if(portedIndex(effect.type)>=0&&ported[portedIndex(effect.type)])ported[portedIndex(effect.type)]->clear();
  effect={};amount=0;coefficient=.1f;phase=phaseStep=0;
  low={};highInput={};highOutput={};held={};reduxOut={};
  decimate=stutterPos=recorded=0;stutterLength=1024;
  reduxJitter=0;reduxSlew=1;randomState=0x12345678;
  stutter.fill(0);filter.clear();nornios.clear();if(space)space->clear();
}
bool InsertEffect::ringing(FxType t){return ringingEffect(t);}
// Effects on the same processor object cannot ring while the other runs.
int InsertEffect::processorOf(FxType t){
  if(portedIndex(t)>=0)return 100+portedIndex(t);
  if(t==FxType::Space)return 1;
  if(t>=FxType::Clouds&&t<=FxType::Rotor)return 2;
  if(t==FxType::Filter)return 3;
  return t==FxType::None?0:4;
}
void InsertEffect::Guard::remember(float inputPeak,int frames,FxType type){
  float seconds=ringingEffect(type)?RingingTailSeconds:ShortTailSeconds;
  memory=std::max(inputPeak,memory*std::pow(.001f,frames/(seconds*44100.f)));   // -60 dB over the tail time
}
void InsertEffect::Guard::apply(float *data,int n){
  float peak=0;for(int i=0;i<2*n;i++)peak=std::max(peak,std::abs(data[i]));
  float limit=2*memory,target=peak>limit?limit/std::max(peak,1e-20f):1;
  if(target>=1&&gain>=1)return;
  // Ramp across the block: down at once to the target, back up gently.
  float end=target<gain?target:std::min(target,gain+.05f);
  for(int i=0;i<n;i++){float g=gain+(end-gain)*(i+1)/n;data[2*i]*=g;data[2*i+1]*=g;}
  gain=end;
}
int InsertEffect::tailCount()const{int n=0;for(auto&t:tails)n+=t.type!=FxType::None;return n;}
void InsertEffect::startTail(Effect e){
  if(!ringing(e.type)||e.value==0)return;
  for(auto &t:tails)if(t.type==e.type){t={e.type,e.value/100.f};t.guard=guard;return;}
  Tail *slot=nullptr;for(auto &t:tails)if(t.type==FxType::None){slot=&t;break;}
  if(!slot){slot=&tails[0];for(auto &t:tails)if(t.age>slot->age)slot=&t;}   // overflow: the oldest stops
  *slot={e.type,e.value/100.f};slot->guard=guard;
  // Keep at most two ringing at full level: the older one fades quickly.
  int live=0;Tail *oldest=nullptr;
  for(auto &t:tails)if(t.type!=FxType::None&&!t.fading){live++;if(&t!=slot&&(!oldest||t.age>oldest->age))oldest=&t;}
  if(live>2&&oldest)oldest->fading=true;
}
void InsertEffect::renderTails(float *data,int n){
  float buffer[2*256];
  for(auto &t:tails){
    if(t.type==FxType::None)continue;
    for(int at=0;at<n;at+=256){
      int k=std::min(256,n-at);std::fill(buffer,buffer+2*k,0.f);
      if(portedIndex(t.type)>=0){auto*p=ported[portedIndex(t.type)];if(p)p->process(buffer,k,t.mix);}
      else if(t.type==FxType::Space&&space)for(int i=0;i<k;i++){float l,r;space->process(0,0,l,r);buffer[2*i]=l*t.mix;buffer[2*i+1]=r*t.mix;}
      else if(t.type==FxType::Clouds)nornios.process(t.type,t.mix,buffer,k);
      t.guard.remember(0,k,t.type);t.guard.apply(buffer,k);
      float peak=0;
      for(int i=0;i<k;i++){
        if(t.fading)t.gain=std::max(0.f,t.gain-1.f/2048);
        for(int c=0;c<2;c++){float v=buffer[2*i+c];if(!std::isfinite(v))v=0;v*=t.gain;peak=std::max(peak,std::abs(v));data[2*(at+i)+c]+=v;}
      }
      t.quiet=peak<1e-4f?t.quiet+k:0;t.age+=k;
      if(t.age>44100*30)t.fading=true;   // a self-oscillating feedback never holds a slot forever
      if(t.quiet>=22050||(t.fading&&t.gain<=0)){t={};break;}
    }
  }
}
void InsertEffect::configure(Effect e, int bpm) {
  // A held effect re-sent with the same type and mix keeps its state untouched.
  if(e.type!=effect.type){
    // Whatever shares the new effect's processor stops ringing (the new one continues it);
    // the effect that ends rings out on its own processor.
    bool continuing=false;
    for(auto &t:tails)if(t.type!=FxType::None&&processorOf(t.type)==processorOf(e.type)){continuing|=t.type==e.type;t={};}
    if(effect.type!=FxType::None&&processorOf(effect.type)!=processorOf(e.type))startTail(effect);
    if(continuing){effect=e;portedTempo=bpm;amount=e.value/100.f;
      if(portedIndex(e.type)>=0&&ported[portedIndex(e.type)])ported[portedIndex(e.type)]->configure(settings,bpm);
      if(e.type==FxType::Space)configureSpace();else if(e.type==FxType::Clouds)nornios.configure(e.type,settings);
      return;}
  }
  if (e.type != effect.type) {
    if(space&&e.type==FxType::Space)space->clear();
    low = {};
    highInput = {};
    highOutput = {};
    held = {};
    reduxOut={};
    decimate = 0;
    stutterPos = recorded = 0;
    phase = 0;
  }
  if (e.type == FxType::Stutter){stutterPos = recorded = 0;releasing=false;stutterTail=1;}
  if(e.type!=effect.type&&e.type==FxType::Filter){filter.configure(settings.filter);filter.clear();}
  if(e.type!=effect.type&&portedIndex(e.type)>=0&&ported[portedIndex(e.type)])ported[portedIndex(e.type)]->clear();
  if(e.type!=effect.type)level.reset();   // a new effect matches its own level from scratch
  effect = e;portedTempo=bpm;
  if(portedIndex(e.type)>=0&&ported[portedIndex(e.type)])ported[portedIndex(e.type)]->configure(settings,bpm);
  if(e.type==FxType::Filter)filter.configure(settings.filter);
  if(e.type==FxType::Space)configureSpace();
  else if(e.type>=FxType::Clouds&&e.type<=FxType::Rotor)nornios.configure(e.type,settings);
  amount = e.value / 100.f;
  float hz = e.type == FxType::Rat ? 12000 / (1 + amount * 20)
                                   : e.type == FxType::Lowpass
                                         ? 18000 * std::pow(1.f / 90, amount)
                                         : 30 * std::pow(200.f, amount);
  coefficient = 1 - std::exp(-6.2831853f * hz / 44100);
  // Built-in inserts have a fixed character; the step value is their dry/wet mix.
  phaseStep = float(bpm) / 60 / 44100 * 4;   // tremolo: sixteenth notes
  stutterLength = std::clamp(int(44100. * 60 / std::max(30, bpm) / 4), 64, 8192);   // a sixteenth-note slice
}
void InsertEffect::setSettings(const InsertSettings &s){settings=s;if(portedIndex(effect.type)>=0&&ported[portedIndex(effect.type)])ported[portedIndex(effect.type)]->configure(settings,portedTempo);if(effect.type==FxType::Filter)filter.configure(settings.filter);if(effect.type==FxType::Space)configureSpace();else if(effect.type>=FxType::Clouds&&effect.type<=FxType::Rotor)nornios.configure(effect.type,settings);}
void LevelMatch::apply(const float *dry,float *data,int n){
  if(n<=0)return;
  float ein=0,eout=0;for(int i=0;i<2*n;i++){ein+=dry[i]*dry[i];eout+=data[i]*data[i];}
  ein/=2*n;eout/=2*n;
  float a=1-std::exp(-n/(.3f*44100)),g=1-std::exp(-n/(.15f*44100));
  in+=(ein-in)*a;out+=(eout-out)*a;
  // Below about -70 dBFS RMS the input is a tail or silence: hold the correction.
  float target=gain;
  if(in>1e-7f)target=out>1e-12f?std::clamp(std::sqrt(in/out),.063f,2.8f):gain;
  float end=gain+(target-gain)*g;
  for(int i=0;i<n;i++){float k=gain+(end-gain)*(i+1)/n;data[2*i]*=k;data[2*i+1]*=k;}
  gain=end;
}
// Inserts whose purpose is a change of level or placement are left as they are.
bool InsertEffect::levelMatched(FxType t){
  return t!=FxType::None&&t!=FxType::Volume&&t!=FxType::Pan&&t!=FxType::Reverb&&t!=FxType::Delay&&t!=FxType::Abrasion&&t!=FxType::Corrosion&&t!=FxType::Modulation;
}
void InsertEffect::processBlock(float *data,int n){
  for(int at=0;at<n;at+=256){
    int k=std::min(256,n-at);float *block=data+2*at,peak=0;
    for(int i=0;i<2*k;i++)peak=std::max(peak,std::abs(block[i]));
    guard.remember(peak,k,effect.type);
    bool engaged=effect.type!=FxType::None&&effect.value>0;
    float dry[512];bool match=engaged&&levelMatched(effect.type);if(match)std::copy(block,block+2*k,dry);
    renderMain(block,k);
    if(match)level.apply(dry,block,k);else level.reset();
    if(engaged)guard.apply(block,k);else guard.gain=1;   // a dry slot is an exact bypass
  }
  for(const auto &t:tails)if(t.type!=FxType::None){renderTails(data,n);break;}
}
void InsertEffect::renderMain(float *data,int n){
  if(portedIndex(effect.type)>=0){auto*p=ported[portedIndex(effect.type)];if(p)p->process(data,n,amount);return;}
  if(effect.type==FxType::Space){
    if(!space)return;
    for(int i=0;i<n;i++){float l,r;space->process(data[2*i],data[2*i+1],l,r);data[2*i]+=(l-data[2*i])*amount;data[2*i+1]+=(r-data[2*i+1])*amount;}
    return;
  }
  if(effect.type>=FxType::Clouds&&effect.type<=FxType::Rotor){nornios.process(effect.type,amount,data,n);for(int i=0;i<n*2;i++)data[i]=std::isfinite(data[i])?std::clamp(data[i],-4.f,4.f):0;return;}
  for(int i=0;i<n;i++)process(data[2*i],data[2*i+1]);
}
void InsertEffect::process(float &l, float &r) {
  if(portedIndex(effect.type)>=0){float data[]={l,r};renderMain(data,1);l=data[0];r=data[1];return;}
  if(effect.type==FxType::Filter){filter.process(l,r,amount);return;}
  if(effect.type>=FxType::Clouds&&effect.type<=FxType::Space){float data[]={l,r};renderMain(data,1);l=data[0];r=data[1];return;}

  if (effect.type>=FxType::Abrasion || effect.type == FxType::None || effect.type == FxType::Delay ||
      effect.type == FxType::Reverb)
    return;
  if (effect.type == FxType::Pan) {
    float p = (int(effect.value) - 50) / 50.f;
    l *= std::sqrt(1 - p);
    r *= std::sqrt(1 + p);
    return;
  }
  if (effect.type == FxType::Volume) {
    l *= amount;
    r *= amount;
    return;
  }
  if (amount <= 0)
    return;
  float x[2] = {l, r};
  if (effect.type == FxType::Redux) {
    if (decimate-- <= 0) {
      held = {l, r};
      float jitter=(int(randomNext(randomState)%2001)-1000)/1000.f;
      decimate = std::max(1,int((1+amount*62)*(1+reduxJitter*jitter*.9f)));
    }
    reduxOut[0]+=reduxSlew*(held[0]-reduxOut[0]);
    reduxOut[1]+=reduxSlew*(held[1]-reduxOut[1]);
    l = reduxOut[0];
    r = reduxOut[1];
    return;
  }
  if (effect.type == FxType::Tremolo) {
    float v = 1 - amount * (.5f - .5f * std::cos(phase * 6.2831853f));
    phase += phaseStep;
    if (phase >= 1)
      phase -= 1;
    l *= v;
    r *= v;
    return;
  }
  if (effect.type == FxType::Stutter) {
    if (recorded < stutterLength) {
      stutter[recorded * 2] = l;
      stutter[recorded * 2 + 1] = r;
      recorded++;
    } else {
      float sl = stutter[stutterPos * 2], sr = stutter[stutterPos * 2 + 1];
      stutterPos = (stutterPos + 1) % stutterLength;
      if(releasing){stutterTail*=.999f;sl*=stutterTail;sr*=stutterTail;}
      l += (sl - l) * amount;
      r += (sr - r) * amount;
    }
    return;
  }
  const float drive = .5f;   // fixed character; the step value mixes it
  for (int c = 0; c < 2; c++) {
    float dry = x[c], v = dry;
    switch (effect.type) {
    case FxType::Distortion:
      v = std::clamp(dry * (1 + drive * 60), -.8f, .8f);
      break;
    case FxType::Saturation:
      v = soft(dry * (1 + drive * 18));
      break;
    case FxType::Overdrive: {
      float a = soft(dry * (1 + drive * 28));
      v = soft((a + .08f) * (1 + drive * 5)) - soft(.08f * (1 + drive * 5));
      break;
    }
    case FxType::Bitcrush: {
      int bits = 16 - int(drive * 13);
      float levels = float(1 << bits);
      v = std::round(dry * levels) / levels;
      break;
    }
    case FxType::Wavefold: {
      float u = dry * (1 + drive * 40) + 1;
      float z = u - 4 * std::floor(u / 4);
      v = 1 - std::abs(z - 2);
      break;
    }
    case FxType::Fuzz: {
      float u = dry * (1 + drive * 100);
      v = std::abs(u) < .015f
              ? 0
              : std::copysign(1 - std::exp(-std::abs(u) * 3), u);
      break;
    }
    case FxType::Rectify:
      v = std::abs(dry) * (1 + drive * 8);
      break;
    case FxType::Diode: {
      float u = dry * (1 + drive * 70);
      v = u > 0 ? soft(u * .8f) : soft(u * 1.8f) * .55f;
      break;
    }
    case FxType::Rat: {
      float u = std::clamp(dry * (1 + drive * 400), -4.2f, 4.2f);
      low[c] += coefficient * (u - low[c]);
      v = soft(low[c] * 2);
      break;
    }
    case FxType::Lowpass:
      low[c] += coefficient * (dry - low[c]);
      v = low[c];
      break;
    case FxType::Highpass:
      low[c] += coefficient * (dry - low[c]);
      v = dry - low[c];
      break;
    default:
      break;
    }
    // Nonlinear asymmetry must not deposit DC on the master bus.
    if (effect.type >= FxType::Distortion && effect.type <= FxType::Rat) {
      float hp = v - highInput[c] + .995f * highOutput[c];
      highInput[c] = v;
      highOutput[c] = hp;
      v = hp;
    }
    x[c] = dry + (std::clamp(v, -2.f, 2.f) - dry) * amount;
  }
  l = x[0];
  r = x[1];
}
EffectBuses::EffectBuses(bool parallel) : echo(88200 * 2) {}
void EffectBuses::setTempo(int bpm) {
  echoLength = std::clamp(int(44100. * 30 / std::max(bpm, 30)), 256, 88200);
}
void EffectBuses::configureMaster(const MasterSettings &s) {
  echoLength=std::clamp(int(44100*.02*std::pow(100.f,s.value(8,1)/100.f)),256,88200);
  echoFeedback=s.value(8,2)*.009f;
  float hz=200*std::pow(90.f,s.value(8,3)/100.f);
  echoTone=1-std::exp(-6.2831853f*hz/44100);
  space.configure(s);
}
void EffectBuses::clear() {
  std::fill(echo.begin(), echo.end(), 0);
  space.clear();
  echoLow={};
  echoPos = 0;
}
void EffectBuses::process(float dl, float dr, float rl, float rr, float &ol,
                          float &oright) {
  delay(dl,dr,ol,oright);
  float a=0,b=0;
  reverb(rl,rr,a,b);
  ol+=a; oright+=b;
}
void EffectBuses::delay(float dl,float dr,float &ol,float &oright) {
  size_t read = (echoPos + 88200 - echoLength) % 88200;
  float el = echo[read * 2], er = echo[read * 2 + 1];
  echoLow[0]+=echoTone*(el-echoLow[0]); echoLow[1]+=echoTone*(er-echoLow[1]);
  echo[echoPos * 2] = std::clamp(dl + echoLow[1] * echoFeedback, -2.f, 2.f);
  echo[echoPos * 2 + 1] = std::clamp(dr + echoLow[0] * echoFeedback, -2.f, 2.f);
  echoPos = (echoPos + 1) % 88200;
  ol = el * .65f;
  oright = er * .65f;
}
void EffectBuses::reverb(float rl,float rr,float &ol,float &oright) {
  space.process(rl,rr,ol,oright);
}

MasterEffects::MasterEffects(bool parallel) : buses(parallel) {}
void MasterEffects::configure(const MasterSettings &s,int bpm) {
  settings=s;
  threshold=.02f+.93f*s.amount[7]/100.f;
  ratio=1+19*s.value(7,1)/100.f;
  auto smoothing=[](float seconds) { return 1-std::exp(-1/(44100*seconds)); };
  attack=smoothing(.0001f*std::pow(1000.f,s.value(7,2)/100.f));
  release=smoothing(.01f*std::pow(200.f,s.value(7,3)/100.f));
  limiterRelease=smoothing(.01f*std::pow(100.f,s.value(10,3)/100.f));
  // OUTPUT LEVEL drives the output stage: 50 is unity (the former 100), 100 is +9 dB into the
  // limiter and the soft knee, 0 is silence.
  float level=s.amount[10]/100.f,balance=s.value(10,1)/100.f,drive=2.8f*level*std::sqrt(level);
  outputGain={drive*std::sqrt(2*(1-balance)),drive*std::sqrt(2*balance)};
  outputCeiling=.1f+.85f*s.value(10,2)/100.f;
  buses.configureMaster(s);
}
void MasterEffects::clear(int bpm){buses.clear();envelope=0;limiterGain=1;configure(settings,bpm);}
void MasterEffects::process(float &l,float &r,float sendDL,float sendDR,float sendRL,float sendRR,float keyL,float keyR) {
  // Track-local macro returns arrive here already in Abrasion/Corrosion/Modulation order.
  // SIDECHAIN: the source track drives the detector and passes unducked; the rest of the mix ducks.
  const bool keyed=settings.sidechain[0];
  float peak=keyed?std::max(std::abs(keyL),std::abs(keyR)):std::max(std::abs(l),std::abs(r));
  envelope+=(peak>envelope?attack:release)*(peak-envelope);
  float gain=envelope>threshold?std::pow(threshold/std::max(envelope,1e-12f),1-1/ratio):1;
  gain=1+(gain-1)*settings.busMix[0]/100.f;
  if(keyed){l=(l-keyL)*gain+keyL;r=(r-keyR)*gain+keyR;}
  else {l*=gain;r*=gain;}
  float dl=0,dr=0,wet=settings.amount[8]/100.f;
  buses.delay(l*wet+sendDL,r*wet+sendDR,dl,dr);l+=dl*settings.busMix[1]/100.f;r+=dr*settings.busMix[1]/100.f;
  float rl=0,rr=0;wet=settings.amount[9]*settings.busMix[2]/10000.f;
  buses.reverb(sendRL,sendRR,rl,rr);l+=rl*wet;r+=rr*wet;
  float dryL=l,dryR=r;   // (the output stage's Mix at 0 bypasses it entirely, drive included)
  l*=outputGain[0];r*=outputGain[1];
  // The limiter holds peaks a little above the ceiling; the soft knee then rounds them
  // into it (transparent below ~55 % of the ceiling): density instead of sharp clipping.
  peak=std::max(std::abs(l),std::abs(r));
  const float hold=outputCeiling*1.35f;
  float target=peak>hold?hold/peak:1;
  limiterGain=target<limiterGain?target:limiterGain+(target-limiterGain)*limiterRelease;
  auto knee=[&](float x){
    const float c=outputCeiling,k=.55f*c;float a=std::abs(x);
    if(a<=k)return x;
    float y=k+(c-k)*std::tanh((a-k)/(c-k));
    return x<0?-y:y;
  };
  float wl=knee(l*limiterGain),wr=knee(r*limiterGain);
  l=dryL+(wl-dryL)*settings.busMix[3]/100.f;r=dryR+(wr-dryR)*settings.busMix[3]/100.f;
  if(!std::isfinite(l)||!std::isfinite(r)) {l=r=0;clear();}
  // The output-stage Mix may bypass its musical limiter, never the safety ceiling.
  l=std::clamp(l,-.95f,.95f);r=std::clamp(r,-.95f,.95f);
}
} // namespace amalthea
