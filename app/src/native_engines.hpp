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
// RINGS and PLAITS (src/mi_rings.cpp, src/mi_plaits.cpp) keep their state in the bound native
// voice's arena. Claim releases whatever the voice held, then takes `bytes` from the start of its
// arena; the retrigger hook replaces retriggerNativeVoice's own handling for that state.
using NativeRetrigger=void(*)(void *state,int note,int velocity);
void *nativeVoiceClaim(void *data,size_t bytes,NativeRetrigger retrigger);
void *nativeVoiceState(void *data);
}
extern "C" const char *amalthea_native_family(int,unsigned);
