#pragma once
#include <memory>
#include <cstddef>
namespace amalthea {
struct MasterSettings;
// Native CloudSeed-derived insert space. Fixed 44.1kHz, 32-frame wet adapter.
// Construction allocates once; configure/process/clear never allocate.
class LushReverb {
public:
  explicit LushReverb(bool parallel=false);
  ~LushReverb();
  void configure(const MasterSettings &);
  void clear();
  void setLineLimit(int lines);
  void process(float inL,float inR,float &outL,float &outR);
  size_t memoryBytes() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}
