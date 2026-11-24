// SPDX-License-Identifier: GPL-3.0-only
// TIME SHAPER: a one-to-one recreation of TIME-12 (C) 2025 tilr (tiagolr), GPL-3.0
// (https://github.com/tiagolr/time12). A delay whose read time follows a curve over
// one sync period: the delay line is exactly one period long and the curve's Y picks
// how far back to read (0 = now, 1 = a whole period ago), so flat steps repeat a
// slice (stutter), slope-1 ramps freeze, steeper or shallower ramps play the past at
// other speeds (tape stop, scratch, reverse, half or double speed). The curve engine
// (hold, curve, s-curve, pulse, wave, triangle, stairs, smooth stairs, half sine
// segments with global tension), the RC attack/release smoother, the cubic delay
// read with its 1.5 ms cosine anti-click crossfade, MIN/MAX scaling, PHASE, and the
// audio transient trigger (simple envelope or drums RMS detector, 5 ms lookahead,
// 50 ms cooldown) are ported as written.
// Changed or omitted: there is no drawing; PATTERN selects TIME-12's built-in preset
// curves (Presets.h, its "Empty" slots and "Load All" banks left out), parsed as the
// original does. Points with equal X keep their written order (stable sort). The
// Hz rate, MIDI trigger, pattern switching by MIDI notes, sidechain, trigger band
// filters, trigger offset, dual tension, sequencer, paint tools, CV/CC/MIDI outputs
// and display code are omitted; anti-noise is fixed at its default (Normal). The
// host play position is replaced by a beat count that restarts on transport Start,
// where the delay clears and the curve realigns to the downbeat as on host play. A
// period longer than the buffer (about 7.5 s) is clamped.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
namespace amalthea {
namespace {
const char*const patternNames[]={
 "STUTTER 1","STUTTER 2","STUTTER 3","STUTTER 4","STUTTER 5","STUTTER 6","STUTTER 7","STUTTER 8","STUTTER 9",
 "STUTTER 10","STUTTER 11","STUTTER 12","STUTTER 13","STUTTER 14","STUTTER 15","STAIRS 1","STAIRS 2","STAIRS 3",
 "STAIRS 4","STAIRS 5","STAIRS 6","STAIRS 7","GATED 1","GATED 2","SHUFFLE 1","SHUFFLE 2","SHUFFLE 3","SHUFFLE 4",
 "BASIC 2","BASIC 3","BASIC 4","BASIC 5","BASIC 6","BASIC 7","BASIC 8","BASIC 9","BASIC 10","BASIC 11","BASIC 12",
 "COMPLEX 1","COMPLEX 2","COMPLEX 3","COMPLEX 4","COMPLEX 5","COMPLEX 6","COMPLEX 7","COMPLEX 8","COMPLEX 9",
 "COMPLEX 10","COMPLEX 11","COMPLEX 12","CHAOS 1","CHAOS 2","CHAOS 3","CHAOS 4","CHAOS 5","CHAOS 6","CHAOS 7",
 "CHAOS 8","CHAOS 9","CHAOS 10","CHAOS 11","CHAOS 12",
};
// TIME-12 Presets.h point lists: x y tension type, repeated.
const char*const patternPoints[]={
 "0 0 0 0 0.625 0.125 0 0 0.75 0 0 0 0.8125 0.0625 0 0 0.875 0 -0.176 6 1 0.09375 0 0",
 "0 0 0 0 0.5 0.25 0 0 0.625 0 0 0 0.75 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.375 0 0 0 0.5 0.125 0 0 0.75 0 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.375 0.125 0 0 0.625 0 0 0 0.75 0.125 0 0 0.875 0.25 0 0 1 0 0 0",
 "0 0 0 0 0.125 0.125 0 0 0.25 0 0 0 0.375 0.125 0 0 0.5 0 0 0 0.625 0.125 0 0 0.75 0 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.0625 0 0.172 6 0.25 0.25 0 0 0.25 0 -0.238 6 0.5 0.21875 0 0 0.5 0 0 0 0.5625 0 0.178 6 0.75 0.25 0 0 0.75 0 0 0 0.78125 0 0.336 6 1 0.25 0 0",
 "0 0 0 0 0.375 0 0 1 0.5 0.25 0 0 0.5 0 0 0 0.625 0.125 0 0 0.75 0 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.375 0 0 0 0.4375 0.1875 0 0 0.625 0 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.375 0.125 0 0 0.5 0.5 0 0 0.75 0 0 0 0.875 0.125 0 0 0.9375 0.1875 0 0 0.96875 0.21875 0 0 1 0 0 0",
 "0 0 0 0 0.125 0.125 0 0 0.1875 0.1875 0 0 0.25 0 0 0 0.375 0.125 0 0 0.4375 0.1875 0 0 0.5625 0.3125 0 0 0.6875 0.4375 0 0 0.8125 0.5625 0 0 0.875 0.25 0 0 0.9375 0 0 0 1 0 0 0",
 "0 0 0 0 0.125 0.125 0 0 0.25 0 0 0 0.375 0.125 0 0 0.4375 0.1875 0 0 0.5625 0.0625 0 0 0.75 0 0 0 0.875 0.125 0 0 0.9375 0.4375 0 0 1 0 0 0",
 "0 0 0 0 0.375 0 0 1 0.46875 0.25 0 0 0.5 0 0 0 0.625 0.125 0 0 0.75 0 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.3125 0 -0.21 1 0.4375 0.1875 0 0 0.625 0 0 0 0.875 0.125 0.206 6 1 0.25 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.375 0.125 0 0 0.75 0 0 0 1 0 0 0",
 "0 0 0 0 0.0625 0.0625 0 0 0.125 0.125 0 0 0.25 0 0 0 0.3125 0.0625 0 0 0.375 0.125 0 0 0.5 0 0 0 0.5625 0.0625 0 0 0.625 0.125 0 0 0.75 0 0 0 0.8125 0.0625 0 0 0.875 0.125 0 0 1 0 0 0",
 "0 0 0 0 0.5 0.5 0 0 1 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.5 0.5 0 0 0.75 0.75 0 0 1 0 0 0",
 "0 0 0 0 0.125 0.125 -0.222 6 1 0.875 0 0",
 "0 0 0 0 0.0625 0.0625 -0.322 6 1 0.9375 0 0",
 "0 0 0 0 0.03125 0.03125 -0.322 0 1 0.0625 0 0",
 "0 0 0 0 0.015625 0.015625 -0.65 6 1 0.984375 0 0",
 "0 0 0 0 0.00983607 0.0102041 -0.922 6 1 0.991837 0 0",
 "0 0 0 0 0.5 0.125 0 0 0.75 0 0 0",
 "0 0 0 0 0.5 0.25 0 0 0.75 0 0 0 0.875 0.25 0 0",
 "0 0.25 0 0 0.25 0 0 0 0.5 0.5 0 0 0.75 0.25 0 0",
 "0 0.75 0 0 0.25 0.25 0 0 0.5 0.5 0 0 0.75 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.3125 0.1875 0 0 0.5625 0.25 0 0 0.625 0.125 0 0 0.75 0 0 0 0.9375 0.1875 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.3125 0.1875 0 0 0.5625 0.25 0 0 0.625 0.125 0 0 0.75 0 0 0 0.875 0.125 0 0",
 "0 0 0 0 0.3125 0.0625 0 0 0.375 0.125 0 0 0.4375 0.1875 0 0 0.5 0 0 0",
 "0 0 0 0 0.125 0.125 0 0 0.25 0 0 0 0.75 0 0 0 0.8125 0.0625 0 0 0.875 0.125 0 0 0.9375 0.1875 0 0",
 "0 0 0 0 0.4375 0.1875 0 0 0.5 0 0 0 0.5625 0.3125 0 0 0.625 0 0 0",
 "0 0 0 0 0.375 0.375 0 0 0.75 0 0 0 0.875 0.1875 0 0",
 "0 0 0 0 0.375 0.25 0 0 0.5 0 0 0 0.6875 0.4375 0 0 0.75 0.5 0 0 0.8125 0.5625 0 0 0.875 0 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.5 0 0 0 0.75 0 -0.178 6 0.875 0.09375 0 0 0.875 0 0 0",
 "0 0 0 0 0.125 0 -0.172 6 0.25 0.09375 0 0 0.25 0 0 0 0.75 0 0 1 1 0.125 0 0",
 "0 0 0 0 0.75 0 0 1 1 0.5 0 0",
 "0 0 0 0 0.5 0.25 0 0 0.75 0.375 0 0 0.875 0.125 0 0 1 0.5 0",
 "0 0 0 0 0.25 0 -0.338 1 0.5 0.0625 0 0 0.5 0 0 0 1 0 0 0",
 "0 0 0 0 0.75 0 -0.15 6 1 0.1875 0 0",
 "0 0 0 0 0.25 0 0 0 0.28125 0.03125 0 0 0.3125 0.0625 0 0 0.375 0.125 0 0 0.4375 0 0 0 0.625 0.375 0 0 0.6875 0.4375 0 0 0.71875 0.46875 0 0 0.75 0.5 0 0 0.75 0 0 0 0.875 0.1875 0 0 1 0 0 0",
 "0 0 0 0 0.3125 0 0 1 0.375 0.125 0 0 0.375 0.0625 0 1 0.4375 0.1875 0 0 0.4375 0 0 0 0.625 0.25 0 0 0.875 0 0 1 1 0.03125 0 0",
 "0 0 0 0.1875 0.1875 0 0 0.25 0 0 0 0.4375 0.1875 0 0 0.625 0.03125 -0.172 6 0.75 0.125 0 0 0.75 0 -0.264 1 1 0.0625 0 0",
 "0 0 0 0 0.25 0.25 0 0 0.4375 0.1875 0 0 0.625 0.625 0 0 0.75 0.5 0 0 0.8125 0.5625 0 0 0.84375 0.59375 0 0 0.875 0.625 0 0 0.9375 0 0 0 1 0 0 0",
 "0 0 0 1 0.25 0.125 0 0 0.25 0 0 0 0.375 0.03125 0 1 0.4375 0 0 0 0.5 0.03125 0 1 0.5625 0 0 0 0.625 0.625 0 1 0.75 0.6875 0 0 0.75 0 0 0 0.8125 0.0625 0 0 0.875 0.0625 0 0 0.875 0.125 0 0 0.9375 0.125 0 0 0.9375 0.1875 0 0 1 0.1875 0 0",
 "0 0 0 0 0.267213 0 0.178 6 0.3125 0.0625 0 0 0.332787 0.0632653 0.144 6 0.375 0.125 0 0 0.40625 0.15625 0 0 0.4375 0.1875 0 0 0.5 0.25 0 0 0.5625 0.3125 0 0 0.625 0 0 0 0.90625 0.03125 0 0 0.9375 0.0625 0 0 0.96875 0.09375 0 0 1 0.09375 0 0",
 "0 0 0 0 0.03125 0 -0.146 2 0.125 0.125 0 0 0.15625 0.125 -0.196 2 0.25 0.25 0 0 0.25 0 0 0 0.75 0 0 0 0.78125 0 -0.106 1 1 0.125 0 0",
 "0 0 -0.204 1 0.25 0.25 0 0 0.25 0 0 0 0.4375 0.1875 0 0 0.625 0.375 0 0 0.8125 0.5625 0 0 0.875 0.625 0 0",
 "0 0 -0.204 0 0.25 0.25 0 0 0.3125 0.1875 0 0 0.375 0.0625 0 0 0.4375 0.1875 0 0 0.5 0.5 0 0 0.5625 0.3125 0 0 0.625 0.5 0 0 0.75 0.75 0 0 0.8125 0.625 0 0 0.875 0.75 0 0 0.9375 0.6875 0 0",
 "0 0 -0.204 0 0.125 0.125 0 0 0.1875 0 0 0 0.3125 0.1875 0 0 0.5 0.5 0 0 0.5625 0.375 0 0 0.625 0.125 0 0 0.6875 0.6875 0 0 0.75 0.5 0 0 0.875 0.875 0 0 0.9375 0.75 0 0 0.96875 0.78125 0 0",
 "0 0 -0.204 0 0.375 0.375 0 0 0.5625 0.4375 0 0 0.625 0.375 0 0 0.75 0.5 0 0 0.8125 0.625 0 0 0.84375 0.65625 0 0 0.875 0.6875 0 0 0.90625 0.71875 0 0 0.9375 0.75 0 0 0.96875 0.78125 0 0",
 "0 0 -0.204 0 0.125 0.125 0 0 0.1875 0.1875 0 0 0.25 0 0 0 0.437705 0.185714 0 0 0.625205 0 0 1 0.74918 0.0625 0 0 0.74918 0 0 0 0.9375 0.6875 0 0",
 "0 0 -0.204 0 0.141053 0 0.19 6 0.186885 0.0612245 0 0 0.186885 0 0 0 0.25 0 -0.18 6 0.3125 0.046875 0 0 0.3125 0 0 0 0.453125 0 0.18 6 0.5 0.046875 0 0 0.5 0 0 0 0.5625 0 -0.18 6 0.625 0.046875 0 0 0.625 0 0 0 0.765625 0 0.234 6 0.875 0.125 0 0 0.90625 0.15625 0 0 0.9375 0.1875 0 0 0.96875 0.21875 0 0",
 "0 0 -0.204 0 0.25 0 0 1 0.5 0.125 0 0 0.5 0 0 1 0.625 0.25 0 0 0.625 0 0 0 0.71875 0.03125 0 0 0.75 0 0 0 0.78125 0.03125 0 0 0.8125 0 0 0 0.84375 0.03125 0 0 0.875 0 0 0 0.90625 0.03125 0 0 0.9375 0 0 0 0.96875 0.03125 0 0",
 "0 0 -0.204 0 0.125 0 -0.17 6 0.25 0.09375 0 0 0.25 0 0 0 0.40625 0 0.18 6 0.5 0.125 0 0 0.5 0 0 0 0.5625 0.03125 0 0 0.59375 0.0625 0 0 0.625 0 0 0 0.6875 0.03125 0 0 0.71875 0.0625 0 0 0.75 0 0 0 0.78125 0 0.236 6 1 0.09375 0 0",
 "0 0 0 1 0.25 0.0625 0 0 0.25 0 0 1 0.5 0.0625 0 0 0.5 0 0 1 0.5625 0.03125 0 1 0.5625 0 0 1 0.625 0.03125 0 1 0.625 0 0 1 0.6875 0.03125 0 1 0.6875 0 0 1 0.75 0.03125 0 1 0.75 0.125 0 1 0.8125 0.09375 0 1 0.8125 0.1875 0 1 0.875 0.15625 0 1 0.875 0.25 0 1 0.9375 0.21875 0 1 0.9375 0.3125 0 1 1 0.28125 0 0",
 "0 0 -0.256 1 0.5 0.25 0 0 0.625 0 0 0 0.8125 0 0 1 1 0.5 0 0",
 "0 0 -0.256 0 0.25 0.03125 0.41 4 0.5 0 0 0 0.765625 0.25 0 0 0.78125 0 0 0 0.796875 0.3125 0 0 0.8125 0.0625 0 0 0.828125 0.375 0 0 0.84375 0.125 0 0 0.859375 0.4375 0 0 0.875 0.1875 0 0 0.890625 0.5 0 0 0.90625 0.25 0 0 0.921875 0.578125 0 0 0.9375 0.3125 0 0 0.953125 0.625 0 0 0.96875 0.375 0 0 0.984375 0.6875 0 0 1 0.6875 0 0",
 "0 0 -0.256 0 0.3125 0 0.132 1 0.5 0.5 0 0 0.5625 0.5625 0 0 0.625 0.125 0 0 0.6875 0.6875 0 0 0.703125 0.703125 0 0 0.71875 0.71875 0 0 0.734375 0.734375 0 0 0.75 0.75 0 0 0.875 0.875 0 0 0.9375 0.875 0 0 0.953125 0.890625 0 0 0.96875 0.90625 0 0 0.984375 0.921875 0 0 1 0.9375 0 0",
 "0 0 -0.238 6 0.25 0.109375 0 0 0.25 0 -0.232 6 0.375 0.0625 0 0 0.375 0 0.454 1 0.5 0.125 0 0 0.625 0 -0.262 6 0.75 0.125 0 0 0.75 0 0 0 0.78125 0 0 1 0.8125 0.03125 0 0 0.8125 0 0 0 0.84375 0 0 1 0.875 0.03125 0 0 0.875 0 0 0 0.90625 0 0 1 0.9375 0.03125 0 0 0.9375 0 0 0 0.96875 0 0 1 1 0.03125 0 0",
 "0 0 -0.238 0 0.186066 0.188776 0 0 0.247541 0.25102 0 0 0.375205 0.373469 0 0 0.437705 0.311735 0 0 0.5 0.25 0 0 0.5625 0.1875 -0.29 6 0.765625 0 0 0 0.8125 0.0625 0 0 0.84375 0.09375 0 0 0.875 0.125 0 0 0.9375 0.1875 0 0",
 "0 0 -0.238 0 0.28125 0.03125 0 0 0.3125 0.0625 0 0 0.34375 0.09375 0 0 0.375 0.125 0 0 0.40625 0.15625 0 0 0.4375 0.1875 0 0 0.46875 0.21875 0 0 0.5 0 0 0 0.5625 0 -0.166 6 0.625 0.046875 0 0 0.625 0 0 0 0.78125 0 0 0 0.8125 0.015625 0 0 0.84375 0.03125 0 0 0.872131 0.0530612 0 0 0.904918 0.0755102 0 0 0.932787 0.104082 0 0 0.963934 0.144898 0 0",
 "0 0 -0.238 0 0.125 0.015625 0.258 4 0.1875 0 -0.27 4 0.25 0.03125 0 0 0.25 0 0 0 0.4375 0.015625 0.27 4 0.5 0 0 0 0.5625 0.03125 0.326 4 0.6875 0 0 0 0.75 0.015625 0.286 4 0.890625 0 0 0",
 "0 0 -0.238 0 0.25 0.1875 0 1 0.28125 0 0 1 0.28125 0.1875 0 1 0.3125 0 0 1 0.3125 0.1875 0 1 0.34375 0 0 1 0.34375 0.1875 0 1 0.375 0 0 1 0.4375 0 0 1 0.4375 0.0625 0 1 0.453125 0 0 1 0.453125 0.0625 0 1 0.46875 0 0 1 0.46875 0.0625 0 1 0.484375 0 0 1 0.484375 0.0625 0 1 0.5 0 0 1 0.5625 0.015625 0 1 0.5625 0 0 1 0.625 0.015625 0 1 0.625 0 0 1 0.6875 0.015625 0 1 0.6875 0 0 1 0.75 0.015625 0 1 0.75 0.125 0 1 0.78125 0.0625 0 1 0.78125 0.125 0 1 0.8125 0.0625 0 1 0.8125 0.125 0 1 0.84375 0.0625 0 1 0.84375 0.125 0 1 0.875 0.0625 0 1 0.875 0 0 1",
};
constexpr int PatternCount=int(std::size(patternNames));
static_assert(PatternCount==int(std::size(patternPoints)),"one point list per name");
const char*const triggerModes[]={"SYNC","AUDIO","DRUMS"};
const char*const retriggerModes[]={"ANY HIT","AFTER END","FREE RUN"};
const NativeParam params[]={
 {"PATTERN",0,NativeUnit::Choice,0,100,patternNames,PatternCount},
 {"RATE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(7)},
 {"PHASE",0,NativeUnit::Percent,0,0},
 {"MIN",0,NativeUnit::Percent,0,0},
 {"MAX",100,NativeUnit::Percent,100,100},
 {"ATTACK",0,NativeUnit::Percent,0,20},
 {"RELEASE",0,NativeUnit::Percent,0,20},
 {"TENSION",50,NativeUnit::Bipolar,40,60},
 {"TRIGGER",0,NativeUnit::Choice,0,0,triggerModes,3},
 {"RETRIGGER",0,NativeUnit::Choice,0,0,retriggerModes,3},
 {"THRESHOLD",50,NativeUnit::Percent,30,70},
 {"SENSITIVITY",50,NativeUnit::Percent,30,70},
};

enum PointType{Hold,Curve,SCurve,Pulse,Wave,Triangle,Stairs,SmoothSt,HalfSine};
struct Segment{double x1,x2,y1,y2,tension;int type;};
struct PPoint{double x,y,tension;int type;};
constexpr double Pi=3.14159265358979323846;
constexpr int Capacity=330000;               // ~7.5 s per channel (2.6 MB stereo)
constexpr int AnoiseSamples=int(1.5/1000.0*44100);
constexpr int LatencySamples=int(5/1000.0*44100);

// Pattern::buildSegments over Presets::parsePreset.
std::vector<Segment> buildSegments(const char*text){
 std::vector<PPoint> pts;std::istringstream iss{std::string(text)};double x,y,t;int type;
 while(iss>>x>>y>>t>>type)pts.push_back({x,y,t,type});
 std::stable_sort(pts.begin(),pts.end(),[](const PPoint&a,const PPoint&b){return a.x<b.x;});
 if(pts.empty()){pts.push_back({-1,0,0,1});pts.push_back({2,0,0,1});}
 else if(pts.size()==1){auto p=pts[0];pts.insert(pts.begin(),{-1,p.y,0,1});pts.push_back({2,p.y,0,1});}
 else{auto p1=pts.front(),p2=pts.back();pts.insert(pts.begin(),{p2.x-1,p2.y,p2.tension,p2.type});pts.push_back({p1.x+1,p1.y,p1.tension,p1.type});}
 std::vector<Segment> s;for(size_t i=0;i+1<pts.size();i++)s.push_back({pts[i].x,pts[i+1].x,pts[i].y,pts[i+1].y,pts[i].tension,pts[i].type});
 return s;
}

// Pattern segment evaluation (single tension multiplier).
struct Shape{
 double tmult=0;
 double power(const Segment&s,double&ten,bool clampFirst)const{
  bool rise=s.y1>s.y2;ten=s.tension+(rise?-tmult:tmult);
  if(clampFirst){ten=std::clamp(ten,-1.0,1.0);ten*=-1;}else{ten*=-1;ten=std::clamp(ten,-1.0,1.0);}
  return std::pow(1.1,std::fabs(ten*50));
 }
 double curve(const Segment&s,double x)const{
  double ten,pwr=power(s,ten,true);if(s.x1==s.x2)return s.y2;
  if(ten>=0)return std::pow((x-s.x1)/(s.x2-s.x1),pwr)*(s.y2-s.y1)+s.y1;
  return -1*(std::pow(1-(x-s.x1)/(s.x2-s.x1),pwr)-1)*(s.y2-s.y1)+s.y1;
 }
 double scurve(const Segment&s,double x)const{
  double ten,pwr=power(s,ten,true);double xx=(s.x2+s.x1)/2,yy=(s.y2+s.y1)/2;if(s.x1==s.x2)return s.y2;
  if(x<xx&&ten>=0)return std::pow((x-s.x1)/(xx-s.x1),pwr)*(yy-s.y1)+s.y1;
  if(x<xx&&ten<0)return -1*(std::pow(1-(x-s.x1)/(xx-s.x1),pwr)-1)*(yy-s.y1)+s.y1;
  if(x>=xx&&ten>=0)return -1*(std::pow(1-(x-xx)/(s.x2-xx),pwr)-1)*(s.y2-yy)+yy;
  return std::pow((x-xx)/(s.x2-xx),pwr)*(s.y2-yy)+yy;
 }
 static double pulse(const Segment&s,double x){
  double t=std::max(std::floor(s.tension*s.tension*100),1.0);if(x==s.x2)return s.y2;
  double w=(s.x2-s.x1)/t,in=w==0?0:std::fmod(x-s.x1,w);
  return in<w/2?(s.tension<=0?s.y1:s.y2):(s.tension<=0?s.y2:s.y1);
 }
 static double wave(const Segment&s,double x){
  double t=2*std::floor(std::fabs(s.tension*s.tension*100)+1)-1,amp=(s.y2-s.y1)/2,v=s.y1+amp,f=t*2*Pi/(2*(s.x2-s.x1));
  return -amp*std::cos(f*(x-s.x1))+v;
 }
 static double triangle(const Segment&s,double x){
  double tt=2*std::floor(std::fabs(s.tension*s.tension*100)+1)-1.0,amp=s.y2-s.y1,t=(s.x2-s.x1)*2/tt;
  return amp*(2*std::fabs((x-s.x1)/t-std::floor(.5+(x-s.x1)/t)))+s.y1;
 }
 static double stairs(const Segment&s,double x){
  double t=std::max(std::floor(s.tension*s.tension*150),2.),step,index,ystep;
  if(s.tension<=0){step=(s.x2-s.x1)/t;index=std::floor((x-s.x1)/step);ystep=(s.y2-s.y1)/(t-1);}
  else{step=(s.x2-s.x1)/(t-1);index=std::ceil((x-s.x1)/step);ystep=(s.y2-s.y1)/t;}
  if(x==s.x2)return s.y2;
  return s.y1+index*ystep;
 }
 static double smoothStairs(const Segment&s,double x){
  double pwr=4,t=std::max(std::floor(s.tension*s.tension*150),1.0),gx=(s.x2-s.x1)/t,gy=(s.y2-s.y1)/t,i=std::floor((x-s.x1)/gx);
  double xx1=s.x1+gx*i,xx2=s.x1+gx*(i+1),xx=(xx1+xx2)/2,yy1=s.y1+gy*i,yy2=s.y1+gy*(i+1),yy=(yy1+yy2)/2;
  if(s.x1==s.x2)return s.y2;
  if(x<xx&&s.tension<=0)return std::pow((x-xx1)/(xx-xx1),pwr)*(yy-yy1)+yy1;
  if(x<xx&&s.tension>0)return -1*(std::pow(1-(x-xx1)/(xx-xx1),pwr)-1)*(yy-yy1)+yy1;
  if(x>=xx&&s.tension<=0)return -1*(std::pow(1-(x-xx)/(xx2-xx),pwr)-1)*(yy2-yy)+yy;
  return std::pow((x-xx)/(xx2-xx),pwr)*(yy2-yy)+yy;
 }
 double halfSine(const Segment&s,double x)const{
  double ten,pwr=power(s,ten,false);if(s.x1==s.x2)return s.y2;
  double t=(x-s.x1)/(s.x2-s.x1);t=.5-.5*std::cos(Pi*t);
  t=ten>=0?std::pow(t,pwr):1.0-std::pow(1.0-t,pwr);
  return t*(s.y2-s.y1)+s.y1;
 }
 double at(const std::vector<Segment>&segs,double x)const{
  int low=0,high=int(segs.size())-1;
  while(low<=high){
   int mid=(low+high)/2;const auto&s=segs[size_t(mid)];
   if(x<s.x1)high=mid-1;else if(x>s.x2)low=mid+1;
   else switch(s.type){
    case Hold:return s.y1;case Curve:return curve(s,x);case SCurve:return scurve(s,x);case Pulse:return pulse(s,x);
    case Wave:return wave(s,x);case Triangle:return triangle(s,x);case Stairs:return stairs(s,x);
    case SmoothSt:return smoothStairs(s,x);case HalfSine:return halfSine(s,x);default:return -1;
   }
  }
  return -1;
 }
};

// TIME-12 Delay: one channel, fixed capacity, variable length.
struct Delay{
 std::vector<float> buf;int size=2,curpos=0;double curval=0;
 Delay():buf(size_t(Capacity),0.f){}
 void resize(int n,bool clear){
  n=std::clamp(n,2,Capacity);if(n>size)std::fill(buf.begin()+size,buf.begin()+n,0.f);
  if(clear){curpos=0;curval=0;}else curpos%=n;
  size=n;
 }
 void clear(){std::fill(buf.begin(),buf.end(),0.f);}
 void write(double s){buf[size_t(curpos)]=float(s);if(++curpos==size)curpos=0;}
 double position(double delay)const{double pos=double(curpos)-delay;if(pos<0){pos+=double(size);if(pos==size)pos=0;}return pos;}
 // TIME-12 returns the last value when reading past the end; this reads the oldest frame
 // instead, so a full-period setting cannot hold a stale sample forever.
 double read(double delay){if(delay>=size)delay=size-1;curval=buf[size_t(int(position(delay)))];return curval;}
 double read3(double delay){
  if(delay>=size)delay=size-1;
  double pos=position(delay);int i=int(pos);double f=pos-i;
  double x0=i==0?buf[size_t(size-1)]:buf[size_t(i-1)],x1=buf[size_t(i)],x2=i==size-1?buf[0]:buf[size_t(i+1)],x3=i>=size-2?buf[size_t(i-size+2)]:buf[size_t(i+2)];
  double a3=f*f;a3-=1.0;a3*=1.0/6.0;double a2=(f+1.0)*.5,a0=a2-1.0,a1=a3*3.0;a2-=a1;a0-=a3;a1-=f;
  a0*=f;a1*=f;a2*=f;a3*=f;a1+=1.0;
  curval=a0*x0+a1*x1+a2*x2+a3*x3;return curval;
 }
};

// TIME-12 RCSmoother.
struct RCSmoother{
 double a=1,b=1,state=0;
 void setup(double ra,double rb,double srate){a=1.0/(ra*srate+1);b=1.0/(rb*srate+1);}
 double process(double in,bool useA){state+=(useA?a:b)*(in-state);return state;}
 void reset(double v){state=v;}
};

// TIME-12 Transient detector.
struct Transient{
 double attAlpha=std::exp(-1.0/(.1/1000.0*44100)),relAlpha=std::exp(-1.0/(100.0/1000.0*44100));
 double envelope=0,prevEnvelope=0,energy=0,prevEnergy=0;int cooldown=0,idx=0;
 std::vector<double> drums=std::vector<double>(size_t(44100*20/1000),0.0);
 void clear(){envelope=prevEnvelope=energy=prevEnergy=0;cooldown=0;idx=0;std::fill(drums.begin(),drums.end(),0.0);}
 void startCooldown(){cooldown=int(44100*50/1000.0);}
 bool detect(int algo,double x,double thres,double sense){
  double diff;
  if(algo==0){
   bool attack=std::fabs(x)>envelope;
   envelope=attack?attAlpha*envelope+(1.0-attAlpha)*std::fabs(x):relAlpha*envelope+(1.0-relAlpha)*std::fabs(x);
   diff=(envelope-prevEnvelope)*10;prevEnvelope=envelope;
  }else{
   double e=x*x;const auto n=drums.size();energy+=e-drums[size_t(idx)];drums[size_t(idx)]=e;idx=int((size_t(idx)+1)%n);
   double total=std::sqrt(std::max(0.0,energy)/double(n));diff=(total-prevEnergy)*75;prevEnergy=total;
  }
  if(cooldown)cooldown-=1;
  return !cooldown&&diff>sense&&std::fabs(x)>thres;
 }
};

class TimeShaperFx final:public NativeFx{
 std::vector<std::vector<Segment>> patterns;Shape shape;
 Delay delayL,delayR;RCSmoother value;Transient detL,detR;
 std::vector<double> latL=std::vector<double>(size_t(LatencySamples),0.0),latR=std::vector<double>(size_t(LatencySamples),0.0);
 int pattern=0,trigger=0,retrigger=0,ltrigger=-1,lrate=-1,latency=0,writepos=0,xfade=0,countdown=-1;
 double spb=22050,syncQN=4,phase=0,minY=0,maxY=1,threshold=.5,sense=.25,lspb=-1;
 double beatPos=0,xpos=0,ypos=0,lypos=0,xfadepos=0,trigpos=0,trigposSinceHit=1;bool audioTrigger=false;uint32_t epoch=0;
 double getY(double x)const{return minY+(maxY-minY)*shape.at(patterns[size_t(pattern)],x);}
 void resizeDelays(bool clear){int n=int(syncQN*spb);delayL.resize(n,clear);delayR.resize(n,clear);}
 void restartEnv(bool fromZero){
  if(fromZero)xpos=phase;
  else{xpos=beatPos/syncQN+phase;xpos-=std::floor(xpos);value.reset(getY(xpos));}
 }
 void onPlay(){
  delayL.clear();delayR.clear();std::fill(latL.begin(),latL.end(),0.0);std::fill(latR.begin(),latR.end(),0.0);writepos=0;
  audioTrigger=false;beatPos=0;trigpos=0;trigposSinceHit=1;countdown=-1;detL.clear();detR.clear();
  if(trigger==0||retrigger==2)restartEnv(false);
 }
 void env(double l,double r,float*out){
  delayL.write(l);delayR.write(r);double oL,oR;
  if(lypos==ypos){oL=delayL.read(1+ypos*delayL.size);oR=delayR.read(1+ypos*delayR.size);}
  else{oL=delayL.read3(1+ypos*delayL.size);oR=delayR.read3(1+ypos*delayR.size);}
  if(std::fabs(ypos-lypos)>1e-3){xfade=AnoiseSamples;xfadepos=1+lypos*delayL.size;}
  if(xfade>0){
   double fadeOut=.5*(1.0+std::cos(Pi*xfade/AnoiseSamples)),fadeIn=1.0-fadeOut,at=xfadepos+AnoiseSamples-xfade;
   oL=oL*fadeOut+delayL.read3(at)*fadeIn;oR=oR*fadeOut+delayR.read3(at)*fadeIn;xfade-=1;
  }
  out[0]=nativeClean(float(oL));out[1]=nativeClean(float(oR));lypos=ypos;
 }
public:
 TimeShaperFx(){patterns.reserve(size_t(PatternCount));for(auto*p:patternPoints)patterns.push_back(buildSegments(p));resizeDelays(true);}
 void configure(const uint8_t*v,float bpm)override{
  pattern=nativeChoice(v[0],PatternCount);int rate=nativeDivision(v[1]);syncQN=nativeDivisionBeats(rate);spb=nativeSamplesPerBeat(bpm);
  phase=v[2]*.01;minY=v[3]*.01;maxY=v[4]*.01;
  double atk=v[5]*.01,rel=v[6]*.01;atk*=atk;rel*=rel;value.setup(atk*.25,rel*.25,44100);
  shape.tmult=(v[7]*2-100)*.01;
  trigger=nativeChoice(v[8],3);retrigger=nativeChoice(v[9],3);threshold=v[10]*.01;sense=1.0-v[11]*.01;sense*=sense;
  if(trigger!=ltrigger){latency=trigger?LatencySamples:0;std::fill(latL.begin(),latL.end(),0.0);std::fill(latR.begin(),latR.end(),0.0);writepos=0;ltrigger=trigger;}
  if(trigger!=1&&trigger!=2)audioTrigger=false;
  if(rate!=lrate){resizeDelays(true);lrate=rate;lspb=spb;}
  else if(spb!=lspb){resizeDelays(false);lspb=spb;}
 }
 void clear()override{
  delayL.clear();delayR.clear();std::fill(latL.begin(),latL.end(),0.0);std::fill(latR.begin(),latR.end(),0.0);
  detL.clear();detR.clear();value.reset(0);xpos=ypos=lypos=0;xfade=0;delayL.curval=delayR.curval=0;
  audioTrigger=false;trigpos=0;trigposSinceHit=1;countdown=-1;writepos=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;onPlay();}   // downbeat
  const double beatsPerSample=1.0/spb;
  for(int i=0;i<frames;i++){
   double l=io[i*2],r=io[i*2+1];
   if(trigger==0){
    xpos=beatPos/syncQN+phase;xpos-=std::floor(xpos);
    double y=getY(xpos);ypos=value.process(y,y>ypos);env(l,r,io+i*2);
   }else{
    double dl=l,dr=r;
    if(latency){latL[size_t(writepos)]=l;latR[size_t(writepos)]=r;int readpos=(writepos+1)%latency;dl=latL[size_t(readpos)];dr=latR[size_t(readpos)];}
    int algo=trigger==2;
    if(detL.detect(algo,l,threshold,sense)||detR.detect(algo,r,threshold,sense)){detL.startCooldown();detR.startCooldown();countdown=std::max(0,latency);}
    bool hit=countdown==0,always=retrigger==2;
    double inc=beatsPerSample/syncQN;xpos+=inc;trigpos+=inc;trigposSinceHit+=inc;xpos-=std::floor(xpos);
    if(hit&&(always||retrigger!=1||trigposSinceHit>.98)){audioTrigger=!always;trigpos=0;trigposSinceHit=0;restartEnv(true);}
    if(!always){
     if(audioTrigger){if(trigpos>=1.0){audioTrigger=false;xpos=phase?phase:1.0;}}
     else xpos=phase?phase:1.0;
    }
    double y=getY(xpos);ypos=value.process(y,y>ypos);env(dl,dr,io+i*2);
    if(countdown>-1)countdown-=1;
    writepos=latency==0?0:(writepos+1)%latency;
   }
   beatPos+=beatsPerSample;
  }
 }
};
}
const NativeInfo timeShaperInfo{FxType::TimeShaper,"TIME SHAPER","TIME","TS",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeTimeShaper(){return std::make_unique<TimeShaperFx>();}
}
