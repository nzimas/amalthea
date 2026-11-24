// SPDX-License-Identifier: GPL-3.0-only
// Robert Heel, Noisferatu; Amalthea instance isolation and host adapter.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include "sample.h"
#define SAMPLE_RATE 16000
#define TOGGLE_SAMPLES (SAMPLE_RATE * 3)
struct Noisferatu {
 static constexpr float PI=3.14159265358979323846f;
 static uint8_t pgm_read_byte(const uint8_t *p){return *p;}
 float pot1_norm=0.5f,pot2_norm=0.5f;
 template<class A,class B> static auto min(A a,B b)->typename std::common_type<A,B>::type{return a<b?a:b;}
 template<class A,class B> static auto max(A a,B b)->typename std::common_type<A,B>::type{return a>b?a:b;}
 template<class A,class B,class C> static A constrain(A x,B lo,C hi){return x<lo?lo:x>hi?hi:x;}
 long random(long lo,long hi){noise1();return lo+rng%static_cast<uint32_t>(hi-lo);}
uint32_t FMnoise_lastClock1State = 0;
uint32_t FMnoise_lastClock2State = 0;
float generativeWaveform1_fractionalPhase1 = 0.0f;
float generativeWaveform2_fractionalPhase2 = 0.0f;
float generativeWaveform3_fractionalPhase3 = 0.0f;
float generativeWaveform4_fractionalPhase = 0.0f;
float generativeWaveform5_fractionalPhase = 0.0f;
float generativeWaveform6_fractionalPhase = 0.0f;
float generativeWaveform7_fractionalPhase = 0.0f;
float generativeWaveform17_fractionalPhase = 0.0f;
uint32_t generativeWaveform18_lastBitClockState = 0;
float generativeWaveform18_fractionalPhase = 0.0f;
uint32_t generativeWaveform8_lastBitClockState = 0;
float generativeWaveform8_fractionalPhase = 0.0f;
uint32_t generativeWaveform9_lastBitClockState = 0;
float generativeWaveform9_fractionalPhase = 0.0f;
uint32_t generativeWaveform10_lastBitClockState = 0;
float generativeWaveform10_fractionalPhase = 0.0f;
uint32_t generativeWaveform11_lastBitClockState = 0;
float generativeWaveform11_fractionalPhase = 0.0f;
uint32_t generativeWaveform12_lastBitClockState = 0;
float generativeWaveform12_fractionalPhase = 0.0f;
uint32_t generativeWaveform13_lastBitClockState = 0;
float generativeWaveform13_fractionalPhase = 0.0f;
uint32_t generativeWaveform14_lastBitClockState = 0;
float generativeWaveform14_fractionalPhase = 0.0f;
uint32_t generativeWaveform15_lastBitClock1State = 0;
uint32_t generativeWaveform15_lastBitClock2State = 0;
float generativeWaveform15_fractionalPhase = 0.0f;
uint32_t generativeWaveform16_lastBitClockState = 0;
float generativeWaveform16_fractionalPhase = 0.0f;
uint32_t highNoiseRhythm_lastClockState = 0;
uint8_t highNoiseRhythm_dividerCounter = 0;
uint32_t majorTris_lfo3SlowPhase = 0;
uint32_t bernoulliTris_lastClockState = 0;
uint32_t pentatonicBlips_lastClockState = 0;
#ifndef ALGOS_H
#define ALGOS_H




// --------------------------------------------------
// PRNG State (shared across algorithms)
// --------------------------------------------------
uint32_t rng = 0x12345678;

// --------------------------------------------------
// Shared Noise Functions
// --------------------------------------------------
inline int16_t noise1()
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (int16_t)(rng & 0x3FF) - 512; // signed 10-bit
}

inline uint16_t rand12()
{
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng & 0x0FFF; // 0–4095
}

// --------------------------------------------------
// Global Triangle Oscillator (reusable building block)
// --------------------------------------------------
// Simple triangle wave generator - can be called from any algorithm
// Input: phase (0.0 to 1.0), Output: -512 to +511 (10-bit signed)
inline int16_t triangle(float phase)
{
  // Convert phase to triangle: 0->1->0, then scale to -1 to +1
  float tri = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
  tri = tri * 2.0f - 1.0f;  // -1 to +1

  return (int16_t)(tri * 511.0f);  // -512 to +511
}

// --------------------------------------------------
// Global Square Wave Oscillator (reusable building block)
// --------------------------------------------------
// Simple square wave generator - can be called from any algorithm
// Input: phase (0.0 to 1.0), Output: -512 or +511 (10-bit signed)
inline int16_t square(float phase)
{
  // Square: high when phase < 0.5, low when phase >= 0.5
  return (phase < 0.5f) ? 511 : -512;
}

// --------------------------------------------------
// Global Square Wave with PWM (reusable building block)
// --------------------------------------------------
// Square wave with variable pulse width
// Input: phase (0.0 to 1.0), duty (0.0 to 1.0), Output: -512 or +511
inline int16_t squarePWM(float phase, float duty)
{
  // High when phase < duty, low otherwise
  return (phase < duty) ? 511 : -512;
}

// --------------------------------------------------
// Algorithm: Latched Noise
// --------------------------------------------------
uint32_t holdCounter = 0;
uint32_t holdPeriodSamples = SAMPLE_RATE;
int16_t latchedValue = 0;
uint16_t latchProbThreshold = 4095;

inline int16_t latchedNoise1()
{
  holdCounter++;

  if (holdCounter >= holdPeriodSamples) {
    holdCounter = 0;

    uint16_t r = rand12();

    if (r < latchProbThreshold) {
      latchedValue = noise1();
    }
    // else: hold previous value
  }

  return latchedValue;
}

// --------------------------------------------------
// Algorithm: Dust
// --------------------------------------------------
float dustProb = 0.0f;
float dustFilterAlpha = 1.0f;     // Filter coefficient (pot1 controlled)
float dustFilterState = 0.0f;     // Filter state

inline int16_t dustNoise()
{
  // uniform random 0..1
  float r = random(0, 65536) * (1.0f / 65536.0f);

  int16_t rawClick;
  if (r < dustProb) {
    rawClick = noise1();   // click
  } else {
    rawClick = 0;          // silence
  }

  // 1-pole lowpass filter
  // output = output + alpha * (input - output)
  dustFilterState += dustFilterAlpha * ((float)rawClick - dustFilterState);

  return (int16_t)dustFilterState;
}

// --------------------------------------------------
// Algorithm: FMnoise (rewritten - inharmonic dual oscillators)
// two clocks at inharmonic ratio, trigger on coincidence
// --------------------------------------------------
uint32_t fmPhase1 = 0;
uint32_t fmPhase2 = 0;
uint32_t fmPhaseInc1 = 0;   // pot1: base frequency osc1
uint32_t fmPhaseInc2 = 0;   // pot1: base frequency osc2 (inharmonic ratio)

// Coincidence clock state
uint32_t fmClock1Phase = 0;
uint32_t fmClock2Phase = 0;
uint32_t fmClockPhaseInc = 0;   // pot2: clock speed
uint32_t fmClockPhaseInc2 = 0;  // move here from params.h
uint8_t fmCurrentRatioIdx = 0;  // current ratio index

inline int16_t FMnoise()
{
  // Advance audio oscillators
  fmPhase1 += fmPhaseInc1;
  fmPhase2 += fmPhaseInc2;

  // Advance coincidence clocks



  fmClock1Phase += fmClockPhaseInc;
  fmClock2Phase += fmClockPhaseInc2;  // set in params: inc * FM_CLOCK_RATIO

  uint32_t clock1State = fmClock1Phase & 0x80000000;
  uint32_t clock2State = fmClock2Phase & 0x80000000;

// Trigger when clock1 rises AND clock2 is currently high
  bool clock1Rise = clock1State && !FMnoise_lastClock1State;
  bool clock2High = clock2State;

  if (clock1Rise && clock2High) {
    // Pick random ratio from table
    fmCurrentRatioIdx = rand12() % 8;
  }

  FMnoise_lastClock1State = clock1State;
  FMnoise_lastClock2State = clock2State;

  // Extract triangles
  uint16_t p1 = fmPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));

  uint16_t p2 = fmPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));

  // XOR for inharmonic metallic chaos
  return tri1 ^ tri2;
}

// --------------------------------------------------
// Algorithm: Noisegates
// --------------------------------------------------
uint32_t ng1Phase = 0;
uint32_t ng2Phase = 0;
uint32_t ng1PhaseInc = 0;      // pot1 controlled
uint32_t ng2BasePhaseInc = 0;  // pot2 controlled
int32_t ng2RandomWalk = 0;     // Integer random walk offset
int32_t randomWalkStep = 0;    // Step size for random walk
uint32_t walkUpdateCounter = 0;
uint32_t walkUpdatePeriod = SAMPLE_RATE / 8;

inline int16_t noisegates()
{
    // --- Update random walk at regular intervals ---
    walkUpdateCounter++;
    if (walkUpdateCounter >= walkUpdatePeriod) {
        walkUpdateCounter = 0;

        // Random direction: +/- step
        int32_t direction = (rand12() & 1) ? 1 : -1;
        ng2RandomWalk += direction * randomWalkStep;

        // Clamp random walk (prevent extreme deviations)
        int32_t maxWalk = randomWalkStep * 500;  // Max deviation
        if (ng2RandomWalk < -maxWalk) ng2RandomWalk = -maxWalk;
        if (ng2RandomWalk > maxWalk) ng2RandomWalk = maxWalk;
    }

    // --- Triangle 1 (regular) ---
    ng1Phase += ng1PhaseInc;

    // Extract triangle from phase (top 10 bits)
    uint16_t phase1_10bit = ng1Phase >> 22;
    int16_t tri1;
    if (phase1_10bit < 512) {
        tri1 = (phase1_10bit << 1) - 512;
    } else {
        tri1 = 1535 - (phase1_10bit << 1);
    }

    // Gate 1: open when triangle > ~486 (out of 511, ~95%)
    bool gate1 = (tri1 > 486);

    // --- Triangle 2 (with random walk) ---
    // Apply random walk to phase increment
    int32_t ng2PhaseInc = (int32_t)ng2BasePhaseInc + ng2RandomWalk;
    if (ng2PhaseInc < 0) ng2PhaseInc = 0;  // Ensure positive

    ng2Phase += (uint32_t)ng2PhaseInc;

    // Extract triangle from phase
    uint16_t phase2_10bit = ng2Phase >> 22;
    int16_t tri2;
    if (phase2_10bit < 512) {
        tri2 = (phase2_10bit << 1) - 512;
    } else {
        tri2 = 1535 - (phase2_10bit << 1);
    }

    // Gate 2: open when triangle > ~486
    bool gate2 = (tri2 > 486);

    // --- Generate gated noise ---
    int16_t n = noise1();

    int16_t gated1 = gate1 ? n : 0;
    int16_t gated2 = gate2 ? n : 0;

    // Mix gated outputs
    return (gated1 + gated2) >> 1;  // Divide by 2
}

// --------------------------------------------------
// Algorithm: Generative Waveform 1
// --------------------------------------------------
// Algorithm: Generative Waveforms (3 variants, shared buffer)
// --------------------------------------------------
#define WAVEFORM_SIZE 4000  // Shared buffer size

// SHARED BUFFER - all 3 algorithms write to this
int16_t waveformBuffer[WAVEFORM_SIZE];
uint16_t playbackPhase = 0;
float playbackSpeed = 1.0f;

// Silence chunk state for GW1
float gw1SilenceProb = 0.0f;
uint16_t gw1SilenceChunkRemaining = 0;

// ============================================
// VARIANT 1: Sparse Glitchy
// ============================================
#define GW1_CHUNK_SIZE_1 32
#define GW1_CHUNK_SIZE_2 8
#define GW1_CHUNK_SIZE_3 64
#define GW1_NOISE_PROB 19
#define GW1_BLIP_SIZE 250
#define GW1_BLIP_FREQ_MIN 1000.0f
#define GW1_BLIP_FREQ_MAX 1200.0f

void generateWaveform1()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW1_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW1_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW1_CHUNK_SIZE_3; break;
      default: chunkSize = GW1_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW1_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW1_BLIP_SIZE > 0 && GW1_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW1_BLIP_SIZE);
    float blipFreq = GW1_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW1_BLIP_FREQ_MAX - GW1_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW1_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

// ============================================
// VARIANT 2: Dense Microglitch
// ============================================
#define GW2_CHUNK_SIZE_1 5
#define GW2_CHUNK_SIZE_2 2
#define GW2_CHUNK_SIZE_3 21
#define GW2_NOISE_PROB 3
#define GW2_BLIP_SIZE 77
#define GW2_BLIP_FREQ_MIN  400.0f
#define GW2_BLIP_FREQ_MAX 2503.0f

float gw2BlipFreq = 2100.0f;
bool gw2BlipFreqSet = false;


void generateWaveform2()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW2_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW2_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW2_CHUNK_SIZE_3; break;
      default: chunkSize = GW2_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW2_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 1) crushedNoise = noise1();  // More grainy!
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Shorter, higher-pitched blip
  if (GW2_BLIP_SIZE > 0 && GW2_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW2_BLIP_SIZE);
  if (!gw2BlipFreqSet) {
        gw2BlipFreq = GW2_BLIP_FREQ_MIN +
                      ((float)(rand12() % 1000) / 1000.0f) *
                      (GW2_BLIP_FREQ_MAX - GW2_BLIP_FREQ_MIN);
      gw2BlipFreqSet = true;
    }
    float blipFreq = gw2BlipFreq;
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW2_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

// ============================================
// VARIANT 3: Spacey Pulses
// ============================================
#define GW3_CHUNK_SIZE_1 0
#define GW3_CHUNK_SIZE_2 33
#define GW3_CHUNK_SIZE_3 1
#define GW3_NOISE_PROB 4
#define GW3_BLIP_SIZE 0  // NO BLIP - pure pulses
#define GW3_BLIP_FREQ_MIN 100.0f
#define GW3_BLIP_FREQ_MAX 500.0f

// Separate silence chunk sizes for GW3 (since noise chunks have 0s)
#define GW3_SILENCE_CHUNK_1 64
#define GW3_SILENCE_CHUNK_2 128
#define GW3_SILENCE_CHUNK_3 256

uint8_t gw3PlayCount = 0;
bool gw3NeedsRegen = false;
#define GW3_REGEN_COUNT 5  // ADJUST: 4=western, 5=buchla

void generateWaveform3()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW3_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW3_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW3_CHUNK_SIZE_3; break;
      default: chunkSize = GW3_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW3_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 2) crushedNoise = noise1();  // Less grainy - smoother pulses
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // No blip for variant 3 (GW3_BLIP_SIZE = 0)
}

// Silence chunk state for GW2
float gw2SilenceProb = 0.0f;
uint16_t gw2SilenceChunkRemaining = 0;

// Silence chunk state for GW3
float gw3SilenceProb = 0.0f;
uint16_t gw3SilenceChunkRemaining = 0;

// ============================================
// PLAYBACK with chunked silence (separate for each variant)
// ============================================

inline int16_t generativeWaveform1()
{
  // Chunked silence injection for GW1
  if (gw1SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as GW1)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW1_CHUNK_SIZE_1; break;  // 32
      case 1: chunkSize = GW1_CHUNK_SIZE_2; break;  // 8
      case 2: chunkSize = GW1_CHUNK_SIZE_3; break;  // 64
      default: chunkSize = GW1_CHUNK_SIZE_1; break;
    }

    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw1SilenceProb);

    if (isSilentChunk) {
      gw1SilenceChunkRemaining = chunkSize;
    }
  }

  if (gw1SilenceChunkRemaining > 0) {
    gw1SilenceChunkRemaining--;
    // Still advance playback phase even during silence

    generativeWaveform1_fractionalPhase1 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)generativeWaveform1_fractionalPhase1;
    generativeWaveform1_fractionalPhase1 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }

  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];

  generativeWaveform1_fractionalPhase1 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)generativeWaveform1_fractionalPhase1;
  generativeWaveform1_fractionalPhase1 -= phaseIncrement;
  playbackPhase += phaseIncrement;
  while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
  return sample;
}

inline int16_t generativeWaveform2()
{
  // Chunked silence injection for GW2
  if (gw2SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as GW2)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW2_CHUNK_SIZE_1; break;  // 8
      case 1: chunkSize = GW2_CHUNK_SIZE_2; break;  // 2
      case 2: chunkSize = GW2_CHUNK_SIZE_3; break;  // 16
      default: chunkSize = GW2_CHUNK_SIZE_1; break;
    }

    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw2SilenceProb);

    if (isSilentChunk) {
      gw2SilenceChunkRemaining = chunkSize;
    }
  }

  if (gw2SilenceChunkRemaining > 0) {
    gw2SilenceChunkRemaining--;

    generativeWaveform2_fractionalPhase2 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)generativeWaveform2_fractionalPhase2;
    generativeWaveform2_fractionalPhase2 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }

  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];

  generativeWaveform2_fractionalPhase2 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)generativeWaveform2_fractionalPhase2;
  generativeWaveform2_fractionalPhase2 -= phaseIncrement;
  playbackPhase += phaseIncrement;
  while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
  return sample;
}

inline int16_t generativeWaveform3()
{
  // Chunked silence injection for GW3
  if (gw3SilenceChunkRemaining == 0) {
    // Choose random chunk size from SEPARATE silence sizes (not noise chunks)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW3_SILENCE_CHUNK_1; break;  // 64
      case 1: chunkSize = GW3_SILENCE_CHUNK_2; break;  // 128
      case 2: chunkSize = GW3_SILENCE_CHUNK_3; break;  // 256
      default: chunkSize = GW3_SILENCE_CHUNK_1; break;
    }

    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw3SilenceProb);

    if (isSilentChunk) {
      gw3SilenceChunkRemaining = chunkSize;
    }
  }

  if (gw3SilenceChunkRemaining > 0) {
    gw3SilenceChunkRemaining--;

    generativeWaveform3_fractionalPhase3 += playbackSpeed;
    uint16_t phaseIncrement = (uint16_t)generativeWaveform3_fractionalPhase3;
    generativeWaveform3_fractionalPhase3 -= phaseIncrement;
    playbackPhase += phaseIncrement;
    while (playbackPhase >= WAVEFORM_SIZE) playbackPhase -= WAVEFORM_SIZE;
    return 0;  // Silent
  }

  // Normal playback
  int16_t sample = waveformBuffer[playbackPhase];

  generativeWaveform3_fractionalPhase3 += playbackSpeed;
  uint16_t phaseIncrement = (uint16_t)generativeWaveform3_fractionalPhase3;
  generativeWaveform3_fractionalPhase3 -= phaseIncrement;
playbackPhase += phaseIncrement;
if (playbackPhase >= WAVEFORM_SIZE) {
  playbackPhase -= WAVEFORM_SIZE;
  gw3PlayCount++;
  if (gw3PlayCount >= GW3_REGEN_COUNT) {
    gw3PlayCount = 0;
    gw3NeedsRegen = true;
  }
}
return sample;
}

// ============================================
// VARIANT 4: Random Jump Glitch
// ============================================
#define GW4_CHUNK_SIZE_1 16
#define GW4_CHUNK_SIZE_2 4
#define GW4_CHUNK_SIZE_3 32
#define GW4_NOISE_PROB 25
#define GW4_BLIP_SIZE 30
#define GW4_BLIP_FREQ_MIN 500.0f
#define GW4_BLIP_FREQ_MAX 3000.0f

float gw4PlaybackSpeed = 1.0f;      // pot1
float gw4SilenceProb = 0.0f;        // pot2 controls this (0.0 to 1.0)
uint32_t gw4JumpCounter = 0;
uint32_t gw4JumpPeriod = SAMPLE_RATE / 3;  // Fixed 3 Hz
uint16_t gw4SilenceChunkRemaining = 0;  // Samples left in current silence chunk

void generateWaveform4()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW4_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW4_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW4_CHUNK_SIZE_3; break;
      default: chunkSize = GW4_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW4_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW4_BLIP_SIZE > 0 && GW4_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW4_BLIP_SIZE);
    float blipFreq = GW4_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW4_BLIP_FREQ_MAX - GW4_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW4_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform4()
{
  // Random jumping through buffer
  gw4JumpCounter++;
  if (gw4JumpCounter >= gw4JumpPeriod) {
    gw4JumpCounter = 0;
    // Jump to random position in full buffer
    playbackPhase = rand12() % WAVEFORM_SIZE;
  }

  // Chunked silence injection (pot2 controls probability)
  // When a chunk ends, decide if next chunk is silent or not
  if (gw4SilenceChunkRemaining == 0) {
    // Choose random chunk size (same sizes as waveform generation)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW4_CHUNK_SIZE_1; break;  // 16
      case 1: chunkSize = GW4_CHUNK_SIZE_2; break;  // 4
      case 2: chunkSize = GW4_CHUNK_SIZE_3; break;  // 32
      default: chunkSize = GW4_CHUNK_SIZE_1; break;
    }

    // Decide if this chunk should be silent based on pot2
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool isSilentChunk = (randNorm < gw4SilenceProb);

    if (isSilentChunk) {
      gw4SilenceChunkRemaining = chunkSize;  // Start silence chunk
    } else {
      gw4SilenceChunkRemaining = 0;  // Continue playing normally
    }
  }

  // If we're in a silence chunk, output silence and count down
  if (gw4SilenceChunkRemaining > 0) {
    gw4SilenceChunkRemaining--;
    return 0;  // Silent sample
  }

  // Otherwise, read and play buffer normally
  int16_t sample = waveformBuffer[playbackPhase];


  generativeWaveform4_fractionalPhase += gw4PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform4_fractionalPhase;
  generativeWaveform4_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;

  while (playbackPhase >= WAVEFORM_SIZE) {
    playbackPhase -= WAVEFORM_SIZE;
  }

  return sample;
}

