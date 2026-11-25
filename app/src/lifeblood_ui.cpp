// LIFEBLOOD screens. The field follows Vascular: breathing circular nodes, swaying
// vessels drawn from the source's colour to the target's, and corpuscles that
// travel along each vessel in the direction of flow.
#include "ui.hpp"
#include "lifeblood.hpp"
#include "fx_rack.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
constexpr uint32_t InColor=0xff7a45,OutColor=0xf1edd4,Key=0xf47b5b,Ink=0x07080b,Dim=0x8d99a8,Ivory=0xf3f1ea,Amber=0xffb35c,Accent=0x3fd9d0;
uint32_t familyColor(FxType t){
 switch(lifebloodFamily(t)){
  case LifebloodFamily::Space:return 0x8fb8ff;case LifebloodFamily::Time:return 0x3fd9d0;case LifebloodFamily::Texture:return 0xc49bff;
  case LifebloodFamily::Color:return 0xffd36c;case LifebloodFamily::Drive:return 0xff6b6b;case LifebloodFamily::Rhythm:return 0xff9de2;default:return 0x7dffa8;
 }
}
uint32_t mixColor(uint32_t a,uint32_t b,float t){
 t=std::clamp(t,0.f,1.f);uint32_t out=0;
 for(int sh=0;sh<24;sh+=8){float x=((a>>sh)&255)*(1-t)+((b>>sh)&255)*t;out|=uint32_t(std::lround(x))<<sh;}return out;
}
void blend(Canvas&c,int x,int y,uint32_t color,float alpha){
 if(x<0||y<0||x>=c.w||y>=c.h||alpha<=0)return;auto&p=c.pixels[y*c.w+x];
 p=0xff000000u|mixColor(p&0xffffff,color,std::min(alpha,1.f));
}
void disc(Canvas&c,float cx,float cy,float r,uint32_t color,float alpha){
 int x0=int(cx-r-1),x1=int(cx+r+1),y0=int(cy-r-1),y1=int(cy+r+1);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){float d=std::hypot(x+.5f-cx,y+.5f-cy);float k=std::clamp(r+.5f-d,0.f,1.f);if(k>0)blend(c,x,y,color,alpha*k);}
}
// A soft halo: alpha falls off linearly from the centre.
void halo(Canvas&c,float cx,float cy,float r,uint32_t color,float alpha){
 int x0=int(cx-r),x1=int(cx+r),y0=int(cy-r),y1=int(cy+r);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){float d=std::hypot(x+.5f-cx,y+.5f-cy);if(d<r)blend(c,x,y,color,alpha*(1-d/r));}
}
void ring(Canvas&c,float cx,float cy,float r,float width,uint32_t color,float alpha){
 float o=r+width/2+1;int x0=int(cx-o),x1=int(cx+o),y0=int(cy-o),y1=int(cy+o);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){float d=std::hypot(x+.5f-cx,y+.5f-cy);float k=std::clamp(width/2+.5f-std::abs(d-r),0.f,1.f);if(k>0)blend(c,x,y,color,alpha*k);}
}
void segment(Canvas&c,float ax,float ay,float bx,float by,float width,uint32_t color,float alpha){
 float h=width/2+1;int x0=int(std::min(ax,bx)-h),x1=int(std::max(ax,bx)+h),y0=int(std::min(ay,by)-h),y1=int(std::max(ay,by)+h);
 float dx=bx-ax,dy=by-ay,len2=std::max(1e-4f,dx*dx+dy*dy);
 for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++){float px=x+.5f-ax,py=y+.5f-ay,t=std::clamp((px*dx+py*dy)/len2,0.f,1.f);
  float d=std::hypot(px-t*dx,py-t*dy),k=std::clamp(width/2+.5f-d,0.f,1.f);if(k>0)blend(c,x,y,color,alpha*k);}
}
// Edge alphas eased toward the network's links, so vessels visibly grow and wither.
struct Shown { std::array<std::array<float,LifebloodVertices>,LifebloodVertices> edge{};float clock=-1;uint32_t generation=0; };
Shown shown;
}
void drawLifebloodMark(Canvas&c,int x,int y,int size,uint32_t accent){
 // Three vessels from a source node through two effect nodes into an outlet.
 float k=size/128.f;auto P=[&](float u,float v){return std::array<float,2>{x+u*k,y+v*k};};
 const std::array<float,2> n[5]={P(16,64),P(58,30),P(62,96),P(98,58),P(116,92)};
 const int e[6][2]={{0,1},{0,2},{1,3},{2,3},{1,2},{3,4}};
 for(auto&l:e)segment(c,n[l[0]][0],n[l[0]][1],n[l[1]][0],n[l[1]][1],5*k,0x55636b,1);
 const uint32_t color[5]={InColor,accent,0x8fb8ff,accent,OutColor};const float radius[5]={13,11,10,12,9};
 for(int i=0;i<5;i++){disc(c,n[i][0],n[i][1],radius[i]*k,color[i],1);disc(c,n[i][0],n[i][1],radius[i]*k*.45f,0x0a0f13,1);}
}
void drawLifebloodUI(Canvas&c,const Editor&e,const AudioStats&audio){
 c.rect(0,0,c.w,c.h,0);float scale=c.w/1024.f;auto X=[&](float v){return int(v*scale);};
 auto box=[&](int x,int y,int w,int h,uint32_t color){c.rect(X(x),X(y),X(w),X(h),color);};
 auto text=[&](int x,int y,int width,const std::string&s,int size,uint32_t color=Ivory){c.textFit(X(x),X(y),X(width),s,X(size),color);};
 auto centred=[&](float cx,int y,const std::string&s,int size,uint32_t color){int w=int(s.size())*X(size)*24/32;c.text(X(cx)-w/2,X(y),s,X(size),color);};
 static const LifebloodSession blank{};const auto&s=e.lifeblood?*e.lifeblood:blank;const auto&n=s.network;
 drawLifebloodMark(c,X(24),X(14),X(76),Accent);text(120,25,600,"AMALTHEA / LIFEBLOOD",38);
 drawBattery(c,X(868),X(32),X(24),e.battery,e.charging,Dim);
 int state=audio.usbState.load(),channels=audio.usbChannels.load();
 std::string input=state==0?(audio.usbPort==1?"USB: COMPUTER LINK":audio.usbPort==2?"USB: STARTING HOST":audio.usbPort==3?"USB HOST: WAITING":audio.usbPort==4?"USB: PORT ERROR":"USB: WAITING"):state==1?std::string(fm1Name(audio.usbFirmware))+" / "+std::to_string(channels)+" TRACKS":state==4?std::string(fm1Name(audio.usbFirmware))+" / STEREO":state==2?"USB: FORMAT UNSUPPORTED":state==5?"USB: NO AUDIO INPUT":"USB: OPEN FAILED";
 text(24,114,470,input,30,Dim);
 std::string status;uint32_t statusColor=Dim;
 if(e.lbBypass){status="BYPASS";statusColor=Amber;}
 else if(s.angiogenesis.active){status="ANGIOGENESIS";statusColor=Key;}
 else if(n.count()){status=std::string(lifebloodCharacterName(n.character))+" / "+std::to_string(n.count())+" NODES";}
 else status="IN > OUT / DRY";
 if(s.activeSnapshot>=0)status+="  S"+std::to_string(s.activeSnapshot+1);
 text(510,114,490,status,30,statusColor);box(24,169,976,2,0x1f2833);
 if(e.page==Page::LifebloodSnapshots||e.page==Page::LifebloodSessions){
  bool snapshots=e.page==Page::LifebloodSnapshots;int count=snapshots?LifebloodSnapshots:LifebloodSessionSlots;
  text(24,187,976,snapshots?"SNAPSHOTS":"LIFEBLOOD SESSIONS",38,Accent);
  int rowHeight=snapshots?110:58,cell=snapshots?100:49;
  for(int i=0;i<count;i++){int x=24+(i%8)*122,y=252+(i/8)*rowHeight;
   bool selected=(snapshots?e.lbSnapshotSlot:e.lbSessionSlot)==i,active=snapshots?s.activeSnapshot==i:e.lbActiveSession==i;
   bool filled=snapshots?bool(s.stored[i]):e.lbSessionState[i]==1,damaged=!snapshots&&e.lbSessionState[i]==2;
   box(x,y,112,cell,active?0x73b5ff:selected?Accent:0x233040);box(x+3,y+3,106,cell-6,selected?0x123634:filled?0x18222e:0x0b1018);
   text(x+12,y+7,90,std::to_string(i+1)+(damaged?" !":filled?" +":""),29,filled?Ivory:Dim);
   if(active)box(x+91,y+7,10,10,0x73b5ff);
   if(snapshots&&filled){const auto&k=s.snapshots[i];text(x+12,y+52,90,lifebloodCharacterName(k.character),20,Dim);}
  }
  text(24,snapshots?700:722,976,snapshots?"TAP: LOAD   HOLD: STORE   ENTER: SETTINGS":"TAP: LOAD   HOLD: SAVE   ENTER: SETTINGS",24,Dim);
  return;
 }
 if(e.page==Page::LifebloodSettings){
  text(24,187,976,"SESSION SETTINGS",38,Accent);
  const char*names[]={"TEMPO","PATH","CHARACTER","NODES","GROWTH","MIX"};
  int rate=int(std::lround(6+114*s.growth/100.f));
  std::string values[]={(audio.usbClock&&audio.usbBpm>0?"EXT "+std::to_string(int(audio.usbBpm.load()+.5f))+" BPM":std::to_string(s.tempo)+" BPM"),lifebloodPathName(s.path),lifebloodCharacterName(s.character),"UP TO "+std::to_string(s.maxNodes),
   std::to_string(s.growth)+"% / "+std::to_string(rate)+" PER MIN",std::to_string(s.mix)+"% WET"};
  for(int f=0;f<LifebloodSettingsFields;f++){int y=250+f*78;bool selected=e.lbSettingsField==f;if(selected)box(24,y-8,976,70,Accent);
   text(48,y,400,names[f],40,selected?Ink:Dim);text(430,y,546,values[f],40,selected?Ink:Ivory);}
  return;
 }
 if(e.page==Page::LifebloodNode&&lifebloodNodeVertex(e.lbVertex)&&n.live(e.lbVertex)){
  const auto&k=n.node[e.lbVertex-2];
  text(24,187,976,"NODE "+std::to_string(k.rank)+" / "+fxName(k.effect.type),34,familyColor(k.effect.type));
  bool paged=k.effect.type==FxType::ChowMatrix||k.effect.type==FxType::Frequalizer||k.effect.type==FxType::Biztortion;
  text(24,235,976,std::string("PARAMETERS")+(paged?" / PAGE "+std::to_string(e.lbPage+1):"")+"   Y: REDRAW   X: NEW EFFECT",26,Dim);
  int count=1+insertParameterCount(k.effect.type,e.lbPage,k.settings),base=e.lbParameter/6*6;
  for(int f=base;f<std::min(base+6,count);f++){
   int y=295+(f-base)*72;bool selected=f==e.lbParameter;uint32_t color=selected?Ink:Ivory;if(selected)box(24,y-5,976,65,familyColor(k.effect.type));
   std::string name,value;
   if(f==0){name="EFFECT";value=fxName(k.effect.type);}
   else{name=insertParameterName(k.effect.type,f-1,e.lbPage,k.settings);Effect effect=k.effect;auto settings=k.settings;
    value=insertParameterDescription(k.effect.type,f-1,e.lbPage,k.settings,insertParameter(effect,settings,f-1,e.lbPage));}
   text(40,y+6,500,name,40,color);text(560,y+6,420,value,40,color);
  }
  return;
 }
 // The field.
 const float left=24,top=178,width=976,height=520;
 for(int y=0;y<int(height);y+=4)box(int(left),int(top)+y,int(width),4,mixColor(0x07080b,0x10131a,y/height));
 auto px=[&](int v){return left+n.x(v)*width;};auto py=[&](int v){return top+n.y(v)*height;};
 float t=e.lbClock,dt=shown.clock<0?1.f:std::clamp(t-shown.clock,0.f,.5f);shown.clock=t;
 if(shown.generation!=n.generation){shown.generation=n.generation;for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++)shown.edge[a][b]=n.link[a][b]*.01f;}
 auto colorOf=[&](int v){return lifebloodInput(v)?InColor:v==LifebloodOut?OutColor:familyColor(n.node[v-2].effect.type);};
 auto radiusOf=[&](int v){return float(X(v>=LifebloodTrackBase?24:v<2?36:28));};
 // Vessels.
 for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++){
  float target=n.live(a)&&n.live(b)?n.link[a][b]*.01f:0.f,&g=shown.edge[a][b];
  g+=(target-g)*std::min(1.f,dt*5);if(g<.01f&&target<=0){g=0;continue;}
  float ax=X(px(a)),ay=X(py(a)),bx=X(px(b)),by=X(py(b)),dx=bx-ax,dy=by-ay,len=std::hypot(dx,dy);if(len<1)continue;
  float phase=(a*7+b*13)*.37f,sway=std::sin(.55f*t+phase)*(len*.035f+4*scale);
  float cx=(ax+bx)/2-dy/len*sway,cy=(ay+by)/2+dx/len*sway;
  int pieces=std::clamp(int(len/(11*scale)),6,40);float lx=ax,ly=ay;
  uint32_t ca=colorOf(a),cb=colorOf(b);
  for(int i=1;i<=pieces;i++){float u=float(i)/pieces,x=(1-u)*(1-u)*ax+2*(1-u)*u*cx+u*u*bx,y=(1-u)*(1-u)*ay+2*(1-u)*u*cy+u*u*by;
   uint32_t col=mixColor(ca,cb,u);segment(c,lx,ly,x,y,7*scale,col,.16f*g);segment(c,lx,ly,x,y,(1.5f+2.5f*g)*scale,col,.92f*g);lx=x;ly=y;}
  // Corpuscles carry the flow from a to b.
  for(int i=0;i<3;i++){float u=std::fmod(t*.32f+i/3.f+phase*.1f,1.f),x=(1-u)*(1-u)*ax+2*(1-u)*u*cx+u*u*bx,y=(1-u)*(1-u)*ay+2*(1-u)*u*cy+u*u*by;
   disc(c,x,y,3.2f*scale,Ivory,.85f*g*std::sin(3.14159265f*u));}
 }
 // Nodes, then labels on top.
 for(int v=0;v<LifebloodVertices;v++){
  if(!n.live(v))continue;bool linked=v<2||v>=LifebloodTrackBase;for(int u=0;u<LifebloodVertices;u++)linked|=n.link[u][v]||n.link[v][u];
  float level=std::min(1.f,audio.lifebloodLevels[v].load()*1.6f),breath=1+.06f*std::sin(1.35f*t+v*1.7f);
  float r=radiusOf(v)*breath,x=X(px(v)),y=X(py(v));uint32_t col=colorOf(v);float alpha=linked?1:.35f;
  if(v>=LifebloodTrackBase&&!(n.heard>>(v-LifebloodTrackBase)&1))alpha=.35f;   // a silent track, dimmed
  halo(c,x,y,r*2.2f,col,(.12f+.3f*level)*alpha);
  disc(c,x,y,r,mixColor(0x0b0e14,col,.18f+.25f*level),alpha);
  ring(c,x,y,r,3*scale,col,alpha);
  if(v==LifebloodOut)ring(c,x,y,r+7*scale,2*scale,col,.35f);
  if(v==e.lbVertex)ring(c,x,y,r+12*scale,3*scale,0xffffff,1);
 }
 for(int v=0;v<LifebloodVertices;v++){
  if(!n.live(v))continue;bool selected=v==e.lbVertex;
  std::string label=v>=LifebloodTrackBase?std::to_string(v-LifebloodTrackBase+1):v==LifebloodIn?"IN":v==LifebloodOut?"OUT":lifebloodLabel(n.node[v-2].effect.type);
  float r=radiusOf(v)/scale;int size=selected?30:22;
  if(!lifebloodNodeVertex(v))centred(px(v),int(py(v))-size*44/64,label,size,colorOf(v));
  else centred(px(v),int(py(v)+r+12),label,size,selected?0xffffff:Dim);
 }
 // The selected vertex, in full.
 std::string name,detail;
 if(e.lbVertex==LifebloodIn){name="IN";detail=e.lbBypass?"BYPASS: PASSING DRY":"FM1 STEREO";}
 else if(e.lbVertex>=LifebloodTrackBase){int t=e.lbVertex-LifebloodTrackBase;name="TRACK "+std::to_string(t+1);detail=e.lbBypass?"BYPASS: PASSING DRY":(n.heard>>t&1)?"HEARD":"SILENT";}
 else if(e.lbVertex==LifebloodOut){name="OUT";detail=std::to_string(s.mix)+"% WET";}
 else if(n.live(e.lbVertex)){const auto&k=n.node[e.lbVertex-2];name=fxName(k.effect.type);detail="MIX "+std::to_string(k.effect.value)+"%";}
 text(24,712,560,name,34,lifebloodNodeVertex(e.lbVertex)&&n.live(e.lbVertex)?colorOf(e.lbVertex):Ivory);text(600,716,400,detail,28,Dim);
}
}
