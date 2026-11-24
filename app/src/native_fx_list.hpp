// SPDX-License-Identifier: GPL-3.0-only
// Every native effect: its description and factory (one file each under src/native/).
#pragma once
#include "native_fx.hpp"
namespace amalthea {
// Batch 1 (build 31): stutter, repeat, slicing, shuffling, gates.
extern const NativeInfo oopsInfo;std::unique_ptr<NativeFx> makeOops();
extern const NativeInfo bufferOverrideInfo;std::unique_ptr<NativeFx> makeBufferOverride();
extern const NativeInfo livecutInfo;std::unique_ptr<NativeFx> makeLivecut();
extern const NativeInfo supaTriggaInfo;std::unique_ptr<NativeFx> makeSupaTrigga();
extern const NativeInfo beatRepeatInfo;std::unique_ptr<NativeFx> makeBeatRepeat();
extern const NativeInfo timeShaperInfo;std::unique_ptr<NativeFx> makeTimeShaper();
extern const NativeInfo jumblrInfo;std::unique_ptr<NativeFx> makeJumblr();
extern const NativeInfo scrubbyInfo;std::unique_ptr<NativeFx> makeScrubby();
extern const NativeInfo glitchletsInfo;std::unique_ptr<NativeFx> makeGlitchlets();
extern const NativeInfo skidderInfo;std::unique_ptr<NativeFx> makeSkidder();
// Batch 2 (build 32): gates, loopers, buffer bending, destruction.
extern const NativeInfo chopprInfo;std::unique_ptr<NativeFx> makeChoppr();
extern const NativeInfo gateShaperInfo;std::unique_ptr<NativeFx> makeGateShaper();
extern const NativeInfo tremoSquareInfo;std::unique_ptr<NativeFx> makeTremoSquare();
extern const NativeInfo beadsInfo;std::unique_ptr<NativeFx> makeBeads();
extern const NativeInfo softcutInfo;std::unique_ptr<NativeFx> makeSoftcut();
extern const NativeInfo transverbInfo;std::unique_ptr<NativeFx> makeTransverb();
extern const NativeInfo glitchShifterInfo;std::unique_ptr<NativeFx> makeGlitchShifter();
extern const NativeInfo pitchNastyInfo;std::unique_ptr<NativeFx> makePitchNasty();
extern const NativeInfo amalgamInfo;std::unique_ptr<NativeFx> makeAmalgam();
extern const NativeInfo geometerInfo;std::unique_ptr<NativeFx> makeGeometer();
extern const NativeInfo multiBandDistortionInfo;std::unique_ptr<NativeFx> makeMultiBandDistortion();
extern const NativeInfo disintegrateInfo;std::unique_ptr<NativeFx> makeDisintegrate();
extern const NativeInfo bitGlitterInfo;std::unique_ptr<NativeFx> makeBitGlitter();
extern const NativeInfo pafnutyInfo;std::unique_ptr<NativeFx> makePafnuty();
// Batch 3 (build 33): waveshapers, cross-modulation, spectral, resonators, vocoders.
extern const NativeInfo cyanideInfo;std::unique_ptr<NativeFx> makeCyanide();
extern const NativeInfo polarizerInfo;std::unique_ptr<NativeFx> makePolarizer();
extern const NativeInfo angrInfo;std::unique_ptr<NativeFx> makeAngr();
extern const NativeInfo warpsInfo;std::unique_ptr<NativeFx> makeWarps();
extern const NativeInfo freqShifterInfo;std::unique_ptr<NativeFx> makeFreqShifter();
extern const NativeInfo brokenFftInfo;std::unique_ptr<NativeFx> makeBrokenFft();
extern const NativeInfo treemonsterInfo;std::unique_ptr<NativeFx> makeTreemonster();
extern const NativeInfo ringModInfo;std::unique_ptr<NativeFx> makeRingMod();
extern const NativeInfo combulatorInfo;std::unique_ptr<NativeFx> makeCombulator();
extern const NativeInfo resonatorInfo;std::unique_ptr<NativeFx> makeResonator();
extern const NativeInfo vocoderInfo;std::unique_ptr<NativeFx> makeVocoder();
extern const NativeInfo smrInfo;std::unique_ptr<NativeFx> makeSmr();
extern const NativeInfo rezSynthInfo;std::unique_ptr<NativeFx> makeRezSynth();
extern const NativeInfo talkBoxInfo;std::unique_ptr<NativeFx> makeTalkBox();
extern const NativeInfo mdaVocoderInfo;std::unique_ptr<NativeFx> makeMdaVocoder();
extern const NativeInfo rePsychoInfo;std::unique_ptr<NativeFx> makeRePsycho();
}