// ============================================
// VARIANT 5: Wandering Window
// ============================================
#define GW5_CHUNK_SIZE_1 0
#define GW5_CHUNK_SIZE_2 6
#define GW5_CHUNK_SIZE_3 48
#define GW5_NOISE_PROB 22
#define GW5_BLIP_SIZE 12
#define GW5_BLIP_FREQ_MIN 600.0f
#define GW5_BLIP_FREQ_MAX 2500.0f
#define GW5_WINDOW_SIZE (WAVEFORM_SIZE / 50)  // Window is buffer/50

float gw5PlaybackSpeed = 1.0f;      // pot1
uint16_t gw5WindowStart = 0;        // Wandering window position
uint32_t gw5WalkCounter = 0;
uint32_t gw5WalkPeriod = SAMPLE_RATE / 5;  // pot2 controls this

void generateWaveform5()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW5_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW5_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW5_CHUNK_SIZE_3; break;
      default: chunkSize = GW5_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW5_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW5_BLIP_SIZE > 0 && GW5_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW5_BLIP_SIZE);
    float blipFreq = GW5_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW5_BLIP_FREQ_MAX - GW5_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW5_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform5()
{
  // Random walk of window position (pot2 controls walk rate)
  gw5WalkCounter++;
  if (gw5WalkCounter >= gw5WalkPeriod) {
    gw5WalkCounter = 0;
    // Random walk: move window start ±1 to ±20 samples
    int16_t step = (rand12() & 1) ? (1 + rand12() % 20) : -(1 + rand12() % 20);
    int32_t newStart = (int32_t)gw5WindowStart + step;

    // Wrap around buffer
    if (newStart < 0) newStart += WAVEFORM_SIZE;
    if (newStart >= WAVEFORM_SIZE) newStart -= WAVEFORM_SIZE;

    gw5WindowStart = (uint16_t)newStart;
  }

  // Play within wandering window, loop at window boundaries
  uint16_t windowEnd = gw5WindowStart + GW5_WINDOW_SIZE;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;

  // Keep playback within current window
  if (playbackPhase < gw5WindowStart || playbackPhase >= windowEnd) {
    playbackPhase = gw5WindowStart;
  }

  int16_t sample = waveformBuffer[playbackPhase];


  generativeWaveform5_fractionalPhase += gw5PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform5_fractionalPhase;
  generativeWaveform5_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;

  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = gw5WindowStart;
  }

  return sample;
}

// ============================================
// VARIANT 6: Manual Window with Spray
// ============================================
#define GW6_CHUNK_SIZE_1 20
#define GW6_CHUNK_SIZE_2 5
#define GW6_CHUNK_SIZE_3 40
#define GW6_NOISE_PROB 35
#define GW6_BLIP_SIZE 15
#define GW6_BLIP_FREQ_MIN 700.0f
#define GW6_BLIP_FREQ_MAX 3500.0f
#define GW6_SPRAY_AMOUNT 30  // ±30 samples random offset

uint16_t gw6WindowStart = 0;        // pot1 controls this
uint16_t gw6WindowSize = WAVEFORM_SIZE;  // pot2 controls this
uint32_t gw6SprayCounter = 0;
int16_t gw6SprayOffset = 0;

void generateWaveform6()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW6_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW6_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW6_CHUNK_SIZE_3; break;
      default: chunkSize = GW6_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeNoise = (rand12() % 100) < GW6_NOISE_PROB;

    if (writeNoise) {
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW6_BLIP_SIZE > 0 && GW6_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW6_BLIP_SIZE);
    float blipFreq = GW6_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW6_BLIP_FREQ_MAX - GW6_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW6_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform6()
{
  // Spray modulation - update based on window size
  // Smaller window = faster spray updates
  uint32_t sprayPeriod = gw6WindowSize;  // Spray updates relative to window size
  if (sprayPeriod < 100) sprayPeriod = 100;  // Minimum update rate

  gw6SprayCounter++;
  if (gw6SprayCounter >= sprayPeriod) {
    gw6SprayCounter = 0;
    // Random spray offset: ±GW6_SPRAY_AMOUNT samples
    gw6SprayOffset = (rand12() % (GW6_SPRAY_AMOUNT * 2 + 1)) - GW6_SPRAY_AMOUNT;
  }

  // Calculate actual window start with spray
  int32_t actualStart = (int32_t)gw6WindowStart + gw6SprayOffset;

  // Keep within buffer bounds
  while (actualStart < 0) actualStart += WAVEFORM_SIZE;
  while (actualStart >= WAVEFORM_SIZE) actualStart -= WAVEFORM_SIZE;

  uint16_t windowEnd = actualStart + gw6WindowSize;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;

  // Keep playback within current window
  if (playbackPhase < actualStart || playbackPhase >= windowEnd) {
    playbackPhase = actualStart;
  }

  int16_t sample = waveformBuffer[playbackPhase];

  // Fixed slow playback speed (0.5x)

  generativeWaveform6_fractionalPhase += 0.5f;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform6_fractionalPhase;
  generativeWaveform6_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;

  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = actualStart;
  }

  return sample;
}

// ==================================================
// VARIANT 7: Noise or Saw Window (based on GW5)
// ==================================================
// ADJUST HERE: Chunk sizes (0 = silence, larger = more content)
#define GW7_CHUNK_SIZE_1 0      // Silence chunks
#define GW7_CHUNK_SIZE_2 6      // Short chunks
#define GW7_CHUNK_SIZE_3 48     // Long chunks

// ADJUST HERE: Probability of writing noise/saw vs silence (0-100)
#define GW7_NOISE_PROB 22       // 22% chance of noise/saw, 78% silence

// ADJUST HERE: Saw frequency range when chosen
#define GW7_SAW_FREQ_MIN 30.0f   // ADJUST: Min saw frequency (e.g., 30 Hz)
#define GW7_SAW_FREQ_MAX 1000.0f // ADJUST: Max saw frequency (e.g., 1000 Hz or 32 Hz for narrow range)

// ADJUST HERE: Probability of saw vs noise when writing audio (0-100)
#define GW7_SAW_PROB 50         // 50% = equal chance, 100% = always saw, 0% = always noise

#define GW7_BLIP_SIZE 12
#define GW7_BLIP_FREQ_MIN 600.0f
#define GW7_BLIP_FREQ_MAX 2500.0f
#define GW7_WINDOW_SIZE (WAVEFORM_SIZE / 50)

uint16_t gw7WindowStart = 0;
uint32_t gw7WalkCounter = 0;
uint32_t gw7WalkPeriod = SAMPLE_RATE / 5;  // pot2 controls this
float gw7PlaybackSpeed = 1.0f;  // pot1 controls this

void generateWaveform7()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW7_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW7_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW7_CHUNK_SIZE_3; break;
      default: chunkSize = GW7_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeAudio = (rand12() % 100) < GW7_NOISE_PROB;

    if (writeAudio) {
      // Decide: noise or saw?
      bool useSaw = (rand12() % 100) < GW7_SAW_PROB;

      if (useSaw) {
        // Write SAW with random frequency
        float sawFreq = GW7_SAW_FREQ_MIN +
                        ((float)(rand12() % 1000) / 1000.0f) *
                        (GW7_SAW_FREQ_MAX - GW7_SAW_FREQ_MIN);
        float sawPhase = 0.0f;
        float sawPhaseInc = sawFreq / SAMPLE_RATE;

        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;
        }
      } else {
        // Write latched NOISE
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Write silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW7_BLIP_SIZE > 0 && GW7_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW7_BLIP_SIZE);
    float blipFreq = GW7_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW7_BLIP_FREQ_MAX - GW7_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW7_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform7()
{
  // Random walk of window position (pot2 controls walk rate)
  gw7WalkCounter++;
  if (gw7WalkCounter >= gw7WalkPeriod) {
    gw7WalkCounter = 0;
    // Random walk: move window start ±1 to ±20 samples
    int16_t step = (rand12() & 1) ? (1 + rand12() % 20) : -(1 + rand12() % 20);
    int32_t newStart = (int32_t)gw7WindowStart + step;

    // Wrap around buffer
    if (newStart < 0) newStart += WAVEFORM_SIZE;
    if (newStart >= WAVEFORM_SIZE) newStart -= WAVEFORM_SIZE;

    gw7WindowStart = (uint16_t)newStart;
  }

  // Play within wandering window, loop at window boundaries
  uint16_t windowEnd = gw7WindowStart + GW7_WINDOW_SIZE;
  if (windowEnd > WAVEFORM_SIZE) windowEnd = WAVEFORM_SIZE;

  // Keep playback within current window
  if (playbackPhase < gw7WindowStart || playbackPhase >= windowEnd) {
    playbackPhase = gw7WindowStart;
  }

  int16_t sample = waveformBuffer[playbackPhase];


  generativeWaveform7_fractionalPhase += gw7PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform7_fractionalPhase;
  generativeWaveform7_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;

  // Loop at window boundary
  if (playbackPhase >= windowEnd) {
    playbackPhase = gw7WindowStart;
  }

  return sample;
}

// ==================================================
// VARIANT 17: Harmonic Drone Builder
// ==================================================
// Multiple triangle harmonics (root, 2×, 3×, 4×) + latched noise
// Creates bell-like drones and harmonic textures
// Two triangles mixed per harmonic chunk for instant harmony

#define GW17_CHUNK_SIZE_NOISE 2        // Tiny noise crackles
#define GW17_CHUNK_SIZE_HARMONIC 324   // Long harmonic drones

#define GW17_SILENCE_CHUNK_1 31        // Short gaps
#define GW17_SILENCE_CHUNK_2 127       // Medium gaps
#define GW17_SILENCE_CHUNK_3 251       // Long gaps

#define GW17_NOISE_PROB 30      // 30% noise
#define GW17_HARMONIC_PROB 50   // 50% harmonics (20% silence)

#define GW17_ROOT_FREQ_MIN 80.0f   // Low drones
#define GW17_ROOT_FREQ_MAX 240.0f  // Higher tonality

#define GW17_REGEN_INTERVAL (SAMPLE_RATE * 3)  // Regenerate every 3 seconds
#define GW17_PLAYBACK_SPEED 0.3f   // Fixed sweet spot!
#define GW17_STUTTER_MAX_PROB 0.005f  // CCW = 0.5% stutter (subtle!)

// GW17 state
uint32_t gw17RegenCounter = 0;
float gw17RootFreq = 150.0f;  // Stored root frequency (base, never changes)
float gw17OctaveMult = 1.0f;  // Octave multiplier from pot1 (applied to new chunks only)
float gw17StutterProb = 0.0f; // Stutter probability from pot2
uint16_t gw17StutterChunkRemaining = 0; // Samples to hold frozen

// Full buffer generation (called on algo selection)
void generateWaveform17()
{
  // Pick NEW root frequency for this algo session
  gw17RootFreq = GW17_ROOT_FREQ_MIN +
                 ((float)(rand12() % 1000) / 1000.0f) *
                 (GW17_ROOT_FREQ_MAX - GW17_ROOT_FREQ_MIN);

  // Initial generation uses BASE root freq (no octave multiplier yet!)
  // Harmonic series: root, 2×, 3×, 4×
  float harmonics[4] = {gw17RootFreq, gw17RootFreq * 2.0f, gw17RootFreq * 3.0f, gw17RootFreq * 4.0f};

  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint8_t roll = rand12() % 100;

    if (roll < GW17_NOISE_PROB) {
      // ===== NOISE CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_NOISE;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }

      // Latched noise
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;

    } else if (roll < (GW17_NOISE_PROB + GW17_HARMONIC_PROB)) {
      // ===== HARMONIC CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_HARMONIC;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }

      // Pick 2 different harmonics randomly
      uint8_t harm1 = rand12() % 4;
      uint8_t harm2 = rand12() % 4;
      while (harm2 == harm1) {
        harm2 = rand12() % 4;  // Ensure different
      }

      float freq1 = harmonics[harm1];
      float freq2 = harmonics[harm2];

      float phase1 = 0.0f;
      float phase2 = 0.0f;
      float phaseInc1 = freq1 / SAMPLE_RATE;
      float phaseInc2 = freq2 / SAMPLE_RATE;

      // Write two triangles mixed 50/50
      for (uint16_t i = 0; i < chunkSize; i++) {
        // Triangle 1
        float tri1 = (phase1 < 0.5f) ? (phase1 * 2.0f) : (2.0f - phase1 * 2.0f);
        int16_t sample1 = (int16_t)((tri1 * 2.0f - 1.0f) * 255.0f);  // Half amplitude

        // Triangle 2
        float tri2 = (phase2 < 0.5f) ? (phase2 * 2.0f) : (2.0f - phase2 * 2.0f);
        int16_t sample2 = (int16_t)((tri2 * 2.0f - 1.0f) * 255.0f);  // Half amplitude

        // Mix 50/50
        waveformBuffer[writePos + i] = sample1 + sample2;

        phase1 += phaseInc1;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        phase2 += phaseInc2;
        if (phase2 >= 1.0f) phase2 -= 1.0f;
      }
      writePos += chunkSize;

    } else {
      // ===== SILENCE CHUNK =====
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = GW17_SILENCE_CHUNK_1; break;
        case 1: silenceSize = GW17_SILENCE_CHUNK_2; break;
        case 2: silenceSize = GW17_SILENCE_CHUNK_3; break;
        default: silenceSize = GW17_SILENCE_CHUNK_1; break;
      }

      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }

      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }
}

