// FUNC + hold X on the INSTRUMENT column: per-row instrument variations.
#include "audio.hpp"
#include "native_engines.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
extern "C" {
#include "eng.h"
extern const eng_t ENG_VAP,ENG_PERC,ENG_CHIP,ENG_FIBER,ENG_HIVE,ENG_OP4,ENG_ANIMA,ENG_RUSSOLO,ENG_BAYAREA,ENG_RESURGENT,ENG_CALIGULA,ENG_FMDRUM;
void amalthea_patch(int,uint32_t,int32_t*);
void amalthea_patch_variant(int,uint32_t,uint32_t,int32_t*);
}
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
static const eng_t *bank[]={&ENG_VAP,&ENG_PERC,&ENG_CHIP,&ENG_FIBER,&ENG_HIVE,&ENG_OP4,&ENG_ANIMA,&ENG_RUSSOLO,&ENG_BAYAREA,&ENG_RESURGENT,&ENG_CALIGULA,&ENG_FMDRUM};

// Renders one held note of a cooked patch (mono, left channel).
static std::vector<float> renderNote(int e,const void *patch,int note,int blocks){
  alignas(16) static unsigned char voice[ENG_V_MAX];std::memset(voice,0,sizeof voice);
  auto native=nativeEngine(e)?makeNativeVoice():NativeVoicePtr{};if(native)bindNativeVoice(voice,native.get());
  auto E=bank[e];E->reset(voice);E->on(voice,patch,note*65536,127);
  std::vector<float> out;out.reserve(blocks*ENG_BLK);
  for(int b=0;b<blocks;b++){int32_t l[ENG_BLK]{},r[ENG_BLK]{};E->render(voice,patch,l,r);for(int i=0;i<ENG_BLK;i++)out.push_back(l[i]/32768.f);}
  return out;
}
// Magnitude spectrum (8192-point Hann FFT) of a stretch of a render.
static constexpr int FftSize=8192;
static std::vector<double> spectrum(const std::vector<float> &x,size_t from){
  std::vector<std::complex<double>> f(FftSize);
  for(int i=0;i<FftSize;i++)f[i]=x[from+i]*(.5-.5*std::cos(2*M_PI*i/FftSize));
  for(int i=1,j=0;i<FftSize;i++){int bit=FftSize>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(f[i],f[j]);}
  for(int len=2;len<=FftSize;len<<=1){auto w=std::polar(1.0,-2*M_PI/len);for(int i=0;i<FftSize;i+=len){std::complex<double> u(1);for(int k=0;k<len/2;k++){auto a=f[i+k],b=f[i+k+len/2]*u;f[i+k]=a+b;f[i+k+len/2]=a-b;u*=w;}}}
  std::vector<double> m(FftSize/2);for(int i=0;i<FftSize/2;i++)m[i]=std::abs(f[i]);return m;
}
int main(){
  // --- Editor: FUNC + hold X on the INSTRUMENT column -------------------------------------
  {
    Editor e;e.project=blankProject();e.column=1;auto &t=e.project.tracks[0];
    for(int r=0;r<16;r++){t.steps[r].note=48+r;if(r%4!=3)t.steps[r].instrument=uint8_t(r%3);}
    auto before=e.project;
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    std::set<uint32_t> drawn;
    for(int r=0;r<16;r++){
      const auto &s=t.steps[r],&o=before.tracks[0].steps[r];
      require(s.note==o.note&&s.instrument==o.instrument&&s.length==o.length&&s.modifier.semitones==o.modifier.semitones,"notes, instruments and modifiers kept");
      if(o.instrument==NoInstrument)require(s.variation==0,"a row without an instrument is not varied");
      else {require(s.variation!=0,"every row with an instrument varied");drawn.insert(s.variation);}
    }
    require(drawn.size()==12,"each row its own variation");
    for(int i=0;i<Instruments;i++)require(trackPatch(e.project,0,i).seed==trackPatch(before,0,i).seed&&trackPatch(e.project,0,i).engine==trackPatch(before,0,i).engine,"instrument patches themselves unchanged");
    for(int tr=1;tr<Tracks;tr++)for(int r=0;r<MaxRows;r++)require(e.project.tracks[tr].steps[r].variation==0,"other tracks untouched");
    require(e.undoStack.size()==1,"one undo step");
    e.action(Action::Undo);for(int r=0;r<16;r++)require(e.project.tracks[0].steps[r].variation==0,"undo removes the variations");
    e.action(Action::Redo);require(e.project.tracks[0].steps[0].variation!=0,"redo restores them");
    // A second press draws new ones.
    auto first=e.project.tracks[0].steps[0].variation;
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    require(e.project.tracks[0].steps[0].variation!=first,"a new press redraws");
    // Hold X (a new track sound) clears them.
    e.action(Action::RandomizeColumn);
    for(int r=0;r<16;r++)require(e.project.tracks[0].steps[r].variation==0,"hold X clears the row variations");
  }
  {
    // Long columns: at most VariationsPerTrack distinct draws, later rows reuse their instrument's.
    Editor e;e.project=blankProject();e.column=1;auto &t=e.project.tracks[2];e.track=2;
    t.length=64;for(int r=0;r<64;r++)t.steps[r].instrument=uint8_t(r%2);
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    std::set<std::pair<int,uint32_t>> pairs;
    for(int r=0;r<64;r++){require(t.steps[r].variation!=0,"every row of a long column varied");pairs.insert({t.steps[r].instrument,t.steps[r].variation});}
    require(int(pairs.size())==VariationsPerTrack,"distinct variations bounded per track");
  }
  {
    // Selected rows only; empty column says so and changes nothing.
    Editor e;e.project=blankProject();e.column=1;auto &t=e.project.tracks[0];
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    require(e.undoStack.empty()&&e.notice=="NO INSTRUMENTS ON ROWS","empty column refused");
    for(int r=0;r<16;r++)t.steps[r].instrument=0;
    e.row=2;e.action(Action::Shift2Down);e.action(Action::NextTrack);e.action(Action::NextTrack);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    for(int r=0;r<16;r++)require((t.steps[r].variation!=0)==(r>=2&&r<=4),"only the selected rows varied");
  }
  {
    // Editing one INSTRUMENT row: FUNC + hold X varies only it; changing its instrument clears it.
    Editor e;e.project=blankProject();e.column=1;auto &t=e.project.tracks[0];
    for(int r=0;r<16;r++)t.steps[r].instrument=1;
    e.row=5;e.action(Action::Enter);
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    require(e.pending.variation!=0&&t.steps[5].variation==0,"edit draws a pending variation");
    e.action(Action::Enter);require(t.steps[5].variation!=0,"commit stores it");
    for(int r=0;r<16;r++)if(r!=5)require(t.steps[r].variation==0,"other rows unchanged");
    e.action(Action::Enter);e.action(Action::Up);e.action(Action::Enter);
    require(t.steps[5].instrument==2&&t.steps[5].variation==0,"a new instrument drops the old variation");
    // ERASE clears it too.
    e.action(Action::Shift2Down);e.action(Action::RandomizeColumn);e.action(Action::Shift2Up);
    require(t.steps[0].variation!=0,"varied");
    e.row=0;e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);e.action(Action::Enter);
    require(t.steps[0].instrument==NoInstrument&&t.steps[0].variation==0,"erase clears the variation");
  }
  // --- Session format --------------------------------------------------------------------
  {
    auto p=blankProject();p.tracks[3].steps[7].instrument=4;p.tracks[3].steps[7].variation=0xdeadbeefu;p.tracks[11].steps[255].instrument=0;p.tracks[11].steps[255].variation=7;
    auto bytes=encodeProject(p);require(bytes[4]>=24,"AMAL24 payload");
    require(bytes.size()<200000,"payload fits the loader limit");
    Project q;std::string err;require(decodeProject(q,bytes,err),"AMAL24 decodes");
    require(encodeProject(blankProject()).size()+2*7==bytes.size(),"only varied rows are stored");
    require(q.tracks[3].steps[7].variation==0xdeadbeefu&&q.tracks[11].steps[255].variation==7&&q.tracks[0].steps[0].variation==0,"variations round-trip");
    // An AMAL23 payload (no variation tail) still loads, without variations.
    auto old=bytes;old.resize(old.size()-2-2*7);old[4]=23;
    uint32_t crc=~0u;for(size_t i=12;i<old.size();i++){crc^=old[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}
    crc=~crc;for(int j=0;j<4;j++)old[8+j]=(crc>>(8*j))&255;
    Project r;require(decodeProject(r,old,err),"AMAL23 still decodes");
    require(r.tracks[3].steps[7].instrument==4&&r.tracks[3].steps[7].variation==0,"older sessions carry no variations");
  }
  // --- Engines: a variation changes the cooked sound, never the pitch -----------------------
  int compared=0,periodic=0,controls=0,caught=0;
  for(int e=0;e<SynthEngines;e++){
    auto E=bank[e];if(E->init)E->init();int differ=0,cases=0;
    for(uint32_t seed:{1u,2u,3u,5u,8u,13u}){
      int32_t base[ENG_NPAR]{},zero[ENG_NPAR]{},varied[ENG_NPAR]{};
      amalthea_patch(e,seed,base);amalthea_patch_variant(e,seed,0,zero);
      require(std::memcmp(base,zero,sizeof base)==0,"variation 0 is the patch itself");
      alignas(16) static unsigned char a[ENG_CK_MAX],b[ENG_CK_MAX],c[ENG_CK_MAX];
      std::memset(a,0,sizeof a);std::memset(c,0,sizeof c);E->cook(a,base);E->cook(c,zero);
      auto plain=renderNote(e,a,57,700);
      require(plain==renderNote(e,c,57,700),"variation 0 sounds as the patch itself");
      double rms=0;for(int i=0;i<FftSize;i++)rms+=double(plain[8000+i])*plain[8000+i];rms=std::sqrt(rms/FftSize);
      // The played note's share (A3 = 220 Hz, or its octave) of the 40 Hz .. 1 kHz region.
      auto noteShare=[](const std::vector<double> &m){double best=1e-12,at=0;int lo=int(40.0*FftSize/44100),hi=int(1000.0*FftSize/44100);
        for(int i=lo;i<hi;i++)best=std::max(best,m[i]);
        for(double f:{220.0,440.0}){int c=int(std::lround(f*FftSize/44100));for(int i=c-2;i<=c+2;i++)at=std::max(at,m[i]);}
        return at/best;};
      // Only a patch that follows the note can show a pitch change: it clearly sounds A3 and,
      // played 3 semitones up, no longer does (fixed-frequency or unpitched patches can't tell).
      auto heard=spectrum(plain,8000);
      bool tracks=rms>1e-3&&noteShare(heard)>=.25&&noteShare(spectrum(renderNote(e,a,60,700),8000))<.04;
      // Pitch kept: the strongest partial of `x` is one the patch already has (a changed
      // timbre reweights partials; a changed pitch or tuning moves every one of them).
      auto samePitch=[&](const std::vector<float> &x,int &where){
        auto m=spectrum(x,8000);int lo=int(80.0*FftSize/44100),hi=int(1000.0*FftSize/44100),top=lo;
        for(int i=lo;i<hi;i++)if(m[i]>m[top])top=i;
        double best=0;for(int i=lo;i<hi;i++)best=std::max(best,heard[i]);
        bool found=false;int w=std::max(2,int(top*.012));
        for(int i=std::max(lo,top-w);i<=std::min(hi-1,top+w);i++)found|=heard[i]>=.1*best&&heard[i]>=heard[i-1]&&heard[i]>=heard[i+1];
        where=top;return found;};
      // Control: the patch itself 3 semitones up must fail that check.
      if(tracks){controls++;int w;if(!samePitch(renderNote(e,a,60,700),w))caught++;}
      for(uint32_t v:{0x1234567u,0x9abcdefu,0x31415926u}){
        amalthea_patch_variant(e,seed,v,varied);std::memset(b,0,sizeof b);E->cook(b,varied);cases++;
        auto other=renderNote(e,b,57,700);
        if(other!=plain)differ++;
        double energy=0;for(float x:other){require(std::isfinite(x)&&std::fabs(x)<64,"varied output bounded");energy+=double(x)*x;}
        require(energy>1e-4,"varied voice audible");
        // Pitch kept: every strong partial of the variation is already a partial of the patch
        // (a changed timbre reweights partials; a changed pitch would move all of them).
        // Pitch kept: a patch that clearly sounds the played note still sounds it (a changed
        // timbre reweights partials; a changed pitch or tuning moves them off the note).
        // Pitch kept: the variation's strongest partial is one the patch already has (a changed
        // timbre reweights partials; a changed pitch or tuning moves every one of them).
        if(tracks){periodic++;int top;
          if(!samePitch(other,top)){fprintf(stderr,"%s seed=%u variation=%08x: strongest partial %.0f Hz is new\n",E->name,seed,v,top*44100.0/FftSize);require(false,"a variation keeps the pitch");}}
        compared++;
      }
    }
    require(differ*2>=cases,"variations change most patches");
    printf("%s: %d/%d variations differ\n",E->name,differ,cases);
  }
  printf("pitch compared on %d note-following pairs of %d\n",periodic,compared);
  printf("pitch control: %d of %d transposed patches detected\n",caught,controls);
  require(caught*10>=controls*9,"the pitch check detects a transposition");
  require(periodic>=20,"enough note-following patches to check pitch");
  // --- Audio: varied rows play, deterministically, without changing a plain pattern --------
  for(int e=0;e<SynthEngines;e++){
    auto p=blankProject();for(auto &patch:p.patches)patch={uint8_t(e),7};
    for(int t=0;t<3;t++)for(int r=0;r<16;r++){p.tracks[t].steps[r].note=48+r%5;p.tracks[t].steps[r].instrument=uint8_t(r%2);}
    auto q=p;for(int t=0;t<3;t++)for(int r=0;r<16;r++)q.tracks[t].steps[r].variation=0x1000u+r*31u+t;
    Audio a(48),b(48),c(48);a.submit(p,true);b.submit(q,true);c.submit(q,true);
    std::vector<float> x(44100*2),y(x.size()),z(x.size());a.render(x.data(),44100);b.render(y.data(),44100);c.render(z.data(),44100);
    require(y==z,"varied playback deterministic");
    double ey=0;for(float v:y){require(std::isfinite(v),"finite");ey+=double(v)*v;}
    require(ey>1e-3,"varied pattern audible");
    if(x==y)fprintf(stderr,"%s: identical playback\n",bank[e]->name);
    require(x!=y,"variations are heard");
    // Many resubmissions recycle the pool instead of growing without bound.
    Audio d(48);std::vector<float> buffer(512*2);
    for(int round=0;round<200;round++){auto r=q;for(int t=0;t<3;t++)for(int row=0;row<16;row++)r.tracks[t].steps[row].variation=uint32_t(round*977+row*13+t+1);d.submit(r,true);d.render(buffer.data(),512);}
  }
  printf("variation tests passed\n");
}
