#pragma once
#include <cstddef>
#include <memory>
namespace amalthea {
struct NativeVoice;
struct NativeDeleter {void operator()(NativeVoice*)const;};
using NativeVoicePtr=std::unique_ptr<NativeVoice,NativeDeleter>;
NativeVoicePtr makeNativeVoice();
void bindNativeVoice(void*,NativeVoice*);
void retriggerNativeVoice(void*,int note,int velocity);
size_t nativeVoiceArenaUsed(const NativeVoice*);
}
extern "C" const char *amalthea_native_family(int,unsigned);