// Partial buffer regeneration (called every 3s for gradual evolution)
// Regenerates ~30% of buffer with new harmonic pairs, keeps root freq same
// NEW: Applies pot1 octave multiplier to new chunks!
void partialRegenWaveform17()
{
  // Use stored root frequency with current octave multiplier from pot1!
  // This way new chunks slide in at different octave while old chunks stay
  float harmonics[4] = {
    gw17RootFreq * gw17OctaveMult,
    gw17RootFreq * gw17OctaveMult * 2.0f,
    gw17RootFreq * gw17OctaveMult * 3.0f,
    gw17RootFreq * gw17OctaveMult * 4.0f
  };

  // Regenerate 5 random chunks scattered through buffer (~30% coverage)
  for (uint8_t chunk = 0; chunk < 5; chunk++) {
    // Pick random start position
    uint16_t startPos = rand12() % (WAVEFORM_SIZE - GW17_CHUNK_SIZE_HARMONIC);

    // Decide: noise or harmonic (skip silence to avoid killing existing content)
    uint8_t roll = rand12() % 100;

    if (roll < 40) {
      // ===== NOISE CHUNK =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_NOISE;

      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[startPos + i] = crushedNoise;
      }

    } else {
      // ===== HARMONIC CHUNK with NEW random harmonic pair =====
      uint16_t chunkSize = GW17_CHUNK_SIZE_HARMONIC;

      // Pick 2 different harmonics
      uint8_t harm1 = rand12() % 4;
      uint8_t harm2 = rand12() % 4;
      while (harm2 == harm1) {
        harm2 = rand12() % 4;
      }

      float freq1 = harmonics[harm1];
      float freq2 = harmonics[harm2];

      float phase1 = 0.0f;
      float phase2 = 0.0f;
      float phaseInc1 = freq1 / SAMPLE_RATE;
      float phaseInc2 = freq2 / SAMPLE_RATE;

      // Write two triangles mixed 50/50
      for (uint16_t i = 0; i < chunkSize; i++) {
        float tri1 = (phase1 < 0.5f) ? (phase1 * 2.0f) : (2.0f - phase1 * 2.0f);
        int16_t sample1 = (int16_t)((tri1 * 2.0f - 1.0f) * 255.0f);

        float tri2 = (phase2 < 0.5f) ? (phase2 * 2.0f) : (2.0f - phase2 * 2.0f);
        int16_t sample2 = (int16_t)((tri2 * 2.0f - 1.0f) * 255.0f);

        waveformBuffer[startPos + i] = sample1 + sample2;

        phase1 += phaseInc1;
        if (phase1 >= 1.0f) phase1 -= 1.0f;

        phase2 += phaseInc2;
        if (phase2 >= 1.0f) phase2 -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform17()
{
  // Auto-regenerate parts of buffer every 3 seconds
  gw17RegenCounter++;
  if (gw17RegenCounter >= GW17_REGEN_INTERVAL) {
    partialRegenWaveform17();  // Smooth morphing!
    gw17RegenCounter = 0;
  }

  // Stutter/pause logic - FREEZE playback for chunk durations!
  if (gw17StutterChunkRemaining == 0) {
    // Pick chunk duration (MUCH longer than before for less aggressive stuttering!)
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = 666; break;   // ~42ms (the devil's stutter!)
      case 1: chunkSize = 1000; break;  // ~62ms
      case 2: chunkSize = 1533; break;  // ~96ms
      default: chunkSize = 666; break;
    }

    // Decide if this chunk should stutter/freeze based on pot2
    float randNorm = (rand12() % 1000) / 1000.0f;
    bool shouldStutter = (randNorm < gw17StutterProb);

    if (shouldStutter) {
      gw17StutterChunkRemaining = chunkSize;  // Start stutter chunk
    } else {
      gw17StutterChunkRemaining = 0;  // Play normally
    }
  }

  // If in stutter chunk, FREEZE playback (don't advance phase!)
  if (gw17StutterChunkRemaining > 0) {
    gw17StutterChunkRemaining--;
    return waveformBuffer[playbackPhase];  // Return same sample = STUTTER!
  }

  // Normal playback (0.3× sweet spot)

  generativeWaveform17_fractionalPhase += GW17_PLAYBACK_SPEED;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform17_fractionalPhase;
  generativeWaveform17_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  return waveformBuffer[playbackPhase];
}

// ==================================================
// VARIANT 18: BitBend Quad (FOUR bit manipulation layers!)
// ==================================================
// Modified Spacey Pulses with super-latched noise
// FOUR simultaneous bit manipulations on different ranges
// Bits 0-1: XOR, Bits 2-3: HOLD, Bits 4-6: SET_0, Bits 7-9: SET_1

// Quad buffer generation (Spacey Pulses style with super-latched noise)
#define GW18_CHUNK_SIZE_1 0
#define GW18_CHUNK_SIZE_2 57
#define GW18_CHUNK_SIZE_3 0
#define GW18_NOISE_PROB 12        // Sparse!
#define GW18_BLIP_SIZE 99         // Big blips
#define GW18_BLIP_FREQ_MIN 600.0f
#define GW18_BLIP_FREQ_MAX 900.0f
#define GW18_SILENCE_CHUNK_1 32
#define GW18_SILENCE_CHUNK_2 146
#define GW18_SILENCE_CHUNK_3 299  // HUGE gaps!

// GW18 BitBend Quad state
float gw18PlaybackSpeed = 1.0f;
uint32_t gw18BitClockPhase = 0;
uint32_t gw18BitClockPhaseInc = 0;
uint8_t gw18BitPos1 = 0;      // XOR on bits 0-1
uint8_t gw18BitPos2 = 2;      // HOLD on bits 2-3
uint8_t gw18BitPos3 = 5;      // SET_0 on bits 4-6
uint8_t gw18BitPos4 = 8;      // SET_1 on bits 7-9
uint16_t gw18HeldBits = 0;

void generateWaveform18()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < GW18_NOISE_PROB;

    if (writeAudio) {
      // Write super-latched noise chunk (only size 2)
      uint16_t chunkSize = GW18_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }

      // SUPER latched - only 10% update rate = crunchy!
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 1) crushedNoise = noise1();  // 10% update = super latched!
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size - BIG gaps!)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = GW18_SILENCE_CHUNK_1; break;
        case 1: silenceSize = GW18_SILENCE_CHUNK_2; break;
        case 2: silenceSize = GW18_SILENCE_CHUNK_3; break;
        default: silenceSize = GW18_SILENCE_CHUNK_1; break;
      }

      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }

      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }

  // Big blips scattered (3 blips)
  for (uint8_t b = 0; b < 3; b++) {
    if (GW18_BLIP_SIZE > 0 && GW18_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW18_BLIP_SIZE);
      float blipFreq = GW18_BLIP_FREQ_MIN +
                       ((float)(rand12() % 1000) / 1000.0f) *
                       (GW18_BLIP_FREQ_MAX - GW18_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;

      for (uint16_t i = 0; i < GW18_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform18()
{
  // Bit clock with 50/50 Bernoulli gate

  gw18BitClockPhase += gw18BitClockPhaseInc;
  uint32_t bitClockState = gw18BitClockPhase & 0x80000000;

  if (bitClockState && !generativeWaveform18_lastBitClockState) {
    if (rand12() & 1) {  // 50% Bernoulli gate
      // Random walk all FOUR positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      gw18BitPos1 = (gw18BitPos1 + step1) % 2;  // Keep in 0-1

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      gw18BitPos2 = 2 + ((gw18BitPos2 - 2 + step2 + 2) % 2);  // Keep in 2-3

      int8_t step3 = (rand12() & 1) ? 1 : -1;
      gw18BitPos3 = 4 + ((gw18BitPos3 - 4 + step3 + 3) % 3);  // Keep in 4-6

      int8_t step4 = (rand12() & 1) ? 1 : -1;
      gw18BitPos4 = 7 + ((gw18BitPos4 - 7 + step4 + 3) % 3);  // Keep in 7-9

      // Capture HOLD bits (range 2-3)
      uint16_t holdMask = ((1 << (gw18BitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      gw18HeldBits = playbackPhase & holdMask;
    }
  }
  generativeWaveform18_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform18_fractionalPhase += gw18PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform18_fractionalPhase;
  generativeWaveform18_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // QUAD manipulation: XOR + HOLD + SET_0 + SET_1
  uint16_t manipulatedAddress = playbackPhase;

  // 1. XOR on bits 0-1 (micro chaos)
  uint16_t xorMask = (1 << (gw18BitPos1 + 1)) - 1;
  manipulatedAddress ^= xorMask;

  // 2. HOLD on bits 2-3 (freeze layer)
  uint16_t holdMask = ((1 << (gw18BitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (gw18HeldBits & holdMask);

  // 3. SET_0 on bits 4-6 (pull to lower addresses)
  uint16_t set0Mask = ((1 << (gw18BitPos3 + 1)) - 1) & ~((1 << 4) - 1);
  manipulatedAddress &= ~set0Mask;

  // 4. SET_1 on bits 7-9 (push to higher addresses)
  uint16_t set1Mask = ((1 << (gw18BitPos4 + 1)) - 1) & ~((1 << 7) - 1);
  manipulatedAddress |= set1Mask;

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 8: BitBender (SoundScaper-style address manipulation)
// ==================================================
// Inspired by SoundScaper's "Clock Address Lines"
// Manipulates binary address bits to create glitches, loops, and granular chaos

// ADJUST HERE: Buffer content generation (same as GW7 for now)
#define GW8_CHUNK_SIZE_1 0      // Silence chunks
#define GW8_CHUNK_SIZE_2 8      // Short chunks
#define GW8_CHUNK_SIZE_3 64     // Long chunks
#define GW8_NOISE_PROB 55       // % chance of audio vs silence
#define GW8_SAW_PROB 40         // % of audio that's saw (vs noise)
#define GW8_SAW_FREQ_MIN 40.0f
#define GW8_SAW_FREQ_MAX 800.0f
#define GW8_BLIP_SIZE 16
#define GW8_BLIP_FREQ_MIN 800.0f
#define GW8_BLIP_FREQ_MAX 3000.0f

// BitBender state
float gw8PlaybackSpeed = 1.0f;         // pot1: playback speed
uint32_t gw8BitClockPhase = 0;
uint32_t gw8BitClockPhaseInc = 0;      // pot2: how fast bit manipulation changes
uint8_t gw8BitPosition = 5;            // Current bit being manipulated (0-11)
uint8_t gw8BitMode = 0;                // 0=SET_0, 1=SET_1, 2=XOR, 3=HOLD
uint16_t gw8HeldBits = 0;              // For HOLD mode

void generateWaveform8()
{
  // Generate buffer content (similar to GW7 - noise or saw chunks)
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = GW8_CHUNK_SIZE_1; break;
      case 1: chunkSize = GW8_CHUNK_SIZE_2; break;
      case 2: chunkSize = GW8_CHUNK_SIZE_3; break;
      default: chunkSize = GW8_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeAudio = (rand12() % 100) < GW8_NOISE_PROB;

    if (writeAudio) {
      // Decide: noise or saw?
      bool useSaw = (rand12() % 100) < GW8_SAW_PROB;

      if (useSaw) {
        // Write SAW
        float sawFreq = GW8_SAW_FREQ_MIN +
                        ((float)(rand12() % 1000) / 1000.0f) *
                        (GW8_SAW_FREQ_MAX - GW8_SAW_FREQ_MIN);
        float sawPhase = 0.0f;
        float sawPhaseInc = sawFreq / SAMPLE_RATE;

        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;
        }
      } else {
        // Write latched NOISE
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Write silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (GW8_BLIP_SIZE > 0 && GW8_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - GW8_BLIP_SIZE);
    float blipFreq = GW8_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (GW8_BLIP_FREQ_MAX - GW8_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < GW8_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform8()
{
  // Generative bit manipulation clock (pot2 controlled)

  gw8BitClockPhase += gw8BitClockPhaseInc;
  uint32_t bitClockState = gw8BitClockPhase & 0x80000000;

  // Rising edge - change bit manipulation generatively
  if (bitClockState && !generativeWaveform8_lastBitClockState) {
    // 50% chance to change mode
    if (rand12() & 1) {
      gw8BitMode = rand12() % 4;  // 0-3: SET_0, SET_1, XOR, HOLD
    }

    // Random walk bit position (wrap at 12 bits for 4096 buffer)
    int8_t step = (rand12() & 1) ? 1 : -1;
    gw8BitPosition = (gw8BitPosition + step) % 12;

    // For HOLD mode, capture current address state
    if (gw8BitMode == 3) {
      gw8HeldBits = playbackPhase & ((1 << gw8BitPosition) - 1);
    }
  }
  generativeWaveform8_lastBitClockState = bitClockState;

  // Advance playback position

  generativeWaveform8_fractionalPhase += gw8PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform8_fractionalPhase;
  generativeWaveform8_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // *** THE BITBENDING MAGIC ***
  // Manipulate address bits before reading buffer (SoundScaper-style!)
  uint16_t manipulatedAddress = playbackPhase;
  uint16_t bitMask = (1 << gw8BitPosition) - 1;  // Mask for bits 0 to bitPosition

  switch(gw8BitMode) {
    case 0: // SET_0 - Force lower bits to 0 (creates loops in lower addresses)
      manipulatedAddress &= ~bitMask;
      break;

    case 1: // SET_1 - Force lower bits to 1 (jumps to higher addresses)
      manipulatedAddress |= bitMask;
      break;

    case 2: // XOR - Toggle bits (chaotic jumping)
      manipulatedAddress ^= bitMask;
      break;

    case 3: // HOLD - Freeze lower bits at captured state (repeating patterns)
      manipulatedAddress = (manipulatedAddress & ~bitMask) | (gw8HeldBits & bitMask);
      break;
  }

  // Keep within buffer bounds
  manipulatedAddress %= WAVEFORM_SIZE;

  // Read from manipulated address!
  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 9: BitBend Sparse (WF1 content + manual mode selection)
// ==================================================
// Uses Sparse Glitchy buffer (GW1) with bit manipulation
// More predictable than Chaos - pot2 selects mode directly

// BitBend Sparse state
float gw9PlaybackSpeed = 1.0f;         // pot1: playback speed
const uint32_t GW9_BIT_CLOCK_FREQ = (uint32_t)(8.0f * 268435.456f);  // Fixed 8Hz bit clock
uint32_t gw9BitClockPhase = 0;
uint8_t gw9BitPosition = 5;            // Current bit being manipulated
uint8_t gw9BitMode = 0;                // pot2: 0=SET_0, 1=SET_1, 2=XOR, 3=HOLD
uint16_t gw9HeldBits = 0;              // For HOLD mode

// Use same generation as GW1 (Sparse Glitchy)
void generateWaveform9()
{
  // Just call GW1 generation - it uses shared waveformBuffer
  generateWaveform1();
}

inline int16_t generativeWaveform9()
{
  // Fixed 8Hz bit manipulation clock

  gw9BitClockPhase += GW9_BIT_CLOCK_FREQ;
  uint32_t bitClockState = gw9BitClockPhase & 0x80000000;

  // Rising edge - random walk bit position only (mode is pot-controlled)
  if (bitClockState && !generativeWaveform9_lastBitClockState) {
    // Random walk bit position
    int8_t step = (rand12() & 1) ? 1 : -1;
    gw9BitPosition = (gw9BitPosition + step) % 12;

    // For HOLD mode, capture current address state
    if (gw9BitMode == 3) {
      gw9HeldBits = playbackPhase & ((1 << gw9BitPosition) - 1);
    }
  }
  generativeWaveform9_lastBitClockState = bitClockState;

  // Advance playback position

  generativeWaveform9_fractionalPhase += gw9PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform9_fractionalPhase;
  generativeWaveform9_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // Bit manipulation (mode selected by pot2!)
  uint16_t manipulatedAddress = playbackPhase;
  uint16_t bitMask = (1 << gw9BitPosition) - 1;

  switch(gw9BitMode) {
    case 0: // SET_0 - Loops in lower addresses
      manipulatedAddress &= ~bitMask;
      break;

    case 1: // SET_1 - Jumps to higher addresses
      manipulatedAddress |= bitMask;
      break;

    case 2: // XOR - Chaotic bit flipping
      manipulatedAddress ^= bitMask;
      break;

    case 3: // HOLD - Freeze bits (locked loops)
      manipulatedAddress = (manipulatedAddress & ~bitMask) | (gw9HeldBits & bitMask);
      break;
  }

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// VARIANT 10: BitBend Dual (XOR + HOLD on different bit ranges)
// ==================================================
// Uses Spacey Pulses buffer (GW3) with TWO simultaneous bit manipulators
// XOR on low bits (chaotic), HOLD on high bits (locked loops)

// BitBend Dual state
float gw10PlaybackSpeed = 1.0f;        // pot1: playback speed
uint32_t gw10BitClockPhase = 0;
uint32_t gw10BitClockPhaseInc = 0;     // pot2: clock speed (Bernoulli gated)

// Two bit manipulators on different ranges
uint8_t gw10BitPosXOR = 3;             // XOR works on bits 0-3 (low range)
uint8_t gw10BitPosHOLD = 8;            // HOLD works on bits 4-8 (high range)
uint16_t gw10HeldBits = 0;             // Captured bits for HOLD mode

// Use same generation as GW3 (Spacey Pulses)
void generateWaveform10()
{
  generateWaveform3();
}

inline int16_t generativeWaveform10()
{
  // Clock with 50/50 Bernoulli gate (pot2 controlled)

  gw10BitClockPhase += gw10BitClockPhaseInc;
  uint32_t bitClockState = gw10BitClockPhase & 0x80000000;

  // Rising edge + 50/50 Bernoulli gate
  if (bitClockState && !generativeWaveform10_lastBitClockState) {
    if (rand12() & 1) {  // 50% chance to process clock
      // Random walk both bit positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      gw10BitPosXOR = (gw10BitPosXOR + step1) % 4;  // Keep in range 0-3

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      gw10BitPosHOLD = 4 + ((gw10BitPosHOLD - 4 + step2 + 5) % 5);  // Keep in range 4-8

      // Capture state for HOLD (only high bits)
      uint16_t holdMask = ((1 << (gw10BitPosHOLD + 1)) - 1) & ~((1 << 4) - 1);
      gw10HeldBits = playbackPhase & holdMask;
    }
  }
  generativeWaveform10_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform10_fractionalPhase += gw10PlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform10_fractionalPhase;
  generativeWaveform10_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // *** DUAL BIT MANIPULATION ***
  uint16_t manipulatedAddress = playbackPhase;

  // 1. XOR on low bits (0 to bitPosXOR) - chaotic jumping
  uint16_t xorMask = (1 << (gw10BitPosXOR + 1)) - 1;
  manipulatedAddress ^= xorMask;

  // 2. HOLD on high bits (4 to bitPosHOLD) - frozen loops
  uint16_t holdMask = ((1 << (gw10BitPosHOLD + 1)) - 1) & ~((1 << 4) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (gw10HeldBits & holdMask);

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Freeze (SET_0 + HOLD on different bit ranges)
// ==================================================
// Uses Wandering Window buffer (GW5 approach) with fixed 23Hz walk
// Less chaotic than Dual - stable loops with subtle variations

// BitBend Freeze state
float bbFreezePlaybackSpeed = 1.0f;
uint32_t bbFreezeBitClockPhase = 0;
uint32_t bbFreezeBitClockPhaseInc = 0;
uint8_t bbFreezeBitPosLow = 2;         // SET_0 on bits 0-2
uint8_t bbFreezeBitPosHigh = 6;        // HOLD on bits 3-6
uint16_t bbFreezeHeldBits = 0;

// Use GW5-style generation with fixed walk rate
void generateWaveform11()
{
  generateWaveform5();  // Reuse Wandering Window generation
}

inline int16_t generativeWaveform11()
{
  // Fixed 23Hz bit clock (no Bernoulli gate)

  bbFreezeBitClockPhase += bbFreezeBitClockPhaseInc;
  uint32_t bitClockState = bbFreezeBitClockPhase & 0x80000000;

  // Rising edge - random walk both bit positions
  if (bitClockState && !generativeWaveform11_lastBitClockState) {
    int8_t step1 = (rand12() & 1) ? 1 : -1;
    bbFreezeBitPosLow = (bbFreezeBitPosLow + step1) % 3;  // Keep in 0-2

    int8_t step2 = (rand12() & 1) ? 1 : -1;
    bbFreezeBitPosHigh = 3 + ((bbFreezeBitPosHigh - 3 + step2 + 4) % 4);  // Keep in 3-6

    // Capture HOLD bits
    uint16_t holdMask = ((1 << (bbFreezeBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
    bbFreezeHeldBits = playbackPhase & holdMask;
  }
  generativeWaveform11_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform11_fractionalPhase += bbFreezePlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform11_fractionalPhase;
  generativeWaveform11_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // Dual manipulation: SET_0 + HOLD
  uint16_t manipulatedAddress = playbackPhase;

  // 1. SET_0 on low bits (0 to bitPosLow) - loops in lower addresses
  uint16_t set0Mask = (1 << (bbFreezeBitPosLow + 1)) - 1;
  manipulatedAddress &= ~set0Mask;

  // 2. HOLD on high bits (3 to bitPosHigh) - frozen macro position
  uint16_t holdMask = ((1 << (bbFreezeBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbFreezeHeldBits & holdMask);

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Ping (XOR + SET_1 on different bit ranges)
// ==================================================
// Uses Noise or Triangle buffer (sparse, high-freq tris)
// Forward-rushing glitches that never settle

// Ping buffer generation params
#define BB_PING_CHUNK_SIZE_1 0
#define BB_PING_CHUNK_SIZE_2 7
#define BB_PING_CHUNK_SIZE_3 31
#define BB_PING_NOISE_PROB 17        // 17% audio, 83% silence
#define BB_PING_TRI_FREQ_MIN 3000.0f // High freq triangles
#define BB_PING_TRI_FREQ_MAX 4000.0f
#define BB_PING_TRI_PROB 37          // 37% triangle, 63% noise
#define BB_PING_BLIP_SIZE 12
#define BB_PING_BLIP_FREQ_MIN 600.0f
#define BB_PING_BLIP_FREQ_MAX 2500.0f

// BitBend Ping state
float bbPingPlaybackSpeed = 1.0f;
uint32_t bbPingBitClockPhase = 0;
uint32_t bbPingBitClockPhaseInc = 0;
uint8_t bbPingBitPosLow = 2;   // XOR on bits 0-2
uint8_t bbPingBitPosHigh = 7;  // SET_1 on bits 3-7

void generateWaveform12()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_PING_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_PING_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_PING_CHUNK_SIZE_3; break;
      default: chunkSize = BB_PING_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeAudio = (rand12() % 100) < BB_PING_NOISE_PROB;

    if (writeAudio) {
      bool useTri = (rand12() % 100) < BB_PING_TRI_PROB;

      if (useTri) {
        // High-frequency triangle
        float triFreq = BB_PING_TRI_FREQ_MIN +
                        ((float)(rand12() % 1000) / 1000.0f) *
                        (BB_PING_TRI_FREQ_MAX - BB_PING_TRI_FREQ_MIN);
        float triPhase = 0.0f;
        float triPhaseInc = triFreq / SAMPLE_RATE;

        for (uint16_t i = 0; i < chunkSize; i++) {
          float tri = (triPhase < 0.5f) ? (triPhase * 2.0f) : (2.0f - triPhase * 2.0f);
          int16_t triSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = triSample;
          triPhase += triPhaseInc;
          if (triPhase >= 1.0f) triPhase -= 1.0f;
        }
      } else {
        // Latched noise
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (BB_PING_BLIP_SIZE > 0 && BB_PING_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_PING_BLIP_SIZE);
    float blipFreq = BB_PING_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (BB_PING_BLIP_FREQ_MAX - BB_PING_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < BB_PING_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform12()
{
  // Bit clock with 50/50 Bernoulli gate

  bbPingBitClockPhase += bbPingBitClockPhaseInc;
  uint32_t bitClockState = bbPingBitClockPhase & 0x80000000;

  if (bitClockState && !generativeWaveform12_lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk both positions
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbPingBitPosLow = (bbPingBitPosLow + step1) % 3;  // Keep in 0-2

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbPingBitPosHigh = 3 + ((bbPingBitPosHigh - 3 + step2 + 5) % 5);  // Keep in 3-7
    }
  }
  generativeWaveform12_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform12_fractionalPhase += bbPingPlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform12_fractionalPhase;
  generativeWaveform12_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // Dual manipulation: XOR + SET_1
  uint16_t manipulatedAddress = playbackPhase;

  // 1. XOR on low bits - chaotic micro-jumps
  uint16_t xorMask = (1 << (bbPingBitPosLow + 1)) - 1;
  manipulatedAddress ^= xorMask;

  // 2. SET_1 on high bits - force skip forward
  uint16_t set1Mask = ((1 << (bbPingBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress |= set1Mask;

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Mirror (SET_0 + SET_1 on different bit ranges)
// ==================================================
// Uses Noise or Saw buffer with frequency sweep on saws
// Conflicting forces lock address to middle range - stable loops

// Mirror buffer generation with saw sweep
#define BB_MIRROR_CHUNK_SIZE_1 0
#define BB_MIRROR_CHUNK_SIZE_2 7
#define BB_MIRROR_CHUNK_SIZE_3 31
#define BB_MIRROR_NOISE_PROB 17
#define BB_MIRROR_SAW_PROB 79          // Mostly saws!
#define BB_MIRROR_SAW_FREQ_START 30.0f // Start freq
#define BB_MIRROR_SAW_FREQ_STEP 1.0f   // Increment per sample
#define BB_MIRROR_BLIP_SIZE 12
#define BB_MIRROR_BLIP_FREQ_MIN 600.0f
#define BB_MIRROR_BLIP_FREQ_MAX 2500.0f

// BitBend Mirror state
float bbMirrorPlaybackSpeed = 1.0f;
uint32_t bbMirrorBitClockPhase = 0;
uint32_t bbMirrorBitClockPhaseInc = 0;
uint8_t bbMirrorBitPosLow = 2;   // SET_0 on bits 0-2
uint8_t bbMirrorBitPosHigh = 7;  // SET_1 on bits 3-7

void generateWaveform13()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_MIRROR_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_MIRROR_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_MIRROR_CHUNK_SIZE_3; break;
      default: chunkSize = BB_MIRROR_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeAudio = (rand12() % 100) < BB_MIRROR_NOISE_PROB;

    if (writeAudio) {
      bool useSaw = (rand12() % 100) < BB_MIRROR_SAW_PROB;

      if (useSaw) {
        // Saw with frequency sweep
        float sawFreq = BB_MIRROR_SAW_FREQ_START;
        float sawPhase = 0.0f;

        for (uint16_t i = 0; i < chunkSize; i++) {
          int16_t sawSample = (int16_t)((sawPhase * 2.0f - 1.0f) * 511.0f);
          waveformBuffer[writePos + i] = sawSample;

          float sawPhaseInc = sawFreq / SAMPLE_RATE;
          sawPhase += sawPhaseInc;
          if (sawPhase >= 1.0f) sawPhase -= 1.0f;

          // Increment frequency for sweep
          sawFreq += BB_MIRROR_SAW_FREQ_STEP;
        }
      } else {
        // Latched noise
        int16_t crushedNoise = noise1();
        for (uint16_t i = 0; i < chunkSize; i++) {
          if ((rand12() % 10) < 3) crushedNoise = noise1();
          waveformBuffer[writePos + i] = crushedNoise;
        }
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // Triangle blip
  if (BB_MIRROR_BLIP_SIZE > 0 && BB_MIRROR_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_MIRROR_BLIP_SIZE);
    float blipFreq = BB_MIRROR_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (BB_MIRROR_BLIP_FREQ_MAX - BB_MIRROR_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < BB_MIRROR_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform13()
{
  // Bit clock with 50/50 Bernoulli gate

  bbMirrorBitClockPhase += bbMirrorBitClockPhaseInc;
  uint32_t bitClockState = bbMirrorBitClockPhase & 0x80000000;

  if (bitClockState && !generativeWaveform13_lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk both positions
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbMirrorBitPosLow = (bbMirrorBitPosLow + step1) % 3;  // Keep in 0-2

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbMirrorBitPosHigh = 3 + ((bbMirrorBitPosHigh - 3 + step2 + 5) % 5);  // Keep in 3-7
    }
  }
  generativeWaveform13_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform13_fractionalPhase += bbMirrorPlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform13_fractionalPhase;
  generativeWaveform13_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // Dual manipulation: SET_0 + SET_1 (conflicting forces!)
  uint16_t manipulatedAddress = playbackPhase;

  // 1. SET_0 on low bits - pull to lower addresses
  uint16_t set0Mask = (1 << (bbMirrorBitPosLow + 1)) - 1;
  manipulatedAddress &= ~set0Mask;

  // 2. SET_1 on high bits - push to higher addresses
  uint16_t set1Mask = ((1 << (bbMirrorBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress |= set1Mask;

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Triple (XOR + HOLD + SET_0 on three bit ranges)
// ==================================================
// Uses Spacey Pulses approach - triangle blips with silence
// THREE layers of manipulation = maximum complexity!

// Triple buffer generation (Spacey Pulses style)
#define BB_TRIPLE_CHUNK_SIZE_1 0
#define BB_TRIPLE_CHUNK_SIZE_2 33
#define BB_TRIPLE_CHUNK_SIZE_3 0
#define BB_TRIPLE_NOISE_PROB 21
#define BB_TRIPLE_BLIP_SIZE 31
#define BB_TRIPLE_BLIP_FREQ_MIN 100.0f
#define BB_TRIPLE_BLIP_FREQ_MAX 300.0f
#define BB_TRIPLE_SILENCE_CHUNK_1 63
#define BB_TRIPLE_SILENCE_CHUNK_2 127
#define BB_TRIPLE_SILENCE_CHUNK_3 251

// BitBend Triple state
float bbTriplePlaybackSpeed = 1.0f;
uint32_t bbTripleBitClockPhase = 0;
uint32_t bbTripleBitClockPhaseInc = 0;
uint8_t bbTripleBitPos1 = 1;   // XOR on bits 0-1 (chaos)
uint8_t bbTripleBitPos2 = 4;   // HOLD on bits 2-4 (freeze)
uint8_t bbTripleBitPos3 = 9;   // SET_0 on bits 5-9 (loop)
uint16_t bbTripleHeldBits = 0;

void generateWaveform14()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < BB_TRIPLE_NOISE_PROB;

    if (writeAudio) {
      // Write noise chunk
      uint16_t chunkSize = (rand12() & 1) ? BB_TRIPLE_CHUNK_SIZE_2 : BB_TRIPLE_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }

      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = BB_TRIPLE_SILENCE_CHUNK_1; break;
        case 1: silenceSize = BB_TRIPLE_SILENCE_CHUNK_2; break;
        case 2: silenceSize = BB_TRIPLE_SILENCE_CHUNK_3; break;
        default: silenceSize = BB_TRIPLE_SILENCE_CHUNK_1; break;
      }

      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }

      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }

  // Triangle blips scattered throughout
  for (uint8_t b = 0; b < 3; b++) {  // 3 blips
    if (BB_TRIPLE_BLIP_SIZE > 0 && BB_TRIPLE_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_TRIPLE_BLIP_SIZE);
      float blipFreq = BB_TRIPLE_BLIP_FREQ_MIN +
                       ((float)(rand12() % 1000) / 1000.0f) *
                       (BB_TRIPLE_BLIP_FREQ_MAX - BB_TRIPLE_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;

      for (uint16_t i = 0; i < BB_TRIPLE_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform14()
{
  // Bit clock with 50/50 Bernoulli gate

  bbTripleBitClockPhase += bbTripleBitClockPhaseInc;
  uint32_t bitClockState = bbTripleBitClockPhase & 0x80000000;

  if (bitClockState && !generativeWaveform14_lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk all three positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos1 = (bbTripleBitPos1 + step1) % 2;  // Keep in 0-1

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos2 = 2 + ((bbTripleBitPos2 - 2 + step2 + 3) % 3);  // Keep in 2-4

      int8_t step3 = (rand12() & 1) ? 1 : -1;
      bbTripleBitPos3 = 5 + ((bbTripleBitPos3 - 5 + step3 + 5) % 5);  // Keep in 5-9

      // Capture HOLD bits (range 2-4)
      uint16_t holdMask = ((1 << (bbTripleBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      bbTripleHeldBits = playbackPhase & holdMask;
    }
  }
  generativeWaveform14_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform14_fractionalPhase += bbTriplePlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform14_fractionalPhase;
  generativeWaveform14_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // TRIPLE manipulation: XOR + HOLD + SET_0
  uint16_t manipulatedAddress = playbackPhase;

  // 1. XOR on lowest bits (0 to bitPos1) - chaotic micro-jumps
  uint16_t xorMask = (1 << (bbTripleBitPos1 + 1)) - 1;
  manipulatedAddress ^= xorMask;

  // 2. HOLD on middle bits (2 to bitPos2) - frozen patterns
  uint16_t holdMask = ((1 << (bbTripleBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbTripleHeldBits & holdMask);

  // 3. SET_0 on highest bits (5 to bitPos3) - loop in lower addresses
  uint16_t set0Mask = ((1 << (bbTripleBitPos3 + 1)) - 1) & ~((1 << 5) - 1);
  manipulatedAddress &= ~set0Mask;

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Sweep (dual HOLD with different walk speeds)
// ==================================================
// Uses Dense Microglitch buffer
// Both manipulators use HOLD mode but walk at different speeds
// Creates nested loop structures evolving at different rates

// Sweep buffer generation (Dense Microglitch style)
#define BB_SWEEP_CHUNK_SIZE_1 5
#define BB_SWEEP_CHUNK_SIZE_2 0
#define BB_SWEEP_CHUNK_SIZE_3 47
#define BB_SWEEP_NOISE_PROB 7
#define BB_SWEEP_BLIP_SIZE 66
#define BB_SWEEP_BLIP_FREQ_MIN 2000.0f
#define BB_SWEEP_BLIP_FREQ_MAX 4200.0f

// BitBend Sweep state
float bbSweepPlaybackSpeed = 1.0f;
uint32_t bbSweepBitClock1Phase = 0;  // Slow walk
uint32_t bbSweepBitClock2Phase = 0;  // Fast walk
uint32_t bbSweepBitClockPhaseInc = 0; // Base clock (pot2)
uint8_t bbSweepBitPosLow = 2;         // HOLD on bits 0-2 (slow)
uint8_t bbSweepBitPosHigh = 6;        // HOLD on bits 3-6 (fast)
uint16_t bbSweepHeldBitsLow = 0;
uint16_t bbSweepHeldBitsHigh = 0;

void generateWaveform15()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    uint16_t chunkSize;
    uint8_t sizeChoice = rand12() % 3;
    switch(sizeChoice) {
      case 0: chunkSize = BB_SWEEP_CHUNK_SIZE_1; break;
      case 1: chunkSize = BB_SWEEP_CHUNK_SIZE_2; break;
      case 2: chunkSize = BB_SWEEP_CHUNK_SIZE_3; break;
      default: chunkSize = BB_SWEEP_CHUNK_SIZE_1; break;
    }

    if (writePos + chunkSize > WAVEFORM_SIZE) {
      chunkSize = WAVEFORM_SIZE - writePos;
    }

    bool writeAudio = (rand12() % 100) < BB_SWEEP_NOISE_PROB;

    if (writeAudio) {
      // Latched noise
      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
    } else {
      // Silence
      for (uint16_t i = 0; i < chunkSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
    }
    writePos += chunkSize;
  }

  // High-freq triangle blip
  if (BB_SWEEP_BLIP_SIZE > 0 && BB_SWEEP_BLIP_SIZE < WAVEFORM_SIZE) {
    uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_SWEEP_BLIP_SIZE);
    float blipFreq = BB_SWEEP_BLIP_FREQ_MIN +
                     ((float)(rand12() % 1000) / 1000.0f) *
                     (BB_SWEEP_BLIP_FREQ_MAX - BB_SWEEP_BLIP_FREQ_MIN);
    float blipPhase = 0.0f;
    float blipPhaseInc = blipFreq / SAMPLE_RATE;

    for (uint16_t i = 0; i < BB_SWEEP_BLIP_SIZE; i++) {
      float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
      int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
      waveformBuffer[blipPos + i] = blipSample;
      blipPhase += blipPhaseInc;
      if (blipPhase >= 1.0f) blipPhase -= 1.0f;
    }
  }
}

inline int16_t generativeWaveform15()
{
  // Two independent clocks with different speeds
  // Clock 1: Slow walk (pot2 / 3)

  bbSweepBitClock1Phase += (bbSweepBitClockPhaseInc / 3);  // 3x slower
  uint32_t bitClock1State = bbSweepBitClock1Phase & 0x80000000;

  if (bitClock1State && !generativeWaveform15_lastBitClock1State) {
    // Slow walk - low bits
    int8_t step1 = (rand12() & 1) ? 1 : -1;
    bbSweepBitPosLow = (bbSweepBitPosLow + step1) % 3;  // Keep in 0-2

    // Capture HOLD bits for low range
    uint16_t holdMask = (1 << (bbSweepBitPosLow + 1)) - 1;
    bbSweepHeldBitsLow = playbackPhase & holdMask;
  }
  generativeWaveform15_lastBitClock1State = bitClock1State;

  // Clock 2: Fast walk (pot2)

  bbSweepBitClock2Phase += bbSweepBitClockPhaseInc;  // Full speed
  uint32_t bitClock2State = bbSweepBitClock2Phase & 0x80000000;

  if (bitClock2State && !generativeWaveform15_lastBitClock2State) {
    // Fast walk - high bits
    int8_t step2 = (rand12() & 1) ? 1 : -1;
    bbSweepBitPosHigh = 3 + ((bbSweepBitPosHigh - 3 + step2 + 4) % 4);  // Keep in 3-6

    // Capture HOLD bits for high range
    uint16_t holdMask = ((1 << (bbSweepBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
    bbSweepHeldBitsHigh = playbackPhase & holdMask;
  }
  generativeWaveform15_lastBitClock2State = bitClock2State;

  // Advance playback

  generativeWaveform15_fractionalPhase += bbSweepPlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform15_fractionalPhase;
  generativeWaveform15_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // Dual HOLD manipulation at different rates
  uint16_t manipulatedAddress = playbackPhase;

  // 1. HOLD on low bits (slow evolution)
  uint16_t holdMaskLow = (1 << (bbSweepBitPosLow + 1)) - 1;
  manipulatedAddress = (manipulatedAddress & ~holdMaskLow) | (bbSweepHeldBitsLow & holdMaskLow);

  // 2. HOLD on high bits (fast evolution)
  uint16_t holdMaskHigh = ((1 << (bbSweepBitPosHigh + 1)) - 1) & ~((1 << 3) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMaskHigh) | (bbSweepHeldBitsHigh & holdMaskHigh);

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// ==================================================
// BitBend Triple B (SET_0 + SET_1 + HOLD on three bit ranges)
// ==================================================
// Different combo than Triple A: tug-of-war with frozen center
// SET_0 pulls low, SET_1 pushes high, HOLD freezes the middle

// Triple B buffer generation
#define BB_TRIPLEB_CHUNK_SIZE_1 0
#define BB_TRIPLEB_CHUNK_SIZE_2 47
#define BB_TRIPLEB_CHUNK_SIZE_3 0
#define BB_TRIPLEB_NOISE_PROB 17        // Sparse noise
#define BB_TRIPLEB_BLIP_SIZE 67         // Larger blip
#define BB_TRIPLEB_BLIP_FREQ_MIN 4000.0f  // High freq!
#define BB_TRIPLEB_BLIP_FREQ_MAX 6000.0f
#define BB_TRIPLEB_SILENCE_CHUNK_1 41
#define BB_TRIPLEB_SILENCE_CHUNK_2 111
#define BB_TRIPLEB_SILENCE_CHUNK_3 253

// BitBend Triple B state
float bbTripleBPlaybackSpeed = 1.0f;
uint32_t bbTripleBBitClockPhase = 0;
uint32_t bbTripleBBitClockPhaseInc = 0;
uint8_t bbTripleBBitPos1 = 1;   // SET_0 on bits 0-1 (pull to low)
uint8_t bbTripleBBitPos2 = 4;   // HOLD on bits 2-4 (freeze center)
uint8_t bbTripleBBitPos3 = 9;   // SET_1 on bits 5-9 (push to high)
uint16_t bbTripleBHeldBits = 0;

void generateWaveform16()
{
  uint16_t writePos = 0;

  while (writePos < WAVEFORM_SIZE) {
    // Decide: noise or silence
    bool writeAudio = (rand12() % 100) < BB_TRIPLEB_NOISE_PROB;

    if (writeAudio) {
      // Write noise chunk (only size 2)
      uint16_t chunkSize = BB_TRIPLEB_CHUNK_SIZE_2;
      if (writePos + chunkSize > WAVEFORM_SIZE) {
        chunkSize = WAVEFORM_SIZE - writePos;
      }

      int16_t crushedNoise = noise1();
      for (uint16_t i = 0; i < chunkSize; i++) {
        if ((rand12() % 10) < 3) crushedNoise = noise1();
        waveformBuffer[writePos + i] = crushedNoise;
      }
      writePos += chunkSize;
    } else {
      // Write silence chunk (variable size - BIG gaps!)
      uint16_t silenceSize;
      uint8_t choice = rand12() % 3;
      switch(choice) {
        case 0: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_1; break;
        case 1: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_2; break;
        case 2: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_3; break;
        default: silenceSize = BB_TRIPLEB_SILENCE_CHUNK_1; break;
      }

      if (writePos + silenceSize > WAVEFORM_SIZE) {
        silenceSize = WAVEFORM_SIZE - writePos;
      }

      for (uint16_t i = 0; i < silenceSize; i++) {
        waveformBuffer[writePos + i] = 0;
      }
      writePos += silenceSize;
    }
  }

  // High-freq triangle blips scattered (3 blips)
  for (uint8_t b = 0; b < 3; b++) {
    if (BB_TRIPLEB_BLIP_SIZE > 0 && BB_TRIPLEB_BLIP_SIZE < WAVEFORM_SIZE) {
      uint16_t blipPos = rand12() % (WAVEFORM_SIZE - BB_TRIPLEB_BLIP_SIZE);
      float blipFreq = BB_TRIPLEB_BLIP_FREQ_MIN +
                       ((float)(rand12() % 1000) / 1000.0f) *
                       (BB_TRIPLEB_BLIP_FREQ_MAX - BB_TRIPLEB_BLIP_FREQ_MIN);
      float blipPhase = 0.0f;
      float blipPhaseInc = blipFreq / SAMPLE_RATE;

      for (uint16_t i = 0; i < BB_TRIPLEB_BLIP_SIZE; i++) {
        float tri = (blipPhase < 0.5f) ? (blipPhase * 2.0f) : (2.0f - blipPhase * 2.0f);
        int16_t blipSample = (int16_t)((tri * 2.0f - 1.0f) * 511.0f);
        waveformBuffer[blipPos + i] = blipSample;
        blipPhase += blipPhaseInc;
        if (blipPhase >= 1.0f) blipPhase -= 1.0f;
      }
    }
  }
}

inline int16_t generativeWaveform16()
{
  // Bit clock with 50/50 Bernoulli gate

  bbTripleBBitClockPhase += bbTripleBBitClockPhaseInc;
  uint32_t bitClockState = bbTripleBBitClockPhase & 0x80000000;

  if (bitClockState && !generativeWaveform16_lastBitClockState) {
    if (rand12() & 1) {  // 50% chance
      // Random walk all three positions independently
      int8_t step1 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos1 = (bbTripleBBitPos1 + step1) % 2;  // Keep in 0-1

      int8_t step2 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos2 = 2 + ((bbTripleBBitPos2 - 2 + step2 + 3) % 3);  // Keep in 2-4

      int8_t step3 = (rand12() & 1) ? 1 : -1;
      bbTripleBBitPos3 = 5 + ((bbTripleBBitPos3 - 5 + step3 + 5) % 5);  // Keep in 5-9

      // Capture HOLD bits (range 2-4)
      uint16_t holdMask = ((1 << (bbTripleBBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
      bbTripleBHeldBits = playbackPhase & holdMask;
    }
  }
  generativeWaveform16_lastBitClockState = bitClockState;

  // Advance playback

  generativeWaveform16_fractionalPhase += bbTripleBPlaybackSpeed;

  uint16_t phaseIncrement = (uint16_t)generativeWaveform16_fractionalPhase;
  generativeWaveform16_fractionalPhase -= phaseIncrement;

  playbackPhase += phaseIncrement;
  if (playbackPhase >= WAVEFORM_SIZE) playbackPhase = 0;

  // TRIPLE manipulation: SET_0 + HOLD + SET_1
  uint16_t manipulatedAddress = playbackPhase;

  // 1. SET_0 on lowest bits (0 to bitPos1) - pull to lower addresses
  uint16_t set0Mask = (1 << (bbTripleBBitPos1 + 1)) - 1;
  manipulatedAddress &= ~set0Mask;

  // 2. HOLD on middle bits (2 to bitPos2) - frozen center
  uint16_t holdMask = ((1 << (bbTripleBBitPos2 + 1)) - 1) & ~((1 << 2) - 1);
  manipulatedAddress = (manipulatedAddress & ~holdMask) | (bbTripleBHeldBits & holdMask);

  // 3. SET_1 on highest bits (5 to bitPos3) - push to higher addresses
  uint16_t set1Mask = ((1 << (bbTripleBBitPos3 + 1)) - 1) & ~((1 << 5) - 1);
  manipulatedAddress |= set1Mask;

  manipulatedAddress %= WAVEFORM_SIZE;

  return waveformBuffer[manipulatedAddress];
}

// --------------------------------------------------
// Algorithm: Random Triangle (Bank 4 - Blips)
// --------------------------------------------------
uint32_t randomTriPhase = 0;
uint32_t randomTriPhaseInc = 0;  // Phase increment (sample & held)

// Simple envelope (attack = 0, decay controlled by pot2)
float randomTriEnvelope = 0.0f;
float randomTriEnvDecay = 0.9995f;  // Decay coefficient (pot2 controlled in params.h)

// Trigger timing (controlled by pot1 in params.h)
uint32_t randomTriTriggerCounter = 0;
uint32_t randomTriTriggerPeriod = SAMPLE_RATE / 2;  // pot1: 0.05s to 0.5s

inline int16_t randomTriangle()
{
  // Trigger clock with cheap irregularity
  randomTriTriggerCounter++;
  if (randomTriTriggerCounter >= randomTriTriggerPeriod) {
    randomTriTriggerCounter = 0;

    // 50% chance to skip this trigger
    bool skipTrigger = (rand12() & 1);

    if (!skipTrigger) {
      randomTriEnvelope = 1.0f;  // Attack = instant (0ms)

      // Sample & hold: new random frequency (250 Hz - 2.78 kHz)
      float randNorm = (rand12() % 1000) / 1000.0f;
      float randomFreq = 250.0f + randNorm * (2780.0f - 250.0f);
      randomTriPhaseInc = (uint32_t)(randomFreq * 268435.456f);
    }
  }

  // Apply decay
  randomTriEnvelope *= randomTriEnvDecay;

  // Advance phase (automatic wrap)
  randomTriPhase += randomTriPhaseInc;

  // Generate triangle from phase
  uint16_t phase_10bit = randomTriPhase >> 22;
  int16_t tri;
  if (phase_10bit < 512) {
    tri = (phase_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase_10bit << 1);
  }

  // Apply envelope
  return (int16_t)(tri * randomTriEnvelope);
}

// --------------------------------------------------
// Algorithm: Saw Clicks (Bank 3 - Blips)
// --------------------------------------------------
// Integer math version for better performance
uint32_t sawClick1Phase = 0;        // 32-bit phase accumulator
uint32_t sawClick2Phase = 0;        // 32-bit phase accumulator
uint32_t sawClick1PhaseInc = 65536; // Phase increment (pot1 controlled)
uint32_t sawClick2PhaseInc = 65536; // Phase increment (pot2 controlled)

inline int16_t sawClicks()
{
  // Advance phase accumulators (32-bit wraps automatically)
  sawClick1Phase += sawClick1PhaseInc;
  sawClick2Phase += sawClick2PhaseInc;

  // Extract top 10 bits as sawtooth value (0 to 1023)
  // Shift right by 22 bits to get top 10 bits from 32-bit phase
  int16_t saw1 = (sawClick1Phase >> 22);  // 0 to 1023
  int16_t saw2 = (sawClick2Phase >> 22);  // 0 to 1023

  // Convert to signed: 0-1023 becomes -512 to +511
  saw1 -= 512;
  saw2 -= 512;

  // Mix: average the two saws (divide by 2)
  int16_t mix = (saw1 + saw2) >> 1;  // Bit shift for fast divide by 2

  return mix;
}

// --------------------------------------------------
// Algorithm: Dust Burst (wandering probability)
// --------------------------------------------------
float dustBurstProb = 0.0f;          // current wandering probability
float dustBurstWalkSpeed = 0.001f;   // how fast prob wanders (pot1)
float dustBurstStepSize = 0.1f;      // how big each step is (pot2)
float dustBurstFilterAlpha = 1.0f;
float dustBurstFilterState = 0.0f;

inline int16_t dustBurst()
{
  // Random walk the probability
  // Each sample: small chance to take a step up or down
  if (rand12() < (uint16_t)(dustBurstWalkSpeed * 4095.0f)) {
    // Take a step
    float step = (rand12() & 1) ? dustBurstStepSize : -dustBurstStepSize;
    dustBurstProb += step;
    // Gentle gravity toward silence
    //dustBurstProb *= 0.995f;

    // Clamp 0.0 to 1.0
    if (dustBurstProb < 0.0f) dustBurstProb = 0.0f;
    if (dustBurstProb > 1.0f) dustBurstProb = 1.0f;

  }

  // Single dust generator driven by wandering probability
  int16_t rawClick = (rand12() < (uint16_t)(dustBurstProb * 4095.0f)) ? noise1() : 0;

  // 1-pole lowpass
  dustBurstFilterState += dustBurstFilterAlpha * ((float)rawClick - dustBurstFilterState);

  return (int16_t)dustBurstFilterState;
}

// --------------------------------------------------
// Algorithm: Highpass Noise (bit-masked highpass + irregular AM)
// --------------------------------------------------
const uint16_t HIGHPASS_NOISE_MASK = 0x380;  // Fixed highpass: bits 7-9
uint32_t highpassNoiseAMPhase = 0;
uint32_t highpassNoiseAMPhaseInc = 0;  // current cycle speed
uint32_t highpassNoiseBasePhaseInc = 0; // pot2: base speed
float highpassNoiseAMDepth = 1.0f;      // pot1: AM depth

// Weighted rate table - mostly 1.0 (stay), occasionally jumps
// ADJUST: change ratios or weighting to taste
const float highpassLFORateTable[16] = {
  1.0f, 1.0f, 1.0f, 1.0f,   // 25% stay same
  1.0f, 1.0f, 1.0f, 1.0f,   // 25% stay same
  1.33f, 0.5f, 0.75f, 0.89f, // 25% slight variation
  0.6f, 0.9f, 0.43f, 0.71f,  // 25% bigger jump
};

inline int16_t highpassNoise()
{
  // Generate highpass filtered noise
  int16_t n = noise1() & HIGHPASS_NOISE_MASK;

  // Advance LFO
  uint32_t prevPhase = highpassNoiseAMPhase;
  highpassNoiseAMPhase += highpassNoiseAMPhaseInc;

  // Detect LFO cycle wrap - pick new rate from table
  if (highpassNoiseAMPhase < prevPhase) {  // wrapped!
    uint8_t idx = rand12() % 16;
    highpassNoiseAMPhaseInc = (uint32_t)(highpassNoiseBasePhaseInc *
                               highpassLFORateTable[idx]);
  }

  // Generate triangle LFO (0-1023)
  uint16_t lfoPhase10bit = highpassNoiseAMPhase >> 22;
  uint16_t lfo = (lfoPhase10bit < 512) ? (lfoPhase10bit << 1) :
                                          (2047 - (lfoPhase10bit << 1));

  // Apply AM with depth control
  uint16_t staticLevel = 1023;
  uint16_t blendedLFO = (uint16_t)(lfo * highpassNoiseAMDepth +
                          staticLevel * (1.0f - highpassNoiseAMDepth));
  int32_t modulated = ((int32_t)n * blendedLFO) >> 10;

  return (int16_t)modulated;
}

// --------------------------------------------------
// Algorithm: High Noise Rhythm (clock-divided rhythms)
// --------------------------------------------------
uint32_t highNoiseRhythmClockPhase = 0;
uint32_t highNoiseRhythmClockPhaseInc = 0;  // pot1: 1-10 Hz
uint8_t highNoiseRhythmDivision = 1;        // pot2: 1-7 clock division
float highNoiseRhythmEnv1 = 0.0f;
float highNoiseRhythmEnv2 = 0.0f;
const float HIGH_NOISE_ENV1_DECAY = 0.965f;  // ADJUST
const float HIGH_NOISE_ENV2_DECAY = 0.985f;  // ADJUST
const uint16_t HIGH_NOISE_MASK = 0x3E0;       // Fixed ~5kHz highpass mask

inline int16_t highNoiseRhythm()
{
  // Main clock



  highNoiseRhythmClockPhase += highNoiseRhythmClockPhaseInc;
  uint32_t clockState = highNoiseRhythmClockPhase & 0x80000000;

  // Rising edge detection
  if (clockState && !highNoiseRhythm_lastClockState) {
    // Main clock triggers envelope 1
    highNoiseRhythmEnv1 = 1.0f;

    // Clock divider for envelope 2
    highNoiseRhythm_dividerCounter++;
    if (highNoiseRhythm_dividerCounter >= highNoiseRhythmDivision) {
      highNoiseRhythm_dividerCounter = 0;
      highNoiseRhythmEnv2 = 1.0f;
    }
  }
  highNoiseRhythm_lastClockState = clockState;

  // Decay envelopes
  highNoiseRhythmEnv1 *= HIGH_NOISE_ENV1_DECAY;
  highNoiseRhythmEnv2 *= HIGH_NOISE_ENV2_DECAY;

  // Generate highpass filtered noise for env1
  int16_t n1 = noise1() & HIGH_NOISE_MASK;
  int16_t env1Out = (int16_t)(n1 * highNoiseRhythmEnv1);

  // Generate raw noise for env2 (divided clock)
  int16_t n2 = noise1();
  int16_t env2Out = (int16_t)(n2 * highNoiseRhythmEnv2);

  // Mix
  return (env1Out + env2Out) >> 1;
}

// --------------------------------------------------
// Algorithm: Vinyl Crackle (PROGMEM sample)
// --------------------------------------------------


// Silence chunk sizes (ADJUST these to taste)
#define VC_SILENCE_1  57
#define VC_SILENCE_2 113
#define VC_SILENCE_3 331

// All state variables
uint16_t vcWindowSize = 1000;       // pot2 controlled
float vcPlaybackSpeed = 0.5f;       // pot1 controlled
float vcFractionalPhase = 0.0f;
uint16_t vcPlaybackPos = 0;
uint16_t vcChunkRemaining = 0;
bool vcInSilence = false;
inline int16_t vinylCrackleAlgo()
{
  // --- Chunk state machine ---
  if (vcChunkRemaining == 0) {
    if (vcInSilence) {
      // Silence just ended → start audio
      vcInSilence = false;
      vcPlaybackPos = rand12() % (VINYL_CRACKLE_SAMPLES - vcWindowSize);
      vcChunkRemaining = vcWindowSize;
    } else {
      // Audio just ended → maybe silence, maybe chain another window
      if (rand12() % 4 > 0) {  // 75% chance of silence
        vcInSilence = true;
        uint8_t choice = rand12() % 3;
        switch(choice) {
          case 0: vcChunkRemaining = VC_SILENCE_1; break;
          case 1: vcChunkRemaining = VC_SILENCE_2; break;
          case 2: vcChunkRemaining = VC_SILENCE_3; break;
        }
      } else {
        // Chain straight to another random window
        vcPlaybackPos = rand12() % (VINYL_CRACKLE_SAMPLES - vcWindowSize);
        vcChunkRemaining = vcWindowSize;
      }
    }
  }

  vcChunkRemaining--;

  // --- Output ---
  if (vcInSilence) {
    return 0;
  }

  // Read from PROGMEM
  uint16_t byteAddr = vcPlaybackPos * 2;
  uint8_t lo = pgm_read_byte(&vinylCrackle[byteAddr]);
  uint8_t hi = pgm_read_byte(&vinylCrackle[byteAddr + 1]);
  int16_t sample = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
  sample = sample >> 6;

  // Advance with fractional phase (pot1 = speed/pitch)
  vcFractionalPhase += vcPlaybackSpeed;
  uint16_t increment = (uint16_t)vcFractionalPhase;
  vcFractionalPhase -= increment;
  vcPlaybackPos += increment;

  return sample;
}


// --------------------------------------------------
// Algorithm: Fast Triangle (Bank 3 - Blips)
// --------------------------------------------------
uint32_t fastTriPhase = 0;
uint32_t fastTriPhaseInc = 0;    // pot1 controlled (20 - 6000 Hz)
float fastTriEnvelope = 0.0f;
float fastTriEnvDecay = 0.9995f; // pot2 controlled
uint8_t fastTriBitcrush = 10;    // Random bitcrush per trigger (3-12 bits)

// Trigger timing (FAST! ~960 BPM with 75% skip)
uint32_t fastTriTriggerCounter = 0;
uint32_t fastTriTriggerPeriod = SAMPLE_RATE / 16;  // ~960 BPM

inline int16_t fastTriangle()
{
  // Trigger clock with 75% skip (only 25% of triggers fire)
  fastTriTriggerCounter++;
  if (fastTriTriggerCounter >= fastTriTriggerPeriod) {
    fastTriTriggerCounter = 0;

    // 75% skip
    bool skipTrigger = (rand12() & 3) > 0;

    if (!skipTrigger) {
      fastTriEnvelope = 1.0f;  // Attack = instant

      // Sample & hold: random bitcrush amount (3-12 bits)
      fastTriBitcrush = 3 + (rand12() % 10);
    }
  }

  // Apply envelope decay
  fastTriEnvelope *= fastTriEnvDecay;

  // Advance phase (automatic wrap)
  fastTriPhase += fastTriPhaseInc;

  // Generate triangle from phase
  uint16_t phase_10bit = fastTriPhase >> 22;
  int16_t tri;
  if (phase_10bit < 512) {
    tri = (phase_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase_10bit << 1);
  }

  // Apply envelope
  int16_t enveloped = (int16_t)(tri * fastTriEnvelope);

  // AGGRESSIVE BITCRUSHING
  int16_t crushMask = ~((1 << (10 - fastTriBitcrush)) - 1);
  int16_t crushed = enveloped & crushMask;

  return crushed;
}

// --------------------------------------------------
// Algorithm: Harmonic Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// 3 triangle oscillators at harmonic intervals (1x, 3x, 4x)
// Each amplitude modulated by slow triangle LFOs at different rates
uint32_t harmTriPhase1 = 0, harmTriPhase2 = 0, harmTriPhase3 = 0;
uint32_t harmTriLFOPhase1 = 0, harmTriLFOPhase2 = 0, harmTriLFOPhase3 = 0;
uint32_t harmTriPhaseInc1 = 0;  // pot1: 50-400 Hz
uint32_t harmTriPhaseInc2 = 0;  // pot1 * 3
uint32_t harmTriPhaseInc3 = 0;  // pot1 * 4
uint32_t harmTriLFOInc1 = 0;    // pot2 controlled
uint32_t harmTriLFOInc2 = 0;    // pot2 * 1.33 (4:3 polyrhythm)
uint32_t harmTriLFOInc3 = 0;    // pot2 * 1.732 (√3)

inline int16_t harmonicTris()
{
  // Advance audio oscillators
  harmTriPhase1 += harmTriPhaseInc1;
  harmTriPhase2 += harmTriPhaseInc2;
  harmTriPhase3 += harmTriPhaseInc3;

  // Advance LFO oscillators
  harmTriLFOPhase1 += harmTriLFOInc1;
  harmTriLFOPhase2 += harmTriLFOInc2;
  harmTriLFOPhase3 += harmTriLFOInc3;

  // Generate triangles from phases
  uint16_t phase1_10bit = harmTriPhase1 >> 22;
  int16_t tri1 = (phase1_10bit < 512) ? ((phase1_10bit << 1) - 512) : (1535 - (phase1_10bit << 1));

  uint16_t phase2_10bit = harmTriPhase2 >> 22;
  int16_t tri2 = (phase2_10bit < 512) ? ((phase2_10bit << 1) - 512) : (1535 - (phase2_10bit << 1));

  uint16_t phase3_10bit = harmTriPhase3 >> 22;
  int16_t tri3 = (phase3_10bit < 512) ? ((phase3_10bit << 1) - 512) : (1535 - (phase3_10bit << 1));

  // Generate LFO triangles (0 to 1023 range for AM)
  uint16_t lfo1_10bit = harmTriLFOPhase1 >> 22;
  uint16_t lfo1 = (lfo1_10bit < 512) ? (lfo1_10bit << 1) : (2047 - (lfo1_10bit << 1));

  uint16_t lfo2_10bit = harmTriLFOPhase2 >> 22;
  uint16_t lfo2 = (lfo2_10bit < 512) ? (lfo2_10bit << 1) : (2047 - (lfo2_10bit << 1));

  uint16_t lfo3_10bit = harmTriLFOPhase3 >> 22;
  uint16_t lfo3 = (lfo3_10bit < 512) ? (lfo3_10bit << 1) : (2047 - (lfo3_10bit << 1));

  // Amplitude modulation (multiply and scale down)
  int16_t mod1 = (tri1 * (int16_t)lfo1) >> 10;  // Divide by 1024
  int16_t mod2 = (tri2 * (int16_t)lfo2) >> 10;
  int16_t mod3 = (tri3 * (int16_t)lfo3) >> 10;

  // Mix three modulated oscillators
  int32_t sum = (int32_t)mod1 + mod2 + mod3;
  return (int16_t)(sum / 3);  // Average
}

// --------------------------------------------------
// Algorithm: Phrygian Tri (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Triangle with envelope, triggered in bursts
// Frequency follows Phrygian scale via random walk
uint32_t phrygTriPhase = 0;
uint32_t phrygTriPhaseInc = 0;  // Current note frequency
float phrygTriEnvelope = 0.0f;
uint32_t phrygTriEnvCounter = 0;
uint32_t phrygTriTriggerCounter = 0;
uint32_t phrygTriTriggerPeriod = SAMPLE_RATE / 4;  // Base trigger rate
uint32_t phrygTriBurstMod = 0;  // Burst modulation phase
uint32_t phrygTriBurstModInc = 0;  // pot2 controlled
int8_t phrygTriScalePos = 0;  // Position in scale (0-7)

// Phrygian scale intervals (semitones from root): 0, 1, 3, 5, 7, 8, 10, 12
const float phrygianRatios[8] = {
  1.0f,      // Root
  1.0595f,   // Minor 2nd
  1.1892f,   // Minor 3rd
  1.3348f,   // Perfect 4th
  1.4983f,   // Perfect 5th
  1.5874f,   // Minor 6th
  1.7818f,   // Minor 7th
  2.0f       // Octave
};

inline int16_t phrygianTri()
{
  // Burst modulation - creates irregular trigger timing
  phrygTriBurstMod += phrygTriBurstModInc;
  uint16_t burstPhase = phrygTriBurstMod >> 22;  // 0-1023

  // Variable trigger period based on burst modulation
  uint32_t currentPeriod = phrygTriTriggerPeriod + (burstPhase << 4);  // Add variation

  // Trigger logic
  phrygTriTriggerCounter++;
  if (phrygTriTriggerCounter >= currentPeriod) {
    phrygTriTriggerCounter = 0;

    // Random walk through scale
    int8_t step = (rand12() & 1) ? 1 : -1;  // ±1
    phrygTriScalePos += step;
    if (phrygTriScalePos < 0) phrygTriScalePos = 7;
    if (phrygTriScalePos > 7) phrygTriScalePos = 0;

    // Trigger envelope
    phrygTriEnvelope = 1.0f;
    phrygTriEnvCounter = 0;
  }

  // Envelope decay (50ms = 800 samples at 16kHz)
  if (phrygTriEnvCounter < 800) {
    phrygTriEnvCounter++;
    phrygTriEnvelope *= 0.9963f;  // Decay coefficient for 50ms
  } else {
    phrygTriEnvelope = 0.0f;
  }

  // Generate triangle
  phrygTriPhase += phrygTriPhaseInc;
  uint16_t phase_10bit = phrygTriPhase >> 22;
  int16_t tri = (phase_10bit < 512) ? ((phase_10bit << 1) - 512) : (1535 - (phase_10bit << 1));

  // Apply envelope
  return (int16_t)(tri * phrygTriEnvelope);
}

// --------------------------------------------------
// Algorithm: Ring Modulation (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Two triangles multiplied together for ring mod effect
uint32_t ringModPhase1 = 0;
uint32_t ringModPhase2 = 0;
uint32_t ringModPhaseInc1 = 0;  // pot1 controlled
uint32_t ringModPhaseInc2 = 0;  // pot2 controlled

inline int16_t ringMod()
{
  // Advance phases
  ringModPhase1 += ringModPhaseInc1;
  ringModPhase2 += ringModPhaseInc2;

  // Generate triangles
  uint16_t phase1_10bit = ringModPhase1 >> 22;
  int16_t tri1 = (phase1_10bit < 512) ? ((phase1_10bit << 1) - 512) : (1535 - (phase1_10bit << 1));

  uint16_t phase2_10bit = ringModPhase2 >> 22;
  int16_t tri2 = (phase2_10bit < 512) ? ((phase2_10bit << 1) - 512) : (1535 - (phase2_10bit << 1));

  // Ring modulation: multiply and scale
  int32_t product = (int32_t)tri1 * tri2;
  return (int16_t)(product >> 9);  // Divide by 512 to get back to ±512 range
}

// --------------------------------------------------
// Algorithm: Two Saws (Bank 3 - Blips and Tones)
// --------------------------------------------------
uint32_t twoSawsPhase1 = 0;
uint32_t twoSawsPhase2 = 0;
uint32_t twoSawsPhaseInc1 = 0;  // pot1: 15-850 Hz
uint32_t twoSawsPhaseInc2 = 0;  // pot2: 15-850 Hz

inline int16_t twoSaws()
{
  // Advance phases
  twoSawsPhase1 += twoSawsPhaseInc1;
  twoSawsPhase2 += twoSawsPhaseInc2;

  // Generate saws from top 10 bits
  int16_t saw1 = (twoSawsPhase1 >> 22) - 512;  // -512 to +511
  int16_t saw2 = (twoSawsPhase2 >> 22) - 512;  // -512 to +511

  // Mix (average)
  return (saw1 + saw2) >> 1;  // Divide by 2
}

// --------------------------------------------------
// Algorithm: Noise OR Square (Bank 3 - Blips and Tones)
// --------------------------------------------------
uint32_t noiseOrSqPhase = 0;
uint32_t noiseOrSqPhaseInc = 0;     // pot2: 35-5000 Hz square
uint32_t noiseOrSqSHCounter = 0;
uint32_t noiseOrSqSHPeriod = 0;     // pot1: 15-116 Hz S&H rate
int16_t noiseOrSqHeldNoise = 0;

inline int16_t noiseOrSquare()
{
  // Sample & hold noise at rate controlled by pot1
  noiseOrSqSHCounter++;
  if (noiseOrSqSHCounter >= noiseOrSqSHPeriod) {
    noiseOrSqSHCounter = 0;
    noiseOrSqHeldNoise = noise1();
  }

  // Generate square wave
  noiseOrSqPhase += noiseOrSqPhaseInc;
  int16_t sq = (noiseOrSqPhase >> 22) - 512;

  // Bitwise OR
  return noiseOrSqHeldNoise | sq;
}

// --------------------------------------------------
// Algorithm: Major Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Fixed chord: 220 Hz (root), major 3rd, perfect 5th
// Each oscillator has its own triangle LFO for AM
uint32_t majorTrisPhase1 = 0, majorTrisPhase2 = 0, majorTrisPhase3 = 0;
uint32_t majorTrisLFO1Phase = 0, majorTrisLFO2Phase = 0, majorTrisLFO3Phase = 0;
const uint32_t MAJOR_TRIS_FREQ1 = (uint32_t)(220.0f * 268435.456f);          // 220 Hz
const uint32_t MAJOR_TRIS_FREQ2 = (uint32_t)(220.0f * 1.25992f * 268435.456f);  // Major 3rd (5/4)
const uint32_t MAJOR_TRIS_FREQ3 = (uint32_t)(220.0f * 1.5f * 268435.456f);      // Perfect 5th (3/2)
const uint32_t MAJOR_TRIS_LFO3 = (uint32_t)(0.2f * 268435.456f);           // Fixed 0.2 Hz
const uint32_t MAJOR_TRIS_LFO3_SLOW = (uint32_t)(0.035f * 268435.456f);    // Fixed 0.035 Hz
uint32_t majorTrisLFO1PhaseInc = 0;  // pot1: 0.08-2 Hz
uint32_t majorTrisLFO2PhaseInc = 0;  // pot2: 0.08-2 Hz

inline int16_t majorTris()
{
  // Advance audio oscillator phases
  majorTrisPhase1 += MAJOR_TRIS_FREQ1;
  majorTrisPhase2 += MAJOR_TRIS_FREQ2;
  majorTrisPhase3 += MAJOR_TRIS_FREQ3;

  // Generate triangles
  uint16_t p1 = majorTrisPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));

  uint16_t p2 = majorTrisPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));

  uint16_t p3 = majorTrisPhase3 >> 22;
  int16_t tri3 = (p3 < 512) ? ((p3 << 1) - 512) : (1535 - (p3 << 1));

  // Generate LFO triangles (0-1023 for AM)
  majorTrisLFO1Phase += majorTrisLFO1PhaseInc;
  uint16_t lfo1_10bit = majorTrisLFO1Phase >> 22;
  uint16_t lfo1 = (lfo1_10bit < 512) ? (lfo1_10bit << 1) : (2047 - (lfo1_10bit << 1));

  majorTrisLFO2Phase += majorTrisLFO2PhaseInc;
  uint16_t lfo2_10bit = majorTrisLFO2Phase >> 22;
  uint16_t lfo2 = (lfo2_10bit < 512) ? (lfo2_10bit << 1) : (2047 - (lfo2_10bit << 1));

  // LFO3: nested AM (0.2 Hz modulated by 0.035 Hz)
  majorTrisLFO3Phase += MAJOR_TRIS_LFO3;
  uint16_t lfo3_10bit = majorTrisLFO3Phase >> 22;
  uint16_t lfo3fast = (lfo3_10bit < 512) ? (lfo3_10bit << 1) : (2047 - (lfo3_10bit << 1));

  // Ultra-slow LFO (static variable for 4th phase)

  majorTris_lfo3SlowPhase += MAJOR_TRIS_LFO3_SLOW;
  uint16_t lfo3slow_10bit = majorTris_lfo3SlowPhase >> 22;
  uint16_t lfo3slow = (lfo3slow_10bit < 512) ? (lfo3slow_10bit << 1) : (2047 - (lfo3slow_10bit << 1));

  // Nested AM for LFO3
  uint16_t lfo3 = ((uint32_t)lfo3fast * lfo3slow) >> 10;

  // Apply amplitude modulation
  int16_t mod1 = (tri1 * (int16_t)lfo1) >> 10;
  int16_t mod2 = (tri2 * (int16_t)lfo2) >> 10;
  int16_t mod3 = (tri3 * (int16_t)lfo3) >> 10;

  // Mix
  int32_t sum = (int32_t)mod1 + mod2 + mod3;
  return (int16_t)(sum / 3);
}

// --------------------------------------------------
// Algorithm: Bernoulli Tris (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Two Bernoulli gates triggering enveloped triangles
// Clock goes through 50/50 Bernoulli gate before triggering
uint32_t bernTrisClockPhase = 0;
const uint32_t BERN_TRIS_CLOCK_FREQ = (uint32_t)(5.0f * 268435.456f);  // ADJUST HERE: clock speed (Hz)
const float BERN_TRIS_ENV_DECAY = 0.9985f;  // ADJUST HERE: envelope decay (0.99=fast, 0.999=slow)
uint32_t bernTrisPhase1 = 0, bernTrisPhase2 = 0;
uint32_t bernTrisPhaseInc1 = 0;  // Bernoulli gate 1 output
uint32_t bernTrisPhaseInc2 = 0;  // Bernoulli gate 2 output
float bernTrisEnv1 = 0.0f, bernTrisEnv2 = 0.0f;
float bernTrisProb1 = 0.5f;  // pot1
float bernTrisProb2 = 0.5f;  // pot2

inline int16_t bernoulliTris()
{
  // Clock (square wave for triggers)

  bernTrisClockPhase += BERN_TRIS_CLOCK_FREQ;
  uint32_t clockState = bernTrisClockPhase & 0x80000000;

  // Detect rising edge
  if (clockState && !bernoulliTris_lastClockState) {
    // 50/50 Bernoulli gate on the clock itself
    if (rand12() & 1) {  // 50% chance to let trigger through
      // Trigger!
      // Bernoulli gate 1
      float r1 = (rand12() % 1000) / 1000.0f;
      if (r1 < bernTrisProb1) {
        // Gate 1 A: 220 Hz
        bernTrisPhaseInc1 = (uint32_t)(220.0f * 268435.456f);
      } else {
        // Gate 1 B: perfect fifth (220 * 3/2)
        bernTrisPhaseInc1 = (uint32_t)(330.0f * 268435.456f);
      }
      bernTrisEnv1 = 1.0f;

      // Bernoulli gate 2
      float r2 = (rand12() % 1000) / 1000.0f;
      if (r2 < bernTrisProb2) {
        // Gate 2 A: minor third (220 * 6/5)
        bernTrisPhaseInc2 = (uint32_t)(264.0f * 268435.456f);
      } else {
        // Gate 2 B: minor seventh (220 * 9/5)
        bernTrisPhaseInc2 = (uint32_t)(396.0f * 268435.456f);
      }
      bernTrisEnv2 = 1.0f;
    }
    // else: clock blocked by 50/50 gate, no trigger
  }
  bernoulliTris_lastClockState = clockState;

  // Decay envelopes
  bernTrisEnv1 *= BERN_TRIS_ENV_DECAY;
  bernTrisEnv2 *= BERN_TRIS_ENV_DECAY;

  // Generate triangles
  bernTrisPhase1 += bernTrisPhaseInc1;
  uint16_t p1 = bernTrisPhase1 >> 22;
  int16_t tri1 = (p1 < 512) ? ((p1 << 1) - 512) : (1535 - (p1 << 1));

  bernTrisPhase2 += bernTrisPhaseInc2;
  uint16_t p2 = bernTrisPhase2 >> 22;
  int16_t tri2 = (p2 < 512) ? ((p2 << 1) - 512) : (1535 - (p2 << 1));

  // Apply envelopes and mix
  int16_t env1 = (int16_t)(tri1 * bernTrisEnv1);
  int16_t env2 = (int16_t)(tri2 * bernTrisEnv2);

  return (env1 + env2) >> 1;
}

// --------------------------------------------------
// Algorithm: Pentatonic Blips (Bank 3 - Blips and Tones)
// --------------------------------------------------
// Random pentatonic notes with envelope
uint32_t pentaBlipsClockPhase = 0;
uint32_t pentaBlipsClockPhaseInc = 0;  // pot1: 3-11.5 Hz
uint32_t pentaBlipsPhase = 0;
uint32_t pentaBlipsPhaseInc = 0;  // Selected pentatonic note
float pentaBlipsEnv = 0.0f;
float pentaBlipsEnvDecay = 0.999f;  // pot2 controlled

// Pentatonic scale from 220 Hz (major pentatonic: 1, 9/8, 5/4, 3/2, 27/16)
const uint32_t pentaBlipsNotes[5] = {
  (uint32_t)(220.0f * 268435.456f),           // Root
  (uint32_t)(220.0f * 1.125f * 268435.456f),  // Major 2nd (9/8)
  (uint32_t)(220.0f * 1.25f * 268435.456f),   // Major 3rd (5/4)
  (uint32_t)(220.0f * 1.5f * 268435.456f),    // Perfect 5th (3/2)
  (uint32_t)(220.0f * 1.6875f * 268435.456f)  // Major 6th (27/16)
};

inline int16_t pentatonicBlips()
{
  // Clock (square LFO -> Bernoulli gate 50/50)

  pentaBlipsClockPhase += pentaBlipsClockPhaseInc;
  uint32_t clockState = pentaBlipsClockPhase & 0x80000000;

  // Rising edge + Bernoulli gate (50/50)
  if (clockState && !pentatonicBlips_lastClockState) {
    if (rand12() & 1) {  // 50% chance
      // Pick random pentatonic note
      uint8_t noteIdx = rand12() % 5;
      pentaBlipsPhaseInc = pentaBlipsNotes[noteIdx];
      pentaBlipsEnv = 1.0f;  // Trigger envelope
    }
  }
  pentatonicBlips_lastClockState = clockState;

  // Decay envelope
  pentaBlipsEnv *= pentaBlipsEnvDecay;

  // Generate triangle
  pentaBlipsPhase += pentaBlipsPhaseInc;
  uint16_t p = pentaBlipsPhase >> 22;
  int16_t tri = (p < 512) ? ((p << 1) - 512) : (1535 - (p << 1));

  // Apply envelope
  return (int16_t)(tri * pentaBlipsEnv);
}

// ==================================================
// BANK 4: LOGIC OPERATIONS
// ==================================================

// --------------------------------------------------
// Algorithm 1: Three Cascaded Squares (Amplitude Modulation Chain)
// --------------------------------------------------
uint32_t logicCascadePhase1 = 0;
uint32_t logicCascadePhase2 = 0;
uint32_t logicCascadePhase3 = 0;
uint32_t logicCascadePhaseInc1 = 0;  // Fixed 9.96 Hz
uint32_t logicCascadePhaseInc2 = 0;  // pot1: 0.6 - 1024 Hz
uint32_t logicCascadePhaseInc3 = 0;  // pot2: 1 - 1024 Hz

inline int16_t threeCascadedSquares()
{
  // Advance phases (automatic wrap at 2^32)
  logicCascadePhase1 += logicCascadePhaseInc1;
  logicCascadePhase2 += logicCascadePhaseInc2;
  logicCascadePhase3 += logicCascadePhaseInc3;

  // Extract squares from top bit of phase (bit 31)
  int16_t sq1 = (logicCascadePhase1 & 0x80000000) ? 511 : -512;
  int16_t sq2 = (logicCascadePhase2 & 0x80000000) ? 511 : -512;
  int16_t sq3 = (logicCascadePhase3 & 0x80000000) ? 511 : -512;

  // Amplitude modulation chain: sq1 AM -> sq2, sq2 AM -> sq3
  // Convert to 0-511 range for AM
  int16_t sq1_am = (sq1 + 512) >> 1;  // 0 to 511
  int16_t sq2_am = (sq2 + 512) >> 1;  // 0 to 511

  // sq1 modulates sq2
  int16_t sq2_modulated = (sq2 * sq1_am) >> 9;  // Divide by 512

  // sq2 modulates sq3
  int16_t sq2_mod_am = (sq2_modulated + 512) >> 1;
  int16_t output = (sq3 * sq2_mod_am) >> 9;

  return output;
}

// --------------------------------------------------
// Algorithm 2: NOR Square
// --------------------------------------------------
uint32_t logicNorPhase1 = 0;
uint32_t logicNorPhase2 = 0;
uint32_t logicNorPhaseInc1 = 0;   // pot1: 0.8 - 200 Hz
uint32_t logicNorPhaseInc2 = 0;   // pot2: 0.73 - 215 Hz

inline int16_t norSquare()
{
  // Advance phases
  logicNorPhase1 += logicNorPhaseInc1;
  logicNorPhase2 += logicNorPhaseInc2;

  // Extract squares from top 10 bits (to match DAC resolution)
  int16_t sq1 = (logicNorPhase1 >> 22) - 512;  // -512 to +511
  int16_t sq2 = (logicNorPhase2 >> 22) - 512;  // -512 to +511

  // Bitwise NOR: ~(A | B)
  // This operates on the actual sample values, not booleans!
  int16_t result = ~(sq1 | sq2);

  // Mask to 10-bit range
  return result & 0x3FF;  // Keep only bottom 10 bits
}

// --------------------------------------------------
// Algorithm 3: Tri OR Saw
// --------------------------------------------------
uint32_t logicTriOrPhase1 = 0;
uint32_t logicTriOrPhase2 = 0;
uint32_t logicTriOrPhaseInc1 = 0;   // pot1: 4.5 - 1024 Hz
uint32_t logicTriOrPhaseInc2 = 0;   // pot2: 2 - 1024 Hz

inline int16_t triOrSaw()
{
  // Advance phases
  logicTriOrPhase1 += logicTriOrPhaseInc1;
  logicTriOrPhase2 += logicTriOrPhaseInc2;

  // Triangle: top 10 bits, convert to triangle shape
  uint16_t phase1_10bit = logicTriOrPhase1 >> 22;
  int16_t tri;
  if (phase1_10bit < 512) {
    tri = (phase1_10bit << 1) - 512;
  } else {
    tri = 1535 - (phase1_10bit << 1);
  }

  // Sawtooth: top 10 bits
  int16_t saw = (logicTriOrPhase2 >> 22) - 512;

  // Bitwise OR
  int16_t result = tri | saw;

  return result;
}

// --------------------------------------------------
// Algorithm 4: Tri NOR Tri
// --------------------------------------------------
uint32_t logicTriNorPhase1 = 0;
uint32_t logicTriNorPhase2 = 0;
uint32_t logicTriNorPhaseInc1 = 0;    // pot1: 4 - 880 Hz
uint32_t logicTriNorPhaseInc2 = 0;    // pot2: 15 - 900 Hz

inline int16_t triNorTri()
{
  // Advance phases
  logicTriNorPhase1 += logicTriNorPhaseInc1;
  logicTriNorPhase2 += logicTriNorPhaseInc2;

  // Triangle 1
  uint16_t phase1_10bit = logicTriNorPhase1 >> 22;
  int16_t tri1;
  if (phase1_10bit < 512) {
    tri1 = (phase1_10bit << 1) - 512;
  } else {
    tri1 = 1535 - (phase1_10bit << 1);
  }

  // Triangle 2
  uint16_t phase2_10bit = logicTriNorPhase2 >> 22;
  int16_t tri2;
  if (phase2_10bit < 512) {
    tri2 = (phase2_10bit << 1) - 512;
  } else {
    tri2 = 1535 - (phase2_10bit << 1);
  }

  // Bitwise NOR: ~(A | B)
  int16_t result = ~(tri1 | tri2);

  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 5: Tri XOR Tri
// --------------------------------------------------
uint32_t logicTriXorPhase1 = 0;
uint32_t logicTriXorPhase2 = 0;
uint32_t logicTriXorPhaseInc1 = 0;    // pot1: 0.7 - 220 Hz
uint32_t logicTriXorPhaseInc2 = 0;    // pot2: 0.6 - 440 Hz

inline int16_t triXorTri()
{
  // Advance phases
  logicTriXorPhase1 += logicTriXorPhaseInc1;
  logicTriXorPhase2 += logicTriXorPhaseInc2;

  // Triangle 1
  uint16_t phase1_10bit = logicTriXorPhase1 >> 22;
  int16_t tri1;
  if (phase1_10bit < 512) {
    tri1 = (phase1_10bit << 1) - 512;
  } else {
    tri1 = 1535 - (phase1_10bit << 1);
  }

  // Triangle 2
  uint16_t phase2_10bit = logicTriXorPhase2 >> 22;
  int16_t tri2;
  if (phase2_10bit < 512) {
    tri2 = (phase2_10bit << 1) - 512;
  } else {
    tri2 = 1535 - (phase2_10bit << 1);
  }

  // Bitwise XOR
  int16_t result = tri1 ^ tri2;

  return result;
}

// --------------------------------------------------
// Algorithm 6: Square XNOR Square
// --------------------------------------------------
uint32_t logicXnorPhase1 = 0;
uint32_t logicXnorPhase2 = 0;
uint32_t logicXnorPhaseInc1 = 0;   // pot1: 0.5 - 440 Hz
uint32_t logicXnorPhaseInc2 = 0;   // pot2: 0.6 - 150 Hz

inline int16_t squareXnorSquare()
{
  // Advance phases
  logicXnorPhase1 += logicXnorPhaseInc1;
  logicXnorPhase2 += logicXnorPhaseInc2;

  // Extract squares from top 10 bits
  int16_t sq1 = (logicXnorPhase1 >> 22) - 512;
  int16_t sq2 = (logicXnorPhase2 >> 22) - 512;

  // Bitwise XNOR: ~(A ^ B)
  int16_t result = ~(sq1 ^ sq2);

  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 7: Square NAND Square
// --------------------------------------------------
uint32_t logicNandPhase1 = 0;
uint32_t logicNandPhase2 = 0;
uint32_t logicNandPhaseInc1 = 0;   // pot1: 0.1 - 50 Hz
uint32_t logicNandPhaseInc2 = 0;   // pot2: 0.08 - 45 Hz

inline int16_t squareNandSquare()
{
  // Advance phases
  logicNandPhase1 += logicNandPhaseInc1;
  logicNandPhase2 += logicNandPhaseInc2;

  // Extract squares from top 10 bits
  int16_t sq1 = (logicNandPhase1 >> 22) - 512;
  int16_t sq2 = (logicNandPhase2 >> 22) - 512;

  // Bitwise NAND: ~(A & B)
  int16_t result = ~(sq1 & sq2);

  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 8: Noise NOR Noise
// --------------------------------------------------
uint32_t noiseNorCounter1 = 0;
uint32_t noiseNorCounter2 = 0;
uint32_t noiseNorPeriod1 = 0;   // pot1: 5-120 Hz (samples between S&H)
uint32_t noiseNorPeriod2 = 0;   // pot2: 5-120 Hz
int16_t noiseNorHeld1 = 0;      // Held noise value 1
int16_t noiseNorHeld2 = 0;      // Held noise value 2

inline int16_t noiseNorNoise()
{
  // Sample & hold 1 at rate controlled by pot1
  noiseNorCounter1++;
  if (noiseNorCounter1 >= noiseNorPeriod1) {
    noiseNorCounter1 = 0;
    noiseNorHeld1 = noise1();  // Sample noise
  }

  // Sample & hold 2 at rate controlled by pot2
  noiseNorCounter2++;
  if (noiseNorCounter2 >= noiseNorPeriod2) {
    noiseNorCounter2 = 0;
    noiseNorHeld2 = noise1();  // Sample noise (separate instance)
  }

  // Bitwise NOR: ~(A | B)
  int16_t result = ~(noiseNorHeld1 | noiseNorHeld2);

  return result & 0x3FF;  // Mask to 10-bit
}

// --------------------------------------------------
// Algorithm 9: Square OR Square
// --------------------------------------------------
uint32_t logicSquareOrPhase1 = 0;
uint32_t logicSquareOrPhase2 = 0;
uint32_t logicSquareOrPhaseInc1 = 0;  // pot1: 15-850 Hz
uint32_t logicSquareOrPhaseInc2 = 0;  // pot2: 15-850 Hz

inline int16_t squareOrSquare()
{
  // Advance phases
  logicSquareOrPhase1 += logicSquareOrPhaseInc1;
  logicSquareOrPhase2 += logicSquareOrPhaseInc2;

  // Extract squares from top 10 bits
  int16_t sq1 = (logicSquareOrPhase1 >> 22) - 512;
  int16_t sq2 = (logicSquareOrPhase2 >> 22) - 512;

  // Bitwise OR
  return sq1 | sq2;
}

#endif // ALGOS_H

#ifndef PARAMS_H
#define PARAMS_H





// --------------------------------------------------
// Latched Noise Parameter Ranges
// --------------------------------------------------
#define HOLD_MIN_HZ 9.0f
#define HOLD_MAX_HZ 8000.0f
#define LATCH_PROB_MIN 0.05f   // never totally dead
#define LATCH_PROB_MAX 1.0f

void updateLatchedNoiseParams()
{
  // pot2 → hold rate
  float freq =
      HOLD_MIN_HZ +
      (HOLD_MAX_HZ - HOLD_MIN_HZ) * (pot2_norm * pot2_norm);
  holdPeriodSamples = max(1, (uint32_t)(SAMPLE_RATE / freq));

  // pot1 → irregularity probability (quadratic for more CCW resolution)
  float curved = pot1_norm * pot1_norm;
  float p =
      LATCH_PROB_MIN +
      (LATCH_PROB_MAX - LATCH_PROB_MIN) * curved;
  latchProbThreshold = (uint16_t)(p * 4095.0f);
}

// --------------------------------------------------
// Dust Parameter Ranges
// --------------------------------------------------
#define DUST_MIN_HZ 1.0f     // one click every ~1s
#define DUST_MAX_HZ 2000.0f
#define DUST_CUTOFF_MIN 90.0f
#define DUST_CUTOFF_MAX 8000.0f

void updateDustParams()
{
  // pot1 → lowpass cutoff frequency (20 Hz - 8000 Hz)
  float cutoff = DUST_CUTOFF_MIN + (DUST_CUTOFF_MAX - DUST_CUTOFF_MIN) * pot1_norm;

  // Calculate filter coefficient (expensive math done here, not in ISR!)
  dustFilterAlpha = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);

  // Clamp alpha to valid range
  if (dustFilterAlpha < 0.0f) dustFilterAlpha = 0.0f;
  if (dustFilterAlpha > 1.0f) dustFilterAlpha = 1.0f;

  // pot2 → dust probability
  float eventsPerSecond =
    DUST_MIN_HZ +
    (DUST_MAX_HZ - DUST_MIN_HZ) * pot2_norm;

  dustProb = eventsPerSecond / SAMPLE_RATE;
}

// --------------------------------------------------
// Dust Burst Parameter Ranges
// --------------------------------------------------
#define DUST_BURST_WALK_SPEED_MIN 0.0001f  // ADJUST: slowest drift
#define DUST_BURST_WALK_SPEED_MAX 0.02f    // ADJUST: fastest drift
#define DUST_BURST_STEP_MIN 0.05f          // ADJUST: smallest step (subtle)
#define DUST_BURST_STEP_MAX 0.8f           // ADJUST: largest step (wild swings)

void updateDustBurstParams()
{
  // pot1 → walk speed (how often a step happens)
  // quadratic for more resolution at slow end
  float speedNorm = pot1_norm * pot1_norm;
  dustBurstWalkSpeed = DUST_BURST_WALK_SPEED_MIN +
                       (DUST_BURST_WALK_SPEED_MAX - DUST_BURST_WALK_SPEED_MIN) * speedNorm;

  // pot2 → step size (how big each jump in probability)
  dustBurstStepSize = DUST_BURST_STEP_MIN +
                      (DUST_BURST_STEP_MAX - DUST_BURST_STEP_MIN) * pot2_norm;

  // fixed lowpass on dust output
  float cutoff = 2000.0f;
  dustBurstFilterAlpha = 1.0f - expf(-2.0f * PI * cutoff / SAMPLE_RATE);
  if (dustBurstFilterAlpha < 0.0f) dustBurstFilterAlpha = 0.0f;
  if (dustBurstFilterAlpha > 1.0f) dustBurstFilterAlpha = 1.0f;
}

// --------------------------------------------------
// Highpass Noise Parameters
// --------------------------------------------------
#define HIGHPASS_NOISE_AM_MIN 0.3f   // ADJUST: slowest LFO
#define HIGHPASS_NOISE_AM_MAX 20.0f  // ADJUST: fastest LFO

void updateHighpassNoiseParams()
{
  // pot1 → AM depth (INVERTED: CCW=full modulation, CW=static)
  highpassNoiseAMDepth = 1.0f - pot1_norm;

  // pot2 → base LFO frequency (table applies ratios on top)
  float amFreq = HIGHPASS_NOISE_AM_MIN +
                 (HIGHPASS_NOISE_AM_MAX - HIGHPASS_NOISE_AM_MIN) * pot2_norm;
  highpassNoiseBasePhaseInc = (uint32_t)(amFreq * 268435.456f);

  // Only update current speed if not mid-cycle
  // (let the wrap detection handle speed changes naturally)
  if (highpassNoiseAMPhaseInc == 0) {
    highpassNoiseAMPhaseInc = highpassNoiseBasePhaseInc;
  }
}

// --------------------------------------------------
// High Noise Rhythm Parameters
// --------------------------------------------------
#define HIGH_NOISE_RHYTHM_CLOCK_MIN 1.0f
#define HIGH_NOISE_RHYTHM_CLOCK_MAX 10.0f

void updateHighNoiseRhythmParams()
{
  // pot1 → main clock frequency (1-10 Hz)
  float clockFreq = HIGH_NOISE_RHYTHM_CLOCK_MIN +
                    (HIGH_NOISE_RHYTHM_CLOCK_MAX - HIGH_NOISE_RHYTHM_CLOCK_MIN) * pot1_norm;
  highNoiseRhythmClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);

  // pot2 → clock division (1-7)
  highNoiseRhythmDivision = 1 + (uint8_t)(pot2_norm * 6.0f);  // 1 to 7
  if (highNoiseRhythmDivision < 1) highNoiseRhythmDivision = 1;
  if (highNoiseRhythmDivision > 7) highNoiseRhythmDivision = 7;
}

// --------------------------------------------------
// Vinyl Crackle Parameters
// --------------------------------------------------
#define VC_SPEED_MIN 0.04f
#define VC_SPEED_MAX 2.0f
#define VC_WINDOW_MIN 200
#define VC_WINDOW_MAX 4000

void updateVinylCrackleParams()
{
  // pot1 → playback speed (pitch)
  vcPlaybackSpeed = VC_SPEED_MIN + (VC_SPEED_MAX - VC_SPEED_MIN) * pot1_norm;

  // pot2 → window size (short bursts to long runs)
  // stored as global so ISR can read it
  vcWindowSize = VC_WINDOW_MIN + (uint16_t)((VC_WINDOW_MAX - VC_WINDOW_MIN) * pot2_norm);
}



// --------------------------------------------------
// FMnoise Parameter Ranges
// --------------------------------------------------
#define FM_FREQ_MIN 2.0f
#define FM_FREQ_MAX 2000.0f
#define FM_CLOCK_MIN_HZ 0.5f
#define FM_CLOCK_MAX_HZ 20.0f

// ADJUST: coincidence clock ratio - irrational = never repeating
#define FM_CLOCK_RATIO 2.37f  // try: 1.618, 1.73, 2.37, 3.14

const float fmRatios[8] = {
  1.41f,   // √2 - classic bell
  1.618f,  // golden ratio - metallic
  1.73f,   // √3
  2.11f,   // gong-like
  2.37f,   // inharmonic 5th
  2.81f,   // harsh metallic
  3.14f,   // π - chaotic
  4.0f,    // hard clang
};


void updateFMnoiseParams()
{
  // pot1 → base frequency (both oscillators scale together)
  float baseFreq = FM_FREQ_MIN + (FM_FREQ_MAX - FM_FREQ_MIN) * pot1_norm;
  fmPhaseInc1 = (uint32_t)(baseFreq * 268435.456f);

  // apply current ratio to osc2
  float ratio = fmRatios[fmCurrentRatioIdx];
  fmPhaseInc2 = (uint32_t)(baseFreq * ratio * 268435.456f);

  // pot2 → coincidence clock speed
  float clockFreq = FM_CLOCK_MIN_HZ + (FM_CLOCK_MAX_HZ - FM_CLOCK_MIN_HZ) * pot2_norm;
  fmClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);

  // clock2 runs at irrational ratio of clock1 - never repeating coincidence
  fmClockPhaseInc2 = (uint32_t)(clockFreq * FM_CLOCK_RATIO * 268435.456f);
}

// --------------------------------------------------
// Noisegates Parameter Ranges
// --------------------------------------------------
#define NOISEGATE_FREQ_MIN   0.01f
#define NOISEGATE_FREQ_MAX   5.0f
#define RANDOM_WALK_STEP_MIN 1000.0f     // Phase increment step (was 0.01 Hz)
#define RANDOM_WALK_STEP_MAX 300000.0f   // Phase increment step (was 3 Hz)
#define RANDOM_WALK_RATE 8.0f  // Hz - how often to update the walk

void updateNoisegatesParams()
{
  // pot1 -> base frequency for both triangles (convert to phase increment)
  float freq = NOISEGATE_FREQ_MIN +
               (NOISEGATE_FREQ_MAX - NOISEGATE_FREQ_MIN) * pot1_norm;

  ng1PhaseInc = (uint32_t)(freq * 268435.456f);
  ng2BasePhaseInc = ng1PhaseInc;  // Start with same base

  // pot2 -> random walk step size (in phase increment units)
  float stepSize = RANDOM_WALK_STEP_MIN +
                   (RANDOM_WALK_STEP_MAX - RANDOM_WALK_STEP_MIN) * pot2_norm;
  randomWalkStep = (int32_t)stepSize;

  // Update the random walk update period
  walkUpdatePeriod = SAMPLE_RATE / RANDOM_WALK_RATE;
}

// --------------------------------------------------
// Generative Waveform Parameter Ranges (shared playback speed for all 3)
// --------------------------------------------------
#define PLAYBACK_SPEED_MIN 0.01f    // very slow playback
#define PLAYBACK_SPEED_MAX 2.0f     // fast playback

void updateGenerativeWaveform1Params()
{
  // pot1 -> playback speed (shared by all variants)
  playbackSpeed = PLAYBACK_SPEED_MIN +
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;

  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  //gw1SilenceProb = 1.0f - pot2_norm;
  gw1SilenceProb = 0.2f - (pot2_norm * 0.2f);  // CCW=0.4 chance for silence, CW=0.0
}

void updateGenerativeWaveform2Params()
{
  // pot1 -> playback speed (shared)
  playbackSpeed = PLAYBACK_SPEED_MIN +
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;

  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  gw2SilenceProb = 1.0f - pot2_norm;
}

void updateGenerativeWaveform3Params()
{
  // pot1 -> playback speed (shared)
  playbackSpeed = PLAYBACK_SPEED_MIN +
                  (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;

  // pot2 -> silence chunk probability (INVERTED)
  // CCW (0.0) = all silence, CW (1.0) = no silence
  //gw3SilenceProb = 1.0f - pot2_norm;
   gw3SilenceProb = 0.1f - (pot2_norm * 0.1f);  // CCW=0.1 chance for silence, CW=0.0
}

// --------------------------------------------------
// Generative Waveform 4 Parameters (Random Jump Glitch)
// --------------------------------------------------
#define GW4_JUMP_FREQ 3.0f  // Fixed jump frequency (3 Hz)

void updateGenerativeWaveform4Params()
{
  // pot1 -> playback speed
  gw4PlaybackSpeed = PLAYBACK_SPEED_MIN +
                     (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;

  // pot2 -> silence injection probability (INVERTED)
  // CCW (0.0) = all silence (muted)
  // CW (1.0) = no silence (continuous noise)
  gw4SilenceProb = 1.0f - pot2_norm;

  // Fixed jump frequency
  gw4JumpPeriod = (uint32_t)(SAMPLE_RATE / GW4_JUMP_FREQ);
}

// --------------------------------------------------
// Generative Waveform 5 Parameters (Wandering Window)
// --------------------------------------------------
#define GW5_WALK_FREQ_MIN 0.01f    // 0.01 Hz (very slow wander)
#define GW5_WALK_FREQ_MAX 50.0f    // 50 Hz (rapid wander)

void updateGenerativeWaveform5Params()
{
  // pot1 -> playback speed
  gw5PlaybackSpeed = PLAYBACK_SPEED_MIN +
                     (PLAYBACK_SPEED_MAX - PLAYBACK_SPEED_MIN) * pot1_norm;

  // pot2 -> random walk frequency
  float walkFreq = GW5_WALK_FREQ_MIN +
                   (GW5_WALK_FREQ_MAX - GW5_WALK_FREQ_MIN) * pot2_norm;
  gw5WalkPeriod = (uint32_t)(SAMPLE_RATE / walkFreq);
}

// --------------------------------------------------
// Generative Waveform 6 Parameters (Manual Window with Spray)
// --------------------------------------------------
void updateGenerativeWaveform6Params()
{
  // pot1 -> window start position (0 to WAVEFORM_SIZE)
  gw6WindowStart = (uint16_t)(pot1_norm * (WAVEFORM_SIZE - 1));

  // pot2 -> window size (1 to full buffer)
  gw6WindowSize = 1 + (uint16_t)(pot2_norm * (WAVEFORM_SIZE - 1));

  // Ensure window doesn't exceed buffer
  if (gw6WindowStart + gw6WindowSize > WAVEFORM_SIZE) {
    gw6WindowSize = WAVEFORM_SIZE - gw6WindowStart;
  }
}

// --------------------------------------------------
// Generative Waveform 7 Parameters (Noise or Saw Window)
// --------------------------------------------------
#define GW7_SPEED_MIN 0.1f
#define GW7_SPEED_MAX 4.0f
#define GW7_WALK_RATE_MIN 0.5f
#define GW7_WALK_RATE_MAX 20.0f

void updateGenerativeWaveform7Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw7PlaybackSpeed = GW7_SPEED_MIN + (GW7_SPEED_MAX - GW7_SPEED_MIN) * pot1_norm;

  // pot2 -> window walk rate (same as GW5)
  float walkRate = GW7_WALK_RATE_MIN + (GW7_WALK_RATE_MAX - GW7_WALK_RATE_MIN) * pot2_norm;
  gw7WalkPeriod = (uint32_t)(SAMPLE_RATE / walkRate);
}

// --------------------------------------------------
// Generative Waveform 17 Parameters (Harmonic Drone Builder)
// --------------------------------------------------
void updateGenerativeWaveform17Params()
{
  // Playback speed is fixed at 0.3×
  // Regen interval is fixed at 3 seconds

  // pot1 -> octave multiplier (5 positions)
  // New chunks slide in at shifted octave!
  if (pot1_norm < 0.2f) {
    gw17OctaveMult = 0.5f;      // Down octave
  } else if (pot1_norm < 0.4f) {
    gw17OctaveMult = 1.0f;      // Original pitch
  } else if (pot1_norm < 0.6f) {
    gw17OctaveMult = 2.0f;      // Up octave
  } else if (pot1_norm < 0.8f) {
    gw17OctaveMult = 4.0f;      // Up 2 octaves
  } else {
    gw17OctaveMult = 8.0f;      // Up 3 octaves
  }

  // pot2 -> stutter/pause probability (INVERTED for consistency)
  // CCW (0.0) = max stutter (GW17_STUTTER_MAX_PROB, adjust in algos.h!)
  // CW (1.0) = no stutter, smooth drones
  gw17StutterProb = GW17_STUTTER_MAX_PROB * (1.0f - pot2_norm);
}

// --------------------------------------------------
// Generative Waveform 18 Parameters (BitBend Quad)
// --------------------------------------------------
#define GW18_SPEED_MIN 0.1f
#define GW18_SPEED_MAX 4.0f
#define GW18_CLOCK_MIN 0.5f
#define GW18_CLOCK_MAX 20.0f

void updateGenerativeWaveform18Params()
{
  // pot1 -> playback speed
  gw18PlaybackSpeed = GW18_SPEED_MIN + (GW18_SPEED_MAX - GW18_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock rate (Bernoulli gated 50/50)
  float clockFreq = GW18_CLOCK_MIN + (GW18_CLOCK_MAX - GW18_CLOCK_MIN) * pot2_norm;
  gw18BitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// Generative Waveform 8 Parameters (BitBender)
// --------------------------------------------------
#define GW8_SPEED_MIN 0.1f
#define GW8_SPEED_MAX 4.0f
#define GW8_BIT_CLOCK_MIN 0.5f   // Slow bit manipulation changes
#define GW8_BIT_CLOCK_MAX 20.0f  // Fast chaotic bit changes

void updateGenerativeWaveform8Params()
{
  // pot1 -> playback speed (0.1x to 4x) - consistent with other wavetables!
  gw8PlaybackSpeed = GW8_SPEED_MIN + (GW8_SPEED_MAX - GW8_SPEED_MIN) * pot1_norm;

  // pot2 -> bit manipulation clock rate (how fast the glitching changes)
  float bitClockFreq = GW8_BIT_CLOCK_MIN + (GW8_BIT_CLOCK_MAX - GW8_BIT_CLOCK_MIN) * pot2_norm;
  gw8BitClockPhaseInc = (uint32_t)(bitClockFreq * 268435.456f);
}

// --------------------------------------------------
// Generative Waveform 9 Parameters (BitBend Sparse)
// --------------------------------------------------
#define GW9_SPEED_MIN 0.1f
#define GW9_SPEED_MAX 4.0f

void updateGenerativeWaveform9Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw9PlaybackSpeed = GW9_SPEED_MIN + (GW9_SPEED_MAX - GW9_SPEED_MIN) * pot1_norm;

  // pot2 -> bit manipulation mode (0-3)
  // CCW = SET_0 (loops), 25% = SET_1 (jumps), 50% = XOR (chaos), CW = HOLD (freeze)
  gw9BitMode = (uint8_t)(pot2_norm * 3.999f);  // 0-3
  if (gw9BitMode > 3) gw9BitMode = 3;
}

// --------------------------------------------------
// Generative Waveform 10 Parameters (BitBend Dual)
// --------------------------------------------------
#define GW10_SPEED_MIN 0.1f
#define GW10_SPEED_MAX 4.0f
#define GW10_CLOCK_MIN 0.5f
#define GW10_CLOCK_MAX 20.0f

void updateGenerativeWaveform10Params()
{
  // pot1 -> playback speed (0.1x to 4x)
  gw10PlaybackSpeed = GW10_SPEED_MIN + (GW10_SPEED_MAX - GW10_SPEED_MIN) * pot1_norm;

  // pot2 -> bit manipulation clock rate (goes through 50/50 Bernoulli gate)
  float clockFreq = GW10_CLOCK_MIN + (GW10_CLOCK_MAX - GW10_CLOCK_MIN) * pot2_norm;
  gw10BitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Freeze Parameters (GW11)
// --------------------------------------------------
#define BB_FREEZE_SPEED_MIN 0.1f
#define BB_FREEZE_SPEED_MAX 4.0f
#define BB_FREEZE_CLOCK_MIN 0.5f
#define BB_FREEZE_CLOCK_MAX 20.0f

void updateGenerativeWaveform11Params()
{
  // pot1 -> playback speed
  bbFreezePlaybackSpeed = BB_FREEZE_SPEED_MIN + (BB_FREEZE_SPEED_MAX - BB_FREEZE_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock (fixed 23Hz is at ~pot2=mid, but user can adjust)
  float clockFreq = BB_FREEZE_CLOCK_MIN + (BB_FREEZE_CLOCK_MAX - BB_FREEZE_CLOCK_MIN) * pot2_norm;
  bbFreezeBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Ping Parameters (GW12)
// --------------------------------------------------
#define BB_PING_SPEED_MIN 0.1f
#define BB_PING_SPEED_MAX 4.0f
#define BB_PING_CLOCK_MIN 0.5f
#define BB_PING_CLOCK_MAX 20.0f

void updateGenerativeWaveform12Params()
{
  // pot1 -> playback speed
  bbPingPlaybackSpeed = BB_PING_SPEED_MIN + (BB_PING_SPEED_MAX - BB_PING_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock rate (Bernoulli gated in ISR)
  float clockFreq = BB_PING_CLOCK_MIN + (BB_PING_CLOCK_MAX - BB_PING_CLOCK_MIN) * pot2_norm;
  bbPingBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Mirror Parameters (GW13)
// --------------------------------------------------
#define BB_MIRROR_SPEED_MIN 0.1f
#define BB_MIRROR_SPEED_MAX 4.0f
#define BB_MIRROR_CLOCK_MIN 0.5f
#define BB_MIRROR_CLOCK_MAX 20.0f

void updateGenerativeWaveform13Params()
{
  // pot1 -> playback speed
  bbMirrorPlaybackSpeed = BB_MIRROR_SPEED_MIN + (BB_MIRROR_SPEED_MAX - BB_MIRROR_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_MIRROR_CLOCK_MIN + (BB_MIRROR_CLOCK_MAX - BB_MIRROR_CLOCK_MIN) * pot2_norm;
  bbMirrorBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Triple Parameters (GW14)
// --------------------------------------------------
#define BB_TRIPLE_SPEED_MIN 0.1f
#define BB_TRIPLE_SPEED_MAX 4.0f
#define BB_TRIPLE_CLOCK_MIN 0.5f
#define BB_TRIPLE_CLOCK_MAX 20.0f

void updateGenerativeWaveform14Params()
{
  // pot1 -> playback speed
  bbTriplePlaybackSpeed = BB_TRIPLE_SPEED_MIN + (BB_TRIPLE_SPEED_MAX - BB_TRIPLE_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_TRIPLE_CLOCK_MIN + (BB_TRIPLE_CLOCK_MAX - BB_TRIPLE_CLOCK_MIN) * pot2_norm;
  bbTripleBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Sweep Parameters (GW15)
// --------------------------------------------------
#define BB_SWEEP_SPEED_MIN 0.1f
#define BB_SWEEP_SPEED_MAX 4.0f
#define BB_SWEEP_CLOCK_MIN 0.5f
#define BB_SWEEP_CLOCK_MAX 20.0f

void updateGenerativeWaveform15Params()
{
  // pot1 -> playback speed
  bbSweepPlaybackSpeed = BB_SWEEP_SPEED_MIN + (BB_SWEEP_SPEED_MAX - BB_SWEEP_SPEED_MIN) * pot1_norm;

  // pot2 -> base bit clock rate (clock1 is 3x slower, clock2 is full speed)
  float clockFreq = BB_SWEEP_CLOCK_MIN + (BB_SWEEP_CLOCK_MAX - BB_SWEEP_CLOCK_MIN) * pot2_norm;
  bbSweepBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// BitBend Triple B Parameters (GW16)
// --------------------------------------------------
#define BB_TRIPLEB_SPEED_MIN 0.1f
#define BB_TRIPLEB_SPEED_MAX 4.0f
#define BB_TRIPLEB_CLOCK_MIN 0.5f
#define BB_TRIPLEB_CLOCK_MAX 20.0f

void updateGenerativeWaveform16Params()
{
  // pot1 -> playback speed
  bbTripleBPlaybackSpeed = BB_TRIPLEB_SPEED_MIN + (BB_TRIPLEB_SPEED_MAX - BB_TRIPLEB_SPEED_MIN) * pot1_norm;

  // pot2 -> bit clock rate (Bernoulli gated)
  float clockFreq = BB_TRIPLEB_CLOCK_MIN + (BB_TRIPLEB_CLOCK_MAX - BB_TRIPLEB_CLOCK_MIN) * pot2_norm;
  bbTripleBBitClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);
}

// --------------------------------------------------
// Random Triangle Parameter Ranges (Bank 4 - Blips)
// --------------------------------------------------
#define RANDOM_TRI_TRIGGER_MIN 0.05f   // 50ms between triggers (fast)
#define RANDOM_TRI_TRIGGER_MAX 0.5f    // 500ms between triggers (slow)
#define RANDOM_TRI_DECAY_MIN 0.005f    // 5ms decay (fast)
#define RANDOM_TRI_DECAY_MAX 0.6f      // 600ms decay (slow)

void updateRandomTriangleParams()
{
  // pot1 -> trigger rate (REVERSED: pot full CW = fast, CCW = slow)
  float triggerTime = RANDOM_TRI_TRIGGER_MIN +
                      (RANDOM_TRI_TRIGGER_MAX - RANDOM_TRI_TRIGGER_MIN) * (1.0f - pot1_norm);
  randomTriTriggerPeriod = (uint32_t)(triggerTime * SAMPLE_RATE);

  // pot2 -> envelope decay time (5ms - 600ms)
  // Map decay time to coefficient using simple approximation (NO exp() needed!)
  // Longer decay = coefficient closer to 1.0
  float decayTime = RANDOM_TRI_DECAY_MIN +
                    (RANDOM_TRI_DECAY_MAX - RANDOM_TRI_DECAY_MIN) * pot2_norm;

  // Simple coefficient calculation
  // For decay to reach ~0.001 in decayTime seconds:
  // coeff = 1 - (3 / (decayTime * SAMPLE_RATE))
  // Clamped to ensure 0 < coeff < 1
  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;          // Minimum (fastest decay)
  if (coeff > 0.9999f) coeff = 0.9999f;    // Maximum (slowest decay)

  randomTriEnvDecay = coeff;
}

// --------------------------------------------------
// Saw Clicks Parameter Ranges (Bank 3 - Blips)
// --------------------------------------------------
#define SAW_FREQ_MIN 0.001f
#define SAW_FREQ_MAX 20.0f

void updateSawClicksParams()
{
  // pot1 -> sawtooth 1 frequency (0.001 - 20 Hz)
  float freq1 = SAW_FREQ_MIN + (SAW_FREQ_MAX - SAW_FREQ_MIN) * pot1_norm;

  // pot2 -> sawtooth 2 frequency (0.001 - 20 Hz)
  float freq2 = SAW_FREQ_MIN + (SAW_FREQ_MAX - SAW_FREQ_MIN) * pot2_norm;

  // Convert frequency to 32-bit phase increment
  // phaseInc = freq * (2^32 / sampleRate)
  // For 16kHz: phaseInc = freq * 268435.456
  sawClick1PhaseInc = (uint32_t)(freq1 * 268435.456f);
  sawClick2PhaseInc = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Fast Triangle Parameter Ranges (Bank 3 - Blips)
// --------------------------------------------------
#define FAST_TRI_FREQ_MIN 11.0f
#define FAST_TRI_FREQ_MAX 6000.0f
#define FAST_TRI_DECAY_MIN 0.0074f   // 7.4ms
#define FAST_TRI_DECAY_MAX 0.7f      // 700ms

void updateFastTriangleParams()
{
  // pot1 -> triangle frequency (20 - 6000 Hz) - convert to phase increment
  float freq = FAST_TRI_FREQ_MIN +
               (FAST_TRI_FREQ_MAX - FAST_TRI_FREQ_MIN) * pot1_norm;
  fastTriPhaseInc = (uint32_t)(freq * 268435.456f);

  // pot2 -> envelope decay (7.4ms - 700ms)
  float decayTime = FAST_TRI_DECAY_MIN +
                    (FAST_TRI_DECAY_MAX - FAST_TRI_DECAY_MIN) * pot2_norm;

  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;
  if (coeff > 0.9999f) coeff = 0.9999f;

  fastTriEnvDecay = coeff;

  // Trigger period: ~960 BPM = 16 beats/sec = 0.0625s between triggers
  fastTriTriggerPeriod = SAMPLE_RATE / 16;
}

// --------------------------------------------------
// Harmonic Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define HARM_TRI_FREQ_MIN 50.0f
#define HARM_TRI_FREQ_MAX 400.0f
#define HARM_TRI_LFO_MIN 0.001f   // Very slow (half a minute period)
#define HARM_TRI_LFO_MAX 10.0f    // Medium fast

void updateHarmonicTrisParams()
{
  // pot1 -> base frequency (50-400 Hz)
  float baseFreq = HARM_TRI_FREQ_MIN +
                   (HARM_TRI_FREQ_MAX - HARM_TRI_FREQ_MIN) * pot1_norm;

  harmTriPhaseInc1 = (uint32_t)(baseFreq * 268435.456f);
  harmTriPhaseInc2 = (uint32_t)(baseFreq * 3.0f * 268435.456f);  // 3x harmonic
  harmTriPhaseInc3 = (uint32_t)(baseFreq * 4.0f * 268435.456f);  // 4x harmonic

  // pot2 -> LFO speed with EXPONENTIAL scaling
  // This keeps the slow range accessible across most of the pot's travel
  float expPot = pot2_norm * pot2_norm * pot2_norm;  // Cube for exponential curve
  float lfoFreq = HARM_TRI_LFO_MIN +
                  (HARM_TRI_LFO_MAX - HARM_TRI_LFO_MIN) * expPot;

  harmTriLFOInc1 = (uint32_t)(lfoFreq * 268435.456f);              // Osc 1: base rate
  harmTriLFOInc2 = (uint32_t)(lfoFreq * 1.618f * 268435.456f);    // Osc 2: Golden ratio
  harmTriLFOInc3 = (uint32_t)(lfoFreq * 1.732f * 268435.456f);    // Osc 3: √3 ratio
}

// --------------------------------------------------
// Phrygian Tri Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define PHRYG_ROOT_MIN 50.0f
#define PHRYG_ROOT_MAX 200.0f
#define PHRYG_BURST_MIN 0.5f   // Slow burst modulation
#define PHRYG_BURST_MAX 8.0f   // Fast burst modulation

void updatePhrygianTriParams()
{
  // pot1 -> burst modulation speed (affects trigger irregularity) - SWAPPED!
  float burstFreq = PHRYG_BURST_MIN +
                    (PHRYG_BURST_MAX - PHRYG_BURST_MIN) * pot1_norm;
  phrygTriBurstModInc = (uint32_t)(burstFreq * 268435.456f);

  // pot2 -> root frequency - SWAPPED!
  float rootFreq = PHRYG_ROOT_MIN +
                   (PHRYG_ROOT_MAX - PHRYG_ROOT_MIN) * pot2_norm;

  // Apply current scale position
  float currentFreq = rootFreq * phrygianRatios[phrygTriScalePos];
  phrygTriPhaseInc = (uint32_t)(currentFreq * 268435.456f);

  // Base trigger rate from pot2 (slower = more space between bursts)
  phrygTriTriggerPeriod = SAMPLE_RATE / (uint32_t)(burstFreq * 2.0f);
}

// --------------------------------------------------
// Ring Mod Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define RING_FREQ1_MIN 15.0f
#define RING_FREQ1_MAX 2000.0f
#define RING_FREQ2_MIN 15.0f
#define RING_FREQ2_MAX 2000.0f

void updateRingModParams()
{
  // pot1 -> oscillator 1 frequency
  float freq1 = RING_FREQ1_MIN +
                (RING_FREQ1_MAX - RING_FREQ1_MIN) * pot1_norm;
  ringModPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> oscillator 2 frequency
  float freq2 = RING_FREQ2_MIN +
                (RING_FREQ2_MAX - RING_FREQ2_MIN) * pot2_norm;
  ringModPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Two Saws Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define TWO_SAWS_FREQ_MIN 15.0f
#define TWO_SAWS_FREQ_MAX 850.0f

void updateTwoSawsParams()
{
  // pot1 -> saw 1 frequency
  float freq1 = TWO_SAWS_FREQ_MIN + (TWO_SAWS_FREQ_MAX - TWO_SAWS_FREQ_MIN) * pot1_norm;
  twoSawsPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> saw 2 frequency
  float freq2 = TWO_SAWS_FREQ_MIN + (TWO_SAWS_FREQ_MAX - TWO_SAWS_FREQ_MIN) * pot2_norm;
  twoSawsPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Noise OR Square Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define NOISE_OR_SQ_SH_FREQ_MIN 15.0f
#define NOISE_OR_SQ_SH_FREQ_MAX 116.0f
#define NOISE_OR_SQ_FREQ_MIN 35.0f
#define NOISE_OR_SQ_FREQ_MAX 5000.0f

void updateNoiseOrSquareParams()
{
  // pot1 -> noise S&H frequency (converted to sample period)
  float shFreq = NOISE_OR_SQ_SH_FREQ_MIN +
                 (NOISE_OR_SQ_SH_FREQ_MAX - NOISE_OR_SQ_SH_FREQ_MIN) * pot1_norm;
  noiseOrSqSHPeriod = (uint32_t)(SAMPLE_RATE / shFreq);

  // pot2 -> square wave frequency
  float sqFreq = NOISE_OR_SQ_FREQ_MIN +
                 (NOISE_OR_SQ_FREQ_MAX - NOISE_OR_SQ_FREQ_MIN) * pot2_norm;
  noiseOrSqPhaseInc = (uint32_t)(sqFreq * 268435.456f);
}

// --------------------------------------------------
// Major Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define MAJOR_TRIS_LFO_MIN 0.08f
#define MAJOR_TRIS_LFO_MAX 2.0f

void updateMajorTrisParams()
{
  // pot1 -> LFO 1 frequency
  float lfo1Freq = MAJOR_TRIS_LFO_MIN +
                   (MAJOR_TRIS_LFO_MAX - MAJOR_TRIS_LFO_MIN) * pot1_norm;
  majorTrisLFO1PhaseInc = (uint32_t)(lfo1Freq * 268435.456f);

  // pot2 -> LFO 2 frequency
  float lfo2Freq = MAJOR_TRIS_LFO_MIN +
                   (MAJOR_TRIS_LFO_MAX - MAJOR_TRIS_LFO_MIN) * pot2_norm;
  majorTrisLFO2PhaseInc = (uint32_t)(lfo2Freq * 268435.456f);
}

// --------------------------------------------------
// Bernoulli Tris Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
void updateBernoulliTrisParams()
{
  // pot1 -> Bernoulli gate 1 probability (0.0 to 1.0)
  bernTrisProb1 = pot1_norm;

  // pot2 -> Bernoulli gate 2 probability (0.0 to 1.0)
  bernTrisProb2 = pot2_norm;
}

// --------------------------------------------------
// Pentatonic Blips Parameters (Bank 3 - Blips and Tones)
// --------------------------------------------------
#define PENTA_BLIPS_CLOCK_MIN 3.0f
#define PENTA_BLIPS_CLOCK_MAX 11.5f
#define PENTA_BLIPS_DECAY_MIN 0.001f   // 1ms
#define PENTA_BLIPS_DECAY_MAX 0.5f     // 500ms

void updatePentatonicBlipsParams()
{
  // pot1 -> clock frequency (3-11.5 Hz)
  float clockFreq = PENTA_BLIPS_CLOCK_MIN +
                    (PENTA_BLIPS_CLOCK_MAX - PENTA_BLIPS_CLOCK_MIN) * pot1_norm;
  pentaBlipsClockPhaseInc = (uint32_t)(clockFreq * 268435.456f);

  // pot2 -> envelope decay time (1ms - 500ms)
  float decayTime = PENTA_BLIPS_DECAY_MIN +
                    (PENTA_BLIPS_DECAY_MAX - PENTA_BLIPS_DECAY_MIN) * pot2_norm;

  // Calculate decay coefficient
  float coeff = 1.0f - (3.0f / (decayTime * SAMPLE_RATE));
  if (coeff < 0.9f) coeff = 0.9f;
  if (coeff > 0.9999f) coeff = 0.9999f;
  pentaBlipsEnvDecay = coeff;
}

// ==================================================
// BANK 4: LOGIC OPERATIONS PARAMETERS
// ==================================================

// --------------------------------------------------
// Three Cascaded Squares Parameters
// --------------------------------------------------
#define CASCADE_FREQ1 9.96f          // Fixed frequency for osc 1
#define CASCADE_FREQ2_MIN 0.6f
#define CASCADE_FREQ2_MAX 1024.0f
#define CASCADE_FREQ3_MIN 1.0f
#define CASCADE_FREQ3_MAX 1024.0f

void updateThreeCascadedSquaresParams()
{
  // Fixed osc 1 at 9.96 Hz
  logicCascadePhaseInc1 = (uint32_t)(CASCADE_FREQ1 * 268435.456f);

  // pot1 -> oscillator 2 frequency
  float freq2 = CASCADE_FREQ2_MIN +
                (CASCADE_FREQ2_MAX - CASCADE_FREQ2_MIN) * pot1_norm;
  logicCascadePhaseInc2 = (uint32_t)(freq2 * 268435.456f);

  // pot2 -> oscillator 3 frequency
  float freq3 = CASCADE_FREQ3_MIN +
                (CASCADE_FREQ3_MAX - CASCADE_FREQ3_MIN) * pot2_norm;
  logicCascadePhaseInc3 = (uint32_t)(freq3 * 268435.456f);
}

// --------------------------------------------------
// NOR Square Parameters
// --------------------------------------------------
#define NOR_FREQ1_MIN 0.8f
#define NOR_FREQ1_MAX 200.0f
#define NOR_FREQ2_MIN 0.73f
#define NOR_FREQ2_MAX 215.0f

void updateNorSquareParams()
{
  // pot1 -> oscillator 1 frequency
  float freq1 = NOR_FREQ1_MIN + (NOR_FREQ1_MAX - NOR_FREQ1_MIN) * pot1_norm;
  logicNorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> oscillator 2 frequency
  float freq2 = NOR_FREQ2_MIN + (NOR_FREQ2_MAX - NOR_FREQ2_MIN) * pot2_norm;
  logicNorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri OR Saw Parameters
// --------------------------------------------------
#define TRI_OR_FREQ1_MIN 4.5f
#define TRI_OR_FREQ1_MAX 1024.0f
#define TRI_OR_FREQ2_MIN 2.0f
#define TRI_OR_FREQ2_MAX 1024.0f

void updateTriOrSawParams()
{
  // pot1 -> triangle frequency
  float freq1 = TRI_OR_FREQ1_MIN +
                (TRI_OR_FREQ1_MAX - TRI_OR_FREQ1_MIN) * pot1_norm;
  logicTriOrPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> sawtooth frequency
  float freq2 = TRI_OR_FREQ2_MIN +
                (TRI_OR_FREQ2_MAX - TRI_OR_FREQ2_MIN) * pot2_norm;
  logicTriOrPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri NOR Tri Parameters
// --------------------------------------------------
#define TRI_NOR_FREQ1_MIN 4.0f
#define TRI_NOR_FREQ1_MAX 880.0f
#define TRI_NOR_FREQ2_MIN 15.0f
#define TRI_NOR_FREQ2_MAX 900.0f

void updateTriNorTriParams()
{
  // pot1 -> triangle 1 frequency
  float freq1 = TRI_NOR_FREQ1_MIN +
                (TRI_NOR_FREQ1_MAX - TRI_NOR_FREQ1_MIN) * pot1_norm;
  logicTriNorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> triangle 2 frequency
  float freq2 = TRI_NOR_FREQ2_MIN +
                (TRI_NOR_FREQ2_MAX - TRI_NOR_FREQ2_MIN) * pot2_norm;
  logicTriNorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Tri XOR Tri Parameters
// --------------------------------------------------
#define TRI_XOR_FREQ1_MIN 0.7f
#define TRI_XOR_FREQ1_MAX 220.0f
#define TRI_XOR_FREQ2_MIN 0.6f
#define TRI_XOR_FREQ2_MAX 440.0f

void updateTriXorTriParams()
{
  // pot1 -> triangle 1 frequency
  float freq1 = TRI_XOR_FREQ1_MIN +
                (TRI_XOR_FREQ1_MAX - TRI_XOR_FREQ1_MIN) * pot1_norm;
  logicTriXorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> triangle 2 frequency
  float freq2 = TRI_XOR_FREQ2_MIN +
                (TRI_XOR_FREQ2_MAX - TRI_XOR_FREQ2_MIN) * pot2_norm;
  logicTriXorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Square XNOR Square Parameters
// --------------------------------------------------
#define XNOR_FREQ1_MIN 0.5f
#define XNOR_FREQ1_MAX 440.0f
#define XNOR_FREQ2_MIN 0.6f
#define XNOR_FREQ2_MAX 150.0f

void updateSquareXnorSquareParams()
{
  // pot1 -> square 1 frequency
  float freq1 = XNOR_FREQ1_MIN + (XNOR_FREQ1_MAX - XNOR_FREQ1_MIN) * pot1_norm;
  logicXnorPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> square 2 frequency
  float freq2 = XNOR_FREQ2_MIN + (XNOR_FREQ2_MAX - XNOR_FREQ2_MIN) * pot2_norm;
  logicXnorPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Square NAND Square Parameters
// --------------------------------------------------
#define NAND_FREQ1_MIN 0.1f
#define NAND_FREQ1_MAX 50.0f
#define NAND_FREQ2_MIN 0.08f
#define NAND_FREQ2_MAX 45.0f

void updateSquareNandSquareParams()
{
  // pot1 -> square 1 frequency (extremely low to low)
  float freq1 = NAND_FREQ1_MIN + (NAND_FREQ1_MAX - NAND_FREQ1_MIN) * pot1_norm;
  logicNandPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> square 2 frequency (extremely low to low)
  float freq2 = NAND_FREQ2_MIN + (NAND_FREQ2_MAX - NAND_FREQ2_MIN) * pot2_norm;
  logicNandPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Noise NOR Noise Parameters
// --------------------------------------------------
#define NOISE_NOR_FREQ_MIN 5.0f
#define NOISE_NOR_FREQ_MAX 120.0f

void updateNoiseNorNoiseParams()
{
  // pot1 -> S&H rate 1 (5-120 Hz, converted to sample period)
  float freq1 = NOISE_NOR_FREQ_MIN + (NOISE_NOR_FREQ_MAX - NOISE_NOR_FREQ_MIN) * pot1_norm;
  noiseNorPeriod1 = (uint32_t)(SAMPLE_RATE / freq1);

  // pot2 -> S&H rate 2 (5-120 Hz, converted to sample period)
  float freq2 = NOISE_NOR_FREQ_MIN + (NOISE_NOR_FREQ_MAX - NOISE_NOR_FREQ_MIN) * pot2_norm;
  noiseNorPeriod2 = (uint32_t)(SAMPLE_RATE / freq2);
}

// --------------------------------------------------
// Square OR Square Parameters
// --------------------------------------------------
#define SQUARE_OR_FREQ_MIN 15.0f
#define SQUARE_OR_FREQ_MAX 850.0f

void updateSquareOrSquareParams()
{
  // pot1 -> square 1 frequency
  float freq1 = SQUARE_OR_FREQ_MIN + (SQUARE_OR_FREQ_MAX - SQUARE_OR_FREQ_MIN) * pot1_norm;
  logicSquareOrPhaseInc1 = (uint32_t)(freq1 * 268435.456f);

  // pot2 -> square 2 frequency
  float freq2 = SQUARE_OR_FREQ_MIN + (SQUARE_OR_FREQ_MAX - SQUARE_OR_FREQ_MIN) * pot2_norm;
  logicSquareOrPhaseInc2 = (uint32_t)(freq2 * 268435.456f);
}

// --------------------------------------------------
// Master Parameter Update (calls all individual updates)
// --------------------------------------------------
void updateAllParams()
{
  updateLatchedNoiseParams();
  updateDustParams();
  updateDustBurstParams();
  updateHighpassNoiseParams();
  updateHighNoiseRhythmParams();
  updateVinylCrackleParams();
  updateFMnoiseParams();
  updateNoisegatesParams();
  updateGenerativeWaveform1Params();
  updateGenerativeWaveform2Params();
  updateGenerativeWaveform3Params();
  updateGenerativeWaveform4Params();
  updateGenerativeWaveform5Params();
  updateGenerativeWaveform6Params();
  updateGenerativeWaveform7Params();
  updateGenerativeWaveform17Params();
  updateGenerativeWaveform18Params();
  updateGenerativeWaveform8Params();
  updateGenerativeWaveform9Params();
  updateGenerativeWaveform10Params();
  updateGenerativeWaveform11Params();
  updateGenerativeWaveform12Params();
  updateGenerativeWaveform13Params();
  updateGenerativeWaveform14Params();
  updateGenerativeWaveform15Params();
  updateGenerativeWaveform16Params();
  updateRandomTriangleParams();
  updateSawClicksParams();
  updateFastTriangleParams();
  updateHarmonicTrisParams();
  updatePhrygianTriParams();
  updateRingModParams();
  updateTwoSawsParams();
  updateNoiseOrSquareParams();
  updateMajorTrisParams();
  updateBernoulliTrisParams();
  updatePentatonicBlipsParams();
  updateThreeCascadedSquaresParams();
  updateNorSquareParams();
  updateTriOrSawParams();
  updateTriNorTriParams();
  updateTriXorTriParams();
  updateSquareXnorSquareParams();
  updateSquareNandSquareParams();
  updateNoiseNorNoiseParams();
  updateSquareOrSquareParams();

  // Future: Add new algorithm param updates here
}

#endif // PARAMS_H

uint32_t serviceSamples=0;
int16_t sample(int kind){if(gw3NeedsRegen&&kind==2){generateWaveform3();gw3NeedsRegen=false;}if(++serviceSamples>=uint32_t(kind==1?80000:kind==5?64000:32000)){serviceSamples=0;if(kind==0)generateWaveform1();if(kind==1)generateWaveform2();if(kind==4)generateWaveform5();if(kind==5)generateWaveform6();}switch(kind){
case 0:return generativeWaveform1();
case 1:return generativeWaveform2();
case 2:return generativeWaveform3();
case 3:return generativeWaveform4();
case 4:return generativeWaveform5();
case 5:return generativeWaveform6();
case 6:return generativeWaveform7();
case 7:return generativeWaveform17();
case 8:return generativeWaveform18();
case 9:return latchedNoise1();
case 10:return dustNoise();
case 11:return FMnoise();
case 12:return noisegates();
case 13:return sawClicks();
case 14:return noiseNorNoise();
case 15:return dustBurst();
case 16:return highpassNoise();
case 17:return vinylCrackleAlgo();
case 18:return generativeWaveform8();
case 19:return generativeWaveform9();
case 20:return generativeWaveform10();
case 21:return generativeWaveform11();
case 22:return generativeWaveform12();
case 23:return generativeWaveform13();
case 24:return generativeWaveform14();
case 25:return generativeWaveform15();
case 26:return generativeWaveform16();
case 27:return randomTriangle();
case 28:return harmonicTris();
case 29:return fastTriangle();
case 30:return phrygianTri();
case 31:return ringMod();
case 32:return noiseOrSquare();
case 33:return majorTris();
case 34:return bernoulliTris();
case 35:return pentatonicBlips();
case 36:return threeCascadedSquares();
case 37:return norSquare();
case 38:return triOrSaw();
case 39:return triNorTri();
case 40:return triXorTri();
case 41:return squareXnorSquare();
case 42:return squareNandSquare();
case 43:return twoSaws();
case 44:return squareOrSquare();
default:return 0;}}
void setup(int kind,uint32_t seed,float a,float b){rng=seed?seed:1;pot1_norm=a;pot2_norm=b;updateAllParams();if(kind==34){bernTrisPhaseInc1=uint32_t(220.f*268435.456f);bernTrisPhaseInc2=uint32_t(264.f*268435.456f);bernTrisEnv1=bernTrisEnv2=1;}switch(kind){
case 0:generateWaveform1();break;
case 1:generateWaveform2();break;
case 2:generateWaveform3();break;
case 3:generateWaveform4();break;
case 4:generateWaveform5();break;
case 5:generateWaveform6();break;
case 6:generateWaveform7();for(int i=0;i<WAVEFORM_SIZE;i++)if(waveformBuffer[i]!=0){gw7WindowStart=playbackPhase=i;break;}break;
case 7:generateWaveform17();break;
case 8:generateWaveform18();break;
case 18:generateWaveform8();break;
case 19:generateWaveform9();break;
case 20:generateWaveform10();break;
case 21:generateWaveform11();break;
case 22:generateWaveform12();break;
case 23:generateWaveform13();break;
case 24:generateWaveform14();break;
case 25:generateWaveform15();break;
case 26:generateWaveform16();break;
}}
};
#undef SAMPLE_RATE
#undef TOGGLE_SAMPLES
