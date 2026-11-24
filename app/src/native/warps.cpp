// SPDX-License-Identifier: GPL-3.0-only
// WARPS: a one-to-one recreation of Mutable Instruments Warps (meta-modulator),
// Copyright 2014 Emilie Gillet (emilie.o.gillet@gmail.com), MIT licence
// (https://github.com/pichenettes/eurorack/tree/master/warps): "Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software ... The above
// copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software." Helpers from stmlib, same author and licence.
// Ported: the modulator (input VCA/saturation with its gate, the ALGORITHM crossfade
// over cross-fading, folding, diode ring modulation, digital ring modulation, XOR,
// comparator, then the 20-band vocoder with formant shift, release and freeze), the
// internal oscillator (sine, triangle, saw for the cross-modulation algorithms; saw,
// pulse, filtered noise for the vocoder, as on the module) with input FM, and the easter
// egg frequency shifter (quadrature oscillator or Hilbert pair, up/down sideband TIMBRE,
// feedback on LEVEL 1, dry/wet on LEVEL 2, ALGORITHM sets the shift or phase).
// Changed: Warps runs at 96 kHz and oversamples the cross-modulation 6x; here the
// cross-modulation runs at 44.1 kHz x4 with Kaiser-windowed FIRs instead of the remez
// tables. The filter bank keeps its 20 third-octave bands, groups decimated 4x and 2x
// (Warps: 12x and 3x) so band rates stay close to the original; its Chamberlin
// coefficients, delays and the Hilbert all-pass poles were regenerated at 44.1 kHz with
// the Warps resource scripts (top band gain corrected for the same flat passband).
// Per-sample smoothing constants are converted to keep their 96 kHz time constants.
// The wavefolder curve is rebuilt from the timbre.raw resource stored at every 8th point.
// Omitted: CV inputs, calibration, normalization probing and the LEDs. Input 1 is the
// carrier and input 2 the modulator: CARRIER OSC uses the internal oscillator with each
// channel as modulator (stereo), RIGHT IN takes the left channel as carrier and the right
// as modulator (mono out, the shifter's two sidebands in stereo), INPUT patches each
// channel into both inputs. OSC FREQ replaces the LEVEL 1 pitch knob (SYNC ties it to a
// tempo division, phase reset on transport Start); LEVEL 1 then only acts as the shifter's
// feedback. To fit the Brick, the vocoder zone runs one vocoder on the mid of both channels
// (each keeps its own crossfade to its raw modulator). Since the oscillator sounds by itself
// in several algorithms, the output is gated off a quarter second after the input stops.
// Processing runs in Warps-sized blocks of 28 frames (0.6 ms latency).
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace amalthea {
namespace {
const char*const carriers[]={"OSC","RIGHT IN","INPUT"};
const char*const shapes[]={"SINE","TRIANGLE","SAW"};
const NativeParam params[]={
 {"ALGORITHM",31,NativeUnit::Percent,0,96},
 {"TIMBRE",50,NativeUnit::Percent,10,90},
 {"CARRIER",0,NativeUnit::Choice,0,100,carriers,3},
 {"OSC SHAPE",0,NativeUnit::Choice,0,100,shapes,3},
 {"OSC FREQ",45,NativeUnit::Range,25,75,nullptr,0,20,4000,true," Hz"},
 {"SYNC",0,NativeUnit::Toggle,0,100},
 {"DIVISION",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(15)},
 {"OSC FM",0,NativeUnit::Percent,0,30},
 {"LEVEL 1",75,NativeUnit::Percent,40,90},
 {"LEVEL 2",75,NativeUnit::Percent,40,90},
 {"FREQ SHIFTER",0,NativeUnit::Toggle,0,100},
};

constexpr float Rate=44100.f;
constexpr int Block=28;           // Warps renders 60 frames at 96 kHz; 28 at 44.1 kHz
constexpr int Over=4,OverTaps=48; // cross-modulation oversampling
constexpr int Bands=20,LowFactor=2,MidFactor=2,BankTaps=32;
// A one-pole coefficient tuned per 96 kHz sample, converted to keep its time constant.
inline float perSample(float c96){return 1-std::pow(1-c96,96000.f/Rate);}
inline float softLimit(float x){return x*(27+x*x)/(27+9*x*x);}
inline float softClip(float x){return x<-3?-1:x>3?1:softLimit(x);}
// Per-sample constants (96 kHz values converted).
const float kLimiterUp=perSample(.05f),kLimiterDown=perSample(.00002f),kDuckUp=perSample(.01f),kDuckDown=perSample(.0001f),
 kSawLp=perSample(.3f),kPulseLp=perSample(.25f),kGateUp=perSample(.1f),kGateDown=perSample(.0001f),kFeedbackLp=perSample(.2f);
inline float interpolate(const float*t,float index,float size){index*=size;int i=int(index);float f=index-float(i);return t[i]+(t[i+1]-t[i])*f;}
inline float clip16(float x){return std::clamp(x,-1.f,32767.f/32768);}
inline int16_t toShort(float x){return int16_t(std::clamp(int32_t(x*32768),-32768,32767));}
struct Ramp {   // stmlib ParameterInterpolator
 float*state,value,step;
 Ramp(float*s,float target,int n):state(s),value(*s),step((target-*s)/float(n)){}
 ~Ramp(){*state=value;}
 float next(){value+=step;return value;}
};

// The timbre.raw transfer curve of the wavefolder, every 8th of its 4097 points.
const float timbre513[]={
-.40976,-.40927,-.40881,-.40836,-.40787,-.40738,-.40692,-.40643,-.40591,-.40543,-.40491,-.40442,-.40390,-.40338,-.40283,-.40231,
-.40176,-.40121,-.40067,-.40009,-.39954,-.39896,-.39838,-.39780,-.39719,-.39658,-.39597,-.39536,-.39471,-.39407,-.39343,-.39279,
-.39212,-.39145,-.39075,-.39008,-.38937,-.38864,-.38791,-.38718,-.38644,-.38568,-.38489,-.38409,-.38330,-.38248,-.38165,-.38083,
-.37994,-.37909,-.37817,-.37729,-.37634,-.37540,-.37445,-.37344,-.37244,-.37143,-.37036,-.36929,-.36819,-.36707,-.36591,-.36472,
-.36349,-.36227,-.36099,-.35965,-.35831,-.35693,-.35550,-.35400,-.35248,-.35089,-.34927,-.34760,-.34586,-.34402,-.34216,-.34018,
-.33813,-.33603,-.33380,-.33145,-.32901,-.32645,-.32373,-.32086,-.31781,-.31458,-.31113,-.30740,-.30341,-.29907,-.29437,-.28922,
-.28351,-.27719,-.27005,-.26196,-.25269,-.24188,-.22922,-.21417,-.19632,-.17529,-.15112,-.12427,-.09546,-.06552,-.03503,-.00436,
.02615,.05627,.08563,.11371,.13986,.16342,.18390,.20102,.21475,.22470,.22900,.22485,.21414,.20026,.18430,.16626,
.14630,.12469,.10187,.07812,.05386,.02930,.00458,-.02011,-.04465,-.06891,-.09268,-.11569,-.13757,-.15799,-.17654,-.19315,
-.20770,-.22040,-.23141,-.24103,-.24942,-.25684,-.26337,-.26917,-.27429,-.27878,-.28253,-.28519,-.28506,-.28021,-.27316,-.26489,
-.25534,-.24414,-.23083,-.21487,-.19565,-.17285,-.14645,-.11716,-.08582,-.05331,-.02023,.01297,.04593,.07828,.10947,.13873,
.16522,.18826,.20767,.22366,.23669,.24698,.25357,.25308,.24402,.23264,.21933,.20370,.18542,.16431,.14059,.11472,
.08740,.05911,.03040,.00143,-.02747,-.05615,-.08432,-.11157,-.13736,-.16110,-.18228,-.20071,-.21649,-.22989,-.24130,-.25110,
-.25952,-.26685,-.27310,-.27811,-.27869,-.27390,-.26660,-.25830,-.24875,-.23767,-.22461,-.20920,-.19098,-.16974,-.14560,-.11899,
-.09064,-.06119,-.03113,-.00085,.02945,.05948,.08896,.11737,.14407,.16840,.18979,.20810,.22351,.23654,.24759,.25702,
.26514,.27164,.26855,.26093,.25226,.24234,.23087,.21741,.20166,.18326,.16214,.13849,.11282,.08563,.05765,.02905,
.00037,-.02847,-.05698,-.08511,-.11230,-.13818,-.16196,-.18320,-.20160,-.21741,-.23083,-.24234,-.25220,-.26083,-.26840,-.27048,
-.26508,-.25699,-.24750,-.23645,-.22336,-.20792,-.18964,-.16837,-.14417,-.11761,-.08932,-.05994,-.02994,.00034,.03064,.06067,
.09015,.11853,.14520,.16946,.19080,.20914,.22461,.23767,.24878,.25836,.26663,.27393,.27954,.27838,.27325,.26703,
.25970,.25137,.24158,.23022,.21683,.20114,.18268,.16156,.13779,.11209,.08481,.05673,.02805,-.00082,-.02979,-.05847,
-.08673,-.11404,-.13992,-.16367,-.18484,-.20328,-.21899,-.23239,-.24380,-.25223,-.25357,-.24695,-.23666,-.22372,-.20776,-.18851,
-.16556,-.13922,-.10995,-.07886,-.04642,-.01352,.01978,.05283,.08548,.11691,.14648,.17297,.19592,.21506,.23099,.24420,
.25540,.26492,.27316,.28033,.28497,.28506,.28250,.27878,.27429,.26917,.26337,.25681,.24942,.24100,.23141,.22040,
.20776,.19324,.17673,.15823,.13794,.11612,.09314,.06943,.04517,.02063,-.00406,-.02875,-.05338,-.07764,-.10144,-.12433,
-.14603,-.16608,-.18420,-.20023,-.21420,-.22552,-.23007,-.22519,-.21512,-.20135,-.18427,-.16379,-.14023,-.11407,-.08600,-.05667,
-.02658,.00394,.03458,.06503,.09494,.12369,.15051,.17474,.19583,.21381,.22897,.24173,.25256,.26190,.27002,.27716,
.28351,.28922,.29440,.29910,.30344,.30743,.31113,.31461,.31784,.32089,.32376,.32648,.32904,.33148,.33383,.33606,
.33820,.34021,.34219,.34406,.34589,.34763,.34930,.35092,.35251,.35403,.35550,.35693,.35834,.35968,.36099,.36227,
.36353,.36475,.36594,.36707,.36819,.36929,.37039,.37143,.37247,.37347,.37445,.37543,.37637,.37729,.37820,.37912,
.37997,.38083,.38168,.38251,.38333,.38412,.38492,.38568,.38644,.38721,.38794,.38867,.38937,.39011,.39078,.39148,
.39215,.39282,.39346,.39410,.39474,.39539,.39600,.39661,.39722,.39780,.39841,.39899,.39957,.40012,.40070,.40125,
.40179,.40231,.40286,.40338,.40390,.40442,.40494,.40546,.40594,.40643,.40692,.40741,.40790,.40839,.40884,.40930,
.40976
};
// Warps filter_bank.py at 44.1 kHz: decimation, delay, post gain, then f/fq of two SVF passes.
const float bankTable[Bands][7]={
 {4,152,1.f,-0.0510839303f,0.0172912597f,-0.0293996383f,0.041258235f},
 {4,422,0.25f,-0.0576075745f,0.00938120035f,-0.0678492026f,0.0110426141f},
 {4,335,0.25f,-0.0725310117f,0.0118059177f,-0.0854139561f,0.013892013f},
 {4,265,0.25f,-0.0913017675f,0.0148532105f,-0.107499039f,0.0174695597f},
 {4,211,0.25f,-0.114899047f,0.0186807557f,-0.135249307f,0.0219569476f},
 {4,167,0.25f,-0.144542128f,0.0234851287f,-0.170086009f,0.0275784161f},
 {4,132,0.25f,-0.181742054f,0.0295110287f,-0.213762744f,0.0346088348f},
 {4,105,0.25f,-0.228358273f,0.0370627133f,-0.268423332f,0.0433819261f},
 {4,83,0.25f,-0.286654332f,0.0465184267f,-0.336651538f,0.0542973008f},
 {4,66,0.25f,-0.35933891f,0.0583492685f,-0.421491042f,0.0678236148f},
 {4,52,0.25f,-0.449562818f,0.0731454273f,-0.526391877f,0.0844925567f},
 {4,41,0.25f,-0.56081208f,0.0916561443f,-0.65499845f,0.104873213f},
 {4,33,0.25f,-0.696578068f,0.114858284f,-0.810620406f,0.129505219f},
 {2,52,0.25f,-0.449562818f,0.0731454273f,-0.526391877f,0.0844925567f},
 {2,41,0.25f,-0.56081208f,0.0916561443f,-0.65499845f,0.104873213f},
 {2,33,0.25f,-0.696578068f,0.114858284f,-0.810620406f,0.129505219f},
 {2,26,0.25f,-0.859572409f,0.144091469f,-0.99510289f,0.158741511f},
 {2,20,0.25f,-1.05003563f,0.181370371f,-1.2066292f,0.192367534f},
 {2,16,0.25f,-1.43583917f,0.22852507f,-1.26223194f,0.230271508f},
 {1,7,4.85f,-0.842103008f,0.274872309f,-0.955125849f,0.853684579f}
};
// Warps lookup_tables.py all-pass poles (elliptic half-band, 10 Hz..20 kHz) at 44.1 kHz.
const float apPoles[17]={0.999637481f,0.998753475f,0.997322722f,0.994719893f,0.989815291f,0.980507763f,0.962908034f,0.93000663f,
 0.869868414f,0.764367928f,0.591965334f,0.340387791f,0.0276153682f,-0.293214133f,-0.566466281f,-0.771847696f,-0.927838218f};

struct Tables {
 float sine[1282],xfadeIn[258],xfadeOut[258],fold[4098],wave[6][1026];
 Tables(){
  for(int i=0;i<1281;i++)sine[i]=float(std::sin(i/1024.0*2*M_PI));sine[1281]=sine[1280];
  for(int i=0;i<257;i++){double t=std::clamp(1.04*i/257.0-.02,0.0,1.0)*M_PI/2;xfadeIn[i]=float(std::sin(t)*std::sqrt(.5));xfadeOut[i]=float(std::cos(t)*std::sqrt(.5));}
  xfadeIn[257]=xfadeIn[256];xfadeOut[257]=xfadeOut[256];
  // timbre.raw at full resolution (Hermite between the stored points).
  static_assert(std::size(timbre513)==513);
  std::vector<float> timbre(4097);
  for(int i=0;i<4097;i++){int k=std::min(i/8,511);float f=(i-k*8)/8.f;auto at=[&](int j){return timbre513[std::clamp(j,0,512)];};
   float xm1=k?at(k-1):2*at(0)-at(1),x0=at(k),x1=at(k+1),x2=k+2<=512?at(k+2):2*at(512)-at(511);
   float c=(x1-xm1)*.5f,v=x0-x1,w=c+v,a=w+v+(x2-x0)*.5f,b=w+a;timbre[i]=((a*f-b)*f+c)*f+x0;}
  for(int i=0;i<=4096;i++){double t=i/4096.0,x=2*t-1,bump=std::exp(-x*x*4);bump*=bump;
   x=x*bump+(1-bump)*((t+std::pow(t,1.3))-1);x*=8;x=x/(1+std::fabs(x));
   double value=(x+1)*.5*4095;int vi=std::clamp(int(value),0,4095);double vf=value-vi;
   fold[i]=float((timbre[vi]+(timbre[vi+1]-timbre[vi])*vf)*2.2);}
  fold[4097]=fold[4096];
  // Quadrature wavetables (waveforms.py): sine, harmonics, buzzy, each with its Hilbert pair.
  for(int s=0;s<3;s++){double peak=0;
   for(int k=0;k<=1024;k++){double t=k/1024.0*2*M_PI,i=0,q=0;
    auto add=[&](double a,int h,double ph){i+=a*std::sin(h*t+ph);q+=a*std::cos(h*t+ph);};
    if(s==0)add(-1,1,0);else if(s==1){add(-1,1,0);add(-.5,2,0);add(.5,5,0);}else for(int n=0;n<7;n++)add(std::sin(1.123*n),1+n,1.012*n);
    wave[2*s][k]=float(i);wave[2*s+1][k]=float(q);peak=std::max({peak,std::fabs(i),std::fabs(q)});}
   for(int c=0;c<2;c++){for(int k=0;k<=1024;k++)wave[2*s+c][k]=float(wave[2*s+c][k]/peak);wave[2*s+c][1025]=wave[2*s+c][1024];}}
 }
};

// Kaiser-windowed sinc low-pass (replacing the remez sample-rate conversion tables).
double besselI0(double x){double s=1,t=1;for(int k=1;k<40;k++){t*=(x/(2*k))*(x/(2*k));s+=t;}return s;}
void designLowpass(float*c,int n,double cutoff,double gain){
 const double beta=5;double sum=0;std::vector<double> h{};h.resize(size_t(n));
 for(int i=0;i<n;i++){double m=i-(n-1)/2.0,r=2.0*i/(n-1)-1;double s=m==0?2*cutoff:std::sin(2*M_PI*cutoff*m)/(M_PI*m);
  h[size_t(i)]=s*besselI0(beta*std::sqrt(std::max(0.0,1-r*r)))/besselI0(beta);sum+=h[size_t(i)];}
 for(int i=0;i<n;i++)c[i]=float(h[size_t(i)]*gain/sum);
}
template<int K,int T> struct Upsampler {
 static constexpr int N=T/K;float c[K][N],x[2*N]{};int at=0;   // coefficients by phase
 void init(double cutoff){float h[T];designLowpass(h,T,cutoff/K,K);for(int k=0;k<K;k++)for(int j=0;j<N;j++)c[k][j]=h[j*K+k];}
 void clear(){std::fill(std::begin(x),std::end(x),0.f);at=0;}
 static constexpr int delay(){return T/K/2;}
 void process(const float*in,float*out,int n){
  for(int s=0;s<n;s++){at=at?at-1:N-1;x[at]=x[at+N]=in[s];const float*h=x+at;
   for(int k=0;k<K;k++){float y=0;for(int j=0;j<N;j++)y+=h[j]*c[k][j];*out++=y;}}
 }
};
template<int K,int T> struct Downsampler {
 float c[T],x[2*T]{};int at=0;
 void init(double cutoff){designLowpass(c,T,cutoff,1);}
 void clear(){std::fill(std::begin(x),std::end(x),0.f);at=0;}
 static constexpr int delay(){return T/2;}
 void process(const float*in,float*out,int n){
  for(int s=0;s<n;s+=K){for(int k=0;k<K;k++){at=at?at-1:T-1;x[at]=x[at+T]=in[s+k];}
   const float*h=x+at;float y=0;for(int j=0;j<T;j++)y+=h[j]*c[j];*out++=y;}
 }
};

// stmlib CrossoverSvf: two cascaded modified-Chamberlin sections. Mode 0 LP, 1 BP normalized, 2 HP.
struct CrossoverSvf {
 float f=0,fq=0,lp[2]{},bp[2]{},x[2]{};
 void clear(){lp[0]=lp[1]=bp[0]=bp[1]=x[0]=x[1]=0;}
 template<int mode> void process(const float*in,float*out,int n){
  float l1=lp[0],b1=bp[0],l2=lp[1],b2=bp[1],x1=x[0],x2=x[1];
  for(int i=0;i<n;i++){
   l1+=f*b1;b1+=-fq*b1-f*l1+in[i];if(mode==1)b1+=x1;x1=in[i];
   float y=mode==0?l1*f:mode==1?b1*fq:x1-l1*f-b1*fq;
   l2+=f*b2;b2+=-fq*b2-f*l2+y;if(mode==1)b2+=x2;x2=y;
   out[i]=mode==0?l2*f:mode==1?b2*fq:x2-l2*f-b2*fq;
  }
  lp[0]=l1;bp[0]=b1;lp[1]=l2;bp[1]=b2;x[0]=x1;x[1]=x2;
 }
};

class FilterBank {
public:
 struct Band{int group,decimation,delay;float sampleRate,postGain;CrossoverSvf svf[2];float*samples;float*line;int lineSize,head;};
 Band band[Bands+1];
 void init(){
  midDown.init(.2);lowDown.init(.2);midUp.init(.4);lowUp.init(.4);
  float*s=samples;int group=-1,decimation=-1,maxDelay=0;
  for(int i=0;i<Bands;i++){Band&b=band[i];const float*c=bankTable[i];b.decimation=int(c[0]);
   if(b.decimation!=decimation){decimation=b.decimation;group++;}
   b.group=group;b.sampleRate=Rate/float(b.decimation);b.samples=s;s+=Block/b.decimation;
   b.delay=int(c[1])*b.decimation;b.postGain=c[2];maxDelay=std::max(maxDelay,b.delay);
   for(int p=0;p<2;p++){b.svf[p].clear();b.svf[p].f=c[3+2*p];b.svf[p].fq=c[4+2*p];}}
  band[Bands].group=band[Bands-1].group+1;
  maxDelay=std::min(maxDelay,118);   // Warps: 256 at 96 kHz
  float*line=lines;
  for(int i=0;i<Bands;i++){Band&b=band[i];int comp=maxDelay-b.delay;
   if(b.group==0)comp-=LowFactor*(lowDown.delay()+lowUp.delay())+midDown.delay()+midUp.delay();
   else if(b.group==1)comp-=midDown.delay()+midUp.delay();
   comp=std::max(comp-b.decimation/2,0);b.lineSize=comp/b.decimation+1;b.line=line;b.head=0;line+=b.lineSize;}
  clear();
 }
 void clear(){midDown.clear();lowDown.clear();midUp.clear();lowUp.clear();
  for(int i=0;i<Bands;i++){band[i].svf[0].clear();band[i].svf[1].clear();}std::fill(std::begin(lines),std::end(lines),0.f);std::fill(std::begin(samples),std::end(samples),0.f);}
 void analyze(const float*in,int n){
  midDown.process(in,tmp[0],n);lowDown.process(tmp[0],tmp[1],n/MidFactor);
  const float*sources[3]={tmp[1],tmp[0],in};
  for(int i=0;i<Bands;i++){Band&b=band[i];int size=n/b.decimation;
   for(int p=0;p<2;p++){const float*src=p==0?sources[b.group]:b.samples;
    if(i==0)b.svf[p].process<0>(src,b.samples,size);else if(i==Bands-1)b.svf[p].process<2>(src,b.samples,size);else b.svf[p].process<1>(src,b.samples,size);}
   for(int j=0;j<size;j++)b.samples[j]*=b.postGain;}
 }
 void synthesize(float*out,int n){
  float*buffers[3]={tmp[1],tmp[0],out};
  std::fill(buffers[0],buffers[0]+n/band[0].decimation,0.f);
  for(int i=0;i<Bands;i++){Band&b=band[i];int size=n/b.decimation;float*s=buffers[b.group];
   for(int j=0;j<size;j++){b.line[b.head]=b.samples[j];b.head=b.head+1==b.lineSize?0:b.head+1;s[j]+=b.line[b.head];}
   if(band[i+1].group!=b.group){if(b.group==0)lowUp.process(tmp[1],tmp[0],size);else if(b.group==1)midUp.process(tmp[0],out,size);}}
 }
private:
 Downsampler<MidFactor,BankTaps> midDown;Downsampler<LowFactor,BankTaps> lowDown;
 Upsampler<MidFactor,BankTaps> midUp;Upsampler<LowFactor,BankTaps> lowUp;
 float tmp[2][Block],samples[Block*Bands],lines[512];
};

class Vocoder {
 struct Follower{float attack=.1f,decay=.1f,envelope=0,peak=0;bool freeze=false;
  void process(const float*in,float*out,int n){float a=freeze?0:attack,d=freeze?0:decay,p=0;const float g=std::sqrt(float(Bands));
   for(int i=0;i<n;i++){float e=std::fabs(in[i]*g)-envelope;envelope+=(e>0?a:d)*e;p=std::max(p,envelope);out[i]=envelope;}
   float e=p-peak;peak+=(e>0?.5f:.1f)*e;}};
 FilterBank modulatorBank,carrierBank;Follower follower[Bands];
 float previousCarrier[Bands]{},previousVocoder[Bands]{},gainCarrier[Bands]{},gainVocoder[Bands]{},tmp[Block],limiterPeak=.5f;
public:
 float release=.5f,formant=.5f;
 void init(){modulatorBank.init();carrierBank.init();clear();}
 void clear(){modulatorBank.clear();carrierBank.clear();for(auto&f:follower)f=Follower{};limiterPeak=.5f;
  std::fill(std::begin(previousCarrier),std::end(previousCarrier),0.f);std::fill(std::begin(previousVocoder),std::end(previousVocoder),0.f);}
 void process(const float*modulator,const float*carrier,float*out,int n){
  modulatorBank.analyze(modulator,n);carrierBank.analyze(carrier,n);
  float f=80*std::exp2(-72*release/12);
  for(int i=0;i<Bands;i++){float d=f/modulatorBank.band[i].sampleRate;follower[i].attack=d*2;follower[i].decay=d*.5f;follower[i].freeze=release>.995f;f*=1.2599f;}
  float amount=2*std::fabs(formant-.5f);amount*=2-amount;amount*=2-amount;
  float increment=4*std::exp2(-48*formant/12),envelope=0;const float last=Bands-1.0001f;
  for(int i=0;i<Bands;i++){float src=std::clamp(envelope,0.f,last);int si=int(src);float sf=src-float(si);
   float a=follower[si].peak,b=follower[si+1].peak,g=a+(b-a)*sf,att=envelope-last;if(att>=0)g*=1/(1+att);
   envelope+=increment;gainCarrier[i]=g*amount;gainVocoder[i]=1-amount;}
  for(int i=0;i<Bands;i++){int size=n/modulatorBank.band[i].decimation;float step=1.f/float(size);
   float*c=carrierBank.band[i].samples;follower[i].process(modulatorBank.band[i].samples,tmp,size);
   float vg=previousVocoder[i],vi=(gainVocoder[i]-vg)*step,cg=previousCarrier[i],ci=(gainCarrier[i]-cg)*step;
   for(int j=0;j<size;j++){c[j]*=cg+vg*tmp[j];vg+=vi;cg+=ci;}
   previousVocoder[i]=gainVocoder[i];previousCarrier[i]=gainCarrier[i];}
  carrierBank.synthesize(out,n);
  for(int i=0;i<n;i++){float s=out[i]*1.4f,e=std::fabs(s)-limiterPeak;limiterPeak+=(e>0?kLimiterUp:kLimiterDown)*e;
   float g=limiterPeak<=1?1:1/limiterPeak;out[i]=softLimit(s*g*.8f);}
 }
};

class Oscillator {   // Warps internal oscillator: 0 sine, 1 triangle, 2 saw, 3 pulse, 4 noise
 bool high=false;float phase=0,increment=100/Rate,nextSample=0,lp=0,hp=0,inputLevel=0;
 float g=0,r=1,h=1,s1=0,s2=0;uint32_t seed=1;
 static float thisBlep(float t){return .5f*t*t;}
 static float nextBlep(float t){t=1-t;return -.5f*t*t;}
public:
 const Tables*tables=nullptr;
 void seedWith(uint32_t s){seed=s;}
 void clear(){high=false;phase=nextSample=lp=hp=inputLevel=s1=s2=0;}
 void resetPhase(){phase=0;high=false;nextSample=0;}
 // Returns the carrier gain (the pulse compensates its density).
 float render(int shape,float target,const float*mod,float*out,int n){
  if(shape==0){Ramp inc(&increment,target,n);float p=phase;
   for(int i=0;i<n;i++){p+=inc.next();if(p>=1)p-=1;
    uint32_t mp=uint32_t(double(p)*4294967296.0)+uint32_t(int32_t(std::clamp(mod[i],-.9999f,.9999f)*.5f*4294967296.0f));
    uint32_t k=mp>>22;float f=float(mp<<10)*(1.f/4294967296.f);out[i]=tables->sine[k]+(tables->sine[k+1]-tables->sine[k])*f;}
   phase=p;return 1;}
  if(shape==4){
   for(int i=0;i<n;i++){seed=seed*1664525u+1013904223u;out[i]=2*float(seed)*(1.f/4294967296.f)-1;}
   float level=inputLevel;
   for(int i=0;i<n;i++){float e=mod[i]*mod[i]-level;level+=(e>0?kDuckUp:kDuckDown)*e;float gain=std::max(0.f,1-32*level);out[i]=mod[i]+gain*(out[i]-mod[i]);}
   inputLevel=level;increment=target;
   g=std::tan(NativePi*std::min(target*4,.497f));r=1;h=1/(1+r*g+g*g);
   for(int i=0;i<n;i++){float hpv=(out[i]-r*s1-g*s1-s2)*h,bp=g*hpv+s1;s1=g*hpv+bp;float l=g*bp+s2;s2=g*bp+l;out[i]=l;}
   return 1;}
  Ramp inc(&increment,target,n);float p=phase,next=nextSample,l=lp,hs=hp;bool hi=high;
  for(int i=0;i<n;i++){float sample=next;next=0;float mi=inc.next()*(1+mod[i]);if(mi<=0)mi=1e-7f;p+=mi;
   if(shape==1){
    if(!hi&&p>=.5f){float t=(p-.5f)/mi;sample+=thisBlep(t);next+=nextBlep(t);hi=true;}
    if(p>=1){p-=1;float t=p/mi;sample-=thisBlep(t);next-=nextBlep(t);hi=false;}
    next+=p<.5f?0:1;sample=128*(sample-.5f);l+=mi*.0625f*(sample-l);out[i]=l;
   }else{
    if(p>=1){p-=1;float t=p/mi;sample-=thisBlep(t);next-=nextBlep(t);}
    next+=p;
    if(shape==2){sample=sample*2-1;l+=kSawLp*(sample-l);out[i]=l;}
    else{l+=kPulseLp*((hs-sample)-l);out[i]=4*l;hs=sample;}
   }
  }
  high=hi;phase=p;nextSample=next;lp=l;hp=hs;
  // 0.025 / (0.0002 + increment) at 96 kHz, scaled for the denser 44.1 kHz pulse.
  return shape==3?.025f/(.0002f+increment*Rate/96000)*(Rate/96000):1;
 }
};

struct Amplifier {   // input VCA with noise gate and soft saturation
 float level=0,drive=0,preGain=0,postGain=0;
 void clear(){level=drive=preGain=postGain=0;}
 void process(float target,float limit,const float*in,float*out,int n){
  {float lv=level;for(int i=0;i<n;i++){float s=in[i],e=s*s-lv;lv+=e*(e>0?kGateUp:kGateDown);s*=lv<=.0001f?(1/.0001f)*lv:1;out[i]=s;}level=lv;}
  drive=target;   // the raw output (Warps' aux sum) is not used here
  float d2=target*target,a=target*.5f,b=d2*d2*target*24,pre=a+(b-a)*d2,squished=target*(2-target);
  float post=1/softClip(.33f+squished*(pre-.33f));
  Ramp pg(&preGain,pre,n),qg(&postGain,post,n);
  for(int i=0;i<n;i++){float x=pg.next()*out[i],y=softClip(x)*qg.next();out[i]=x+(y-x)*limit;}
 }
};

class Quadrature {   // Hilbert pair from two all-pass chains
 float c[17],x[17]{},y[17]{};
public:
 void init(){for(int i=0;i<17;i++)c[i]=-apPoles[i];clear();}
 void clear(){std::fill(std::begin(x),std::end(x),0.f);std::fill(std::begin(y),std::end(y),0.f);}
 void process(float in,float&iOut,float&qOut){
  // Even filters chain into I, odd ones into Q (Warps' order).
  float vi=in,vq=in;
  for(int k=0;k<16;k+=2){float oi=c[k]*(vi-y[k])+x[k];x[k]=vi;y[k]=oi;vi=oi;float oq=c[k+1]*(vq-y[k+1])+x[k+1];x[k+1]=vq;y[k+1]=oq;vq=oq;}
  float o=c[16]*(vi-y[16])+x[16];x[16]=vi;y[16]=o;iOut=o;qOut=vq;
 }
};

struct Settings{float drive[2],algorithm,parameter,shiftPot,phaseShift,increment;int carrierShape;
 float skewed()const{float skew=algorithm<=1?algorithm:1;return parameter*(1+skew*(parameter-1));}};   // Warps' skew (its 1..5 branches never apply)

class Modulator {
 const Tables*T=nullptr;Amplifier amplifier[2];Oscillator xmodOsc,vocoderOsc;Upsampler<Over,OverTaps> up[2];Downsampler<Over,OverTaps> down;
 Vocoder vocoder;Quadrature quadrature[2];float qPhase=0,qFrequency=0,qShape=0,feedback=0;
 float carrierBuffer[Block],modulatorBuffer[Block],aux[Block],internal[Block],overCarrier[Block*Over],overModulator[Block*Over],overOut[Block*Over];
 Settings previous{};
 typedef float(*Xmod)(float,float,float);
 static float diode(float x){float s=x>0?1.f:-1.f,d=std::fabs(x)-.667f;d+=std::fabs(d);d*=d;return .04324765822726063f*d*s;}
 float xfade(float a,float b,float p)const{return a*interpolate(T->xfadeIn,p,256)+b*interpolate(T->xfadeOut,p,256);}
 template<int algorithm> float xmod(float x1,float x2,float p)const{
  switch(algorithm){
   case 0:return xfade(x1,x2,p);
   case 1:{float sum=(x1+x2+x1*x2*.25f)*(.02f+p);const float scale=2048/((1+1+.25f)*1.02f);
    float index=std::clamp(sum*scale,-2048.f,2047.999f);int i=int(std::floor(index));float f=index-float(i);const float*t=T->fold+2048;return t[i]+(t[i+1]-t[i])*f;}
   case 2:{float c=x2*2,ring=diode(x1+c)+diode(x1-c);ring*=4+p*24;return softLimit(ring);}
   case 3:{float ring=4*x1*x2*(1+p*8);return ring/(1+std::fabs(ring));}
   case 4:{float mod=float(int16_t(toShort(x1)^toShort(x2)))/32768,sum=(x1+x2)*.7f;return sum+(mod-sum)*p;}
   case 5:{float x=p*2.995f;int xi=int(x);float xf=x-float(xi);
    float direct=x1<x2?x1:x2,window=std::fabs(x1)>std::fabs(x2)?x1:x2,window2=std::fabs(x1)>std::fabs(x2)?std::fabs(x1):-std::fabs(x2),threshold=x2>.05f?x2:x1;
    float seq[4]={direct,threshold,window,window2};return seq[xi]+(seq[xi+1]-seq[xi])*xf;}
   default:return x1;
  }
 }
public:
 void init(const Tables*t,uint32_t seed){
  T=t;xmodOsc.tables=vocoderOsc.tables=t;xmodOsc.seedWith(seed);vocoderOsc.seedWith(seed*747796405u+1);
  for(auto&u:up)u.init(.4);down.init(.4/Over);vocoder.init();for(auto&q:quadrature)q.init();clear();
 }
 void clear(){for(auto&a:amplifier)a.clear();xmodOsc.clear();vocoderOsc.clear();for(auto&u:up)u.clear();down.clear();vocoder.clear();
  for(auto&q:quadrature)q.clear();qPhase=qFrequency=qShape=feedback=0;previous=Settings{};}
 void resetPhase(){xmodOsc.resetPhase();vocoderOsc.resetPhase();qPhase=0;}
 // Warps' ProcessXmod: crossfade between two neighbouring algorithms over the oversampled block.
 template<int A> void xmodPair(int size,float param,float paramStep,float balance,float balanceStep){
  for(int i=0;i<size;i++){float x1=overModulator[i],x2=overCarrier[i];float a=xmod<A>(x1,x2,param),b=xmod<A+1>(x1,x2,param);
   overOut[i]=a+(b-a)*balance;param+=paramStep;balance+=balanceStep;}
 }
 // The input amplifiers and the internal oscillator: fill the carrier and modulator buffers.
 float front(const Settings&p,const float*in1,const float*in2,int n){
  float a1[Block],a2[Block];for(int i=0;i<n;i++){a1[i]=clip16(in1[i]);a2[i]=clip16(in2[i]);}
  float vocoderAmount=std::clamp((p.algorithm-.7f)*20+.5f,0.f,1.f);
  float*carrier=carrierBuffer,*modulator=modulatorBuffer;
  for(int c=p.carrierShape?1:0;c<2;c++)amplifier[c].process(p.drive[c],1-vocoderAmount,c?a2:a1,c?modulator:carrier,n);
  if(p.carrierShape){
   for(int i=0;i<n;i++)internal[i]=a1[i];
   int xs=p.carrierShape-1,vs=p.carrierShape+1;const float xmodGain=.5f;
   if(vocoderAmount==0){xmodOsc.render(xs,p.increment,internal,aux,n);for(int i=0;i<n;i++)carrier[i]=aux[i]*xmodGain;}
   else if(vocoderAmount>=.5f){float g=vocoderOsc.render(vs,p.increment,internal,aux,n);for(int i=0;i<n;i++)carrier[i]=aux[i]*g;}
   else{float balance=vocoderAmount*2;xmodOsc.render(xs,p.increment,internal,carrier,n);float g=vocoderOsc.render(vs,p.increment,internal,aux,n);
    for(int i=0;i<n;i++){float a=carrier[i]*xmodGain,b=aux[i]*g;carrier[i]=a+(b-a)*balance;}}
  }
  return vocoderAmount;
 }
 void vocode(const Settings&p,const float*modulator,const float*carrier,float*out,int n){
  float release=std::clamp(4*(p.algorithm-.75f),0.f,1.f);
  vocoder.release=release*(2-release);vocoder.formant=p.parameter;vocoder.process(modulator,carrier,out,n);
 }
 // in1 carrier (or FM with the oscillator), in2 modulator; out main, outAux the shifter's other sideband.
 void process(const Settings&p,const float*in1,const float*in2,float*out,float*outAux,int n){
  float vocoderAmount=front(p,in1,in2,n),*carrier=carrierBuffer,*modulator=modulatorBuffer,*main=carrierBuffer;
  if(vocoderAmount<.5f){
   up[0].process(carrier,overCarrier,n);up[1].process(modulator,overModulator,n);
   float algorithm=std::min(p.algorithm*8,5.999f),previousAlgorithm=std::min(previous.algorithm*8,5.999f);
   int ai=int(algorithm),pi=int(previousAlgorithm);float af=algorithm-float(ai),pf=previousAlgorithm-float(pi);if(ai!=pi)pf=af;
   int size=n*Over;float step=1.f/float(size),param=previous.skewed(),paramStep=(p.skewed()-param)*step,balance=pf,balanceStep=(af-pf)*step;
   switch(ai){
    case 0:xmodPair<0>(size,param,paramStep,balance,balanceStep);break;
    case 1:xmodPair<1>(size,param,paramStep,balance,balanceStep);break;
    case 2:xmodPair<2>(size,param,paramStep,balance,balanceStep);break;
    case 3:xmodPair<3>(size,param,paramStep,balance,balanceStep);break;
    case 4:xmodPair<4>(size,param,paramStep,balance,balanceStep);break;
    default:xmodPair<5>(size,param,paramStep,balance,balanceStep);break;
   }
   down.process(overOut,main,size);
  }else vocode(p,modulator,carrier,main,n);
  float transition=2*(vocoderAmount<.5f?vocoderAmount:1-vocoderAmount);
  if(transition!=0)for(int i=0;i<n;i++)main[i]+=transition*(modulator[i]-main[i]);
  for(int i=0;i<n;i++)out[i]=outAux[i]=clip16(main[i]);
  previous=p;
 }
 // Vocoder zone in stereo: one vocoder on the mid of both channels (budget), each channel
 // keeping its own amplifier, oscillator and transition to its raw modulator.
 static void vocodePair(Modulator&l,Modulator&r,const Settings&p,const float*const in1[2],const float*const in2[2],float*const out[2],int n){
  Modulator*m[2]={&l,&r};float va=0,mid[Block],car[Block],main[Block];
  for(int c=0;c<2;c++)va=m[c]->front(p,in1[c],in2[c],n);
  for(int i=0;i<n;i++){mid[i]=.5f*(l.modulatorBuffer[i]+r.modulatorBuffer[i]);car[i]=.5f*(l.carrierBuffer[i]+r.carrierBuffer[i]);}
  l.vocode(p,mid,car,main,n);float transition=2*(1-va);
  for(int c=0;c<2;c++){for(int i=0;i<n;i++)out[c][i]=clip16(main[i]+transition*(m[c]->modulatorBuffer[i]-main[i]));m[c]->previous=p;}
 }
 // The easter egg: a frequency shifter.
 void processShifter(const Settings&p,const float*in1,const float*in2,float*out,float*outAux,int n){
  float ci[Block],cq[Block];
  if(p.carrierShape){
   float direction=p.shiftPot>=.5f?1.f:-1.f,f=2*std::fabs(p.shiftPot-.5f);
   f=f<=.4f?f*f*f*62.5f:4*std::exp2(180*(f-.4f)/12);f*=direction;
   float shape=float(p.carrierShape-1)*.5f,normalized=std::clamp(f/Rate,-.25f,.25f);
   Ramp fr(&qFrequency,normalized,n),sh(&qShape,shape,n);float ph=qPhase;
   for(int i=0;i<n;i++){ph+=fr.next();if(ph<=0)ph+=1;else if(ph>=1)ph-=1;
    float s=sh.next()*1.9999f;int si=int(s);float sf=s-float(si);float iq[2];
    for(int c=0;c<2;c++){float a=interpolate(T->wave[2*si+c],ph,1024),b=interpolate(T->wave[2*si+2+c],ph,1024);iq[c]=a+(b-a)*sf;}
    ci[i]=iq[0];cq[i]=iq[1];}
   qPhase=ph;
  }else{
   Ramp shift(&previous.phaseShift,p.phaseShift,n);
   for(int i=0;i<n;i++){float xi,xq;quadrature[0].process(clip16(in1[i]),xi,xq);float angle=shift.next();
    float rs=interpolate(T->sine,angle,1024),rc=interpolate(T->sine+256,angle,1024);ci[i]=rs*xi+rc*xq;cq[i]=rs*xq-rc*xi;}
  }
  Ramp mix(&previous.parameter,p.parameter,n),amountR(&previous.drive[0],p.drive[0],n),wet(&previous.drive[1],p.drive[1],n);
  float fb=feedback;
  for(int i=0;i<n;i++){float timbre=mix.next(),in=clip16(in2[i]);if(p.carrierShape)in+=clip16(in1[i]);
   float m=in,amount=amountR.next();amount*=2-amount;amount*=2-amount;float maxFb=1+2*(timbre-.5f)*(timbre-.5f);
   m+=amount*(softClip(m+maxFb*fb*amount)-m);float mi,mq;quadrature[1].process(m,mi,mq);
   float a=ci[i]*mi,b=cq[i]*mq,upS=a-b,downS=a+b;float fin=interpolate(T->xfadeIn,timbre,256),fout=interpolate(T->xfadeOut,timbre,256);
   float mainS=upS*fin+downS*fout,auxS=downS*fin+upS*fout;fb+=kFeedbackLp*(mainS-fb);
   float dry=1-wet.next();mainS+=dry*(in-mainS);auxS+=dry*(in-auxS);out[i]=clip16(mainS);outAux[i]=clip16(auxS);}
  feedback=std::fabs(fb)<1e-20f?0:fb;
  previous=p;   // the ramps above end on the same values
 }
};

class WarpsFx final:public NativeFx{
 std::unique_ptr<Tables> tables=std::make_unique<Tables>();
 Modulator core[2];
 float inBuf[2][Block]{},outBuf[2][Block]{};int pos=0;
 // Targets and Warps' pot smoothing (0.33 * 0.08 per block), note glide (0.1 per block).
 float tAlg=.31f,tParam=.5f,tDrive[2]{.5f,.5f},tNote=48,sAlg=.31f,sParam=.5f,sDrive[2]{.5f,.5f},sNote=48;
 int carrier=0,shape=0;bool shifter=false,sync=false,fresh=true;float fm=0;
 float gate=0;long hold=0;uint32_t epoch=0;
public:
 WarpsFx(){core[0].init(tables.get(),0x3a7e1u);core[1].init(tables.get(),0x91c5u);epoch=nativeTransportEpoch();}
 void configure(const uint8_t*v,float bpm)override{
  tAlg=v[0]*.01f;tParam=v[1]*.01f;carrier=nativeChoice(v[2],3);shape=nativeChoice(v[3],3);
  float hz=20*std::pow(200.f,v[4]*.01f);sync=v[5]>=50;
  if(sync)hz=float(44100/(nativeSamplesPerBeat(bpm)*nativeDivisionBeats(nativeDivision(v[6]))));
  tNote=69+12*std::log2(std::clamp(hz,.01f,12000.f)/440);
  fm=v[7]*.01f;for(int c=0;c<2;c++){float pot=v[8+c]*.01f;tDrive[c]=pot*pot;}
  shifter=v[10]>=50;
  if(fresh){sAlg=tAlg;sParam=tParam;sDrive[0]=tDrive[0];sDrive[1]=tDrive[1];sNote=tNote;fresh=false;}
 }
 void clear()override{for(auto&c:core)c.clear();for(auto&b:inBuf)std::fill(std::begin(b),std::end(b),0.f);for(auto&b:outBuf)std::fill(std::begin(b),std::end(b),0.f);pos=0;gate=0;hold=0;}
 void block(){
  const float pot=.33f*.08f;sAlg+=pot*(tAlg-sAlg);sParam+=pot*(tParam-sParam);for(int c=0;c<2;c++)sDrive[c]+=pot*(tDrive[c]-sDrive[c]);sNote+=.1f*(tNote-sNote);
  Settings s{};float alg=sAlg;if(alg<=.125f)alg=std::clamp(alg*1.08f-.01f,0.f,1.f);
  s.algorithm=alg;s.parameter=sParam;s.drive[0]=sDrive[0];s.drive[1]=sDrive[1];s.shiftPot=sAlg;s.phaseShift=sAlg;
  s.increment=std::min(440*std::exp2((sNote-69)/12),12000.f)/Rate;s.carrierShape=carrier==0?shape+1:0;
  float peak=0;for(int c=0;c<2;c++)for(int i=0;i<Block;i++)peak=std::max(peak,std::fabs(inBuf[c][i]));
  if(peak>1e-5f)hold=44100/4/Block;else if(hold>0)hold--;
  float o[2][Block],aux[Block];
  if(carrier==1){
   if(shifter)core[0].processShifter(s,inBuf[0],inBuf[1],o[0],o[1],Block);else core[0].process(s,inBuf[0],inBuf[1],o[0],o[1],Block);
  }else{
   float first[2][Block];const float*in1[2]={inBuf[0],inBuf[1]},*in2[2]={inBuf[0],inBuf[1]};float*out[2]={o[0],o[1]};
   if(carrier==0)for(int c=0;c<2;c++){for(int i=0;i<Block;i++)first[c][i]=inBuf[c][i]*fm;in1[c]=first[c];}
   if(!shifter&&s.algorithm>=.7f)Modulator::vocodePair(core[0],core[1],s,in1,in2,out,Block);
   else for(int c=0;c<2;c++){
   if(shifter)core[c].processShifter(s,in1[c],inBuf[c],o[c],aux,Block);else core[c].process(s,in1[c],inBuf[c],o[c],aux,Block);
  }}
  // The input-presence gate: about 2 ms up, 50 ms down, exact zero once closed.
  const float rise=1.f/96,fall=1.f/2205;
  for(int i=0;i<Block;i++){gate=hold>0?std::min(1.f,gate+rise):std::max(0.f,gate-fall);
   for(int c=0;c<2;c++)outBuf[c][i]=gate>0?nativeClean(o[c][i]*gate):0.f;}
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;if(sync)for(auto&c:core)c.resetPhase();}
  for(int i=0;i<frames;i++){
   for(int c=0;c<2;c++){inBuf[c][pos]=io[i*2+c];io[i*2+c]=outBuf[c][pos];}
   if(++pos==Block){pos=0;block();}
  }
 }
};
}
const NativeInfo warpsInfo{FxType::Warps,"WARPS","WARPS","WA",4,1,4,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeWarps(){return std::make_unique<WarpsFx>();}
}
