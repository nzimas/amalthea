#pragma once
#include "audio_health.hpp"
#include <atomic>
#include <memory>
#include <string>
namespace amalthea {
// One producer (capture thread), one low-priority disk writer. Never waits for disk.
class PersistentAudioLog {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 explicit PersistentAudioLog(const std::string& directory);
 ~PersistentAudioLog();
 bool push(const AudioHealthRecord&,const char* detail="");
 uint64_t dropped()const;
};
}
