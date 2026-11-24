#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
namespace amalthea {
struct AudioHealthRecord {
 double seconds=0;const char*event="health";int status=0;
 uint64_t frames=0,captureXruns=0,starvation=0,overflow=0,outputXruns=0,maxGapUs=0,maxRenderUs=0;
 int fill=0,clock=0,running=0;
};
// Single capture-thread writer. Fixed memory; no file I/O while collecting.
template<size_t Capacity=1024> class AudioHealthHistory {
 static_assert(Capacity>0,"Nonempty trace required");
 std::array<AudioHealthRecord,Capacity> records{};size_t next=0,count=0;
public:
 void clear(){next=count=0;}
 bool empty()const{return count==0;}
 void push(const AudioHealthRecord&record){records[next]=record;next=(next+1)%Capacity;if(count<Capacity)count++;}
 bool write(FILE*file)const{
  if(!file)return false;
  if(fputs("seconds,event,status,capture_frames,capture_xruns,input_starvation,ring_overflow,output_xruns,max_capture_gap_us,max_fx_render_us,fill_frames,midi_clock,transport_running\n",file)<0)return false;
  size_t start=(next+Capacity-count)%Capacity;
  for(size_t i=0;i<count;i++){
   auto&r=records[(start+i)%Capacity];
   if(fprintf(file,"%.9f,%s,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d,%d,%d\n",r.seconds,r.event,r.status,
    (unsigned long long)r.frames,(unsigned long long)r.captureXruns,(unsigned long long)r.starvation,
    (unsigned long long)r.overflow,(unsigned long long)r.outputXruns,(unsigned long long)r.maxGapUs,
    (unsigned long long)r.maxRenderUs,r.fill,r.clock,r.running)<0)return false;
  }
  return !ferror(file);
 }
};
}
