// SPDX-License-Identifier: GPL-3.0-only
// GATE SHAPER: a one-to-one recreation of GATE-12 (C) 2025 tilr (tiagolr), GPL-3.0
// (https://github.com/tiagolr/gate12). A trance gate / volume shaper: a curve drawn
// over one sync period sets the gain (curve Y 0 = MAX, 1 = MIN), smoothed by an RC
// attack/release follower. The curve engine (hold, curve, s-curve, pulse, wave,
// triangle, stairs, smooth stairs, half sine segments with global tension), the RC
// smoother, MIN/MAX scaling, PHASE, the stereo phase offset, and the audio transient
// trigger (simple envelope or drums RMS detector, 5 ms lookahead, 50 ms cooldown, the
// default 5 ms ease-in-out anti-click tween before each restart) are ported as written.
// Changed or omitted: there is no drawing; PATTERN selects GATE-12's built-in preset
// curves (Presets.h, without its "Empty" and "Mute" slots or the "Init" triangle),
// parsed as the original does; points with equal X keep their written order (stable
// sort). The Hz rate, Free and MIDI triggers, pattern switching by MIDI notes, the
// sidechain, trigger band filters, trigger offset, band splitter, dual smooth/tension
// switches (attack and release are always separate, tension single), "always playing"
// and "ignore hits while playing" options, sequencer, paint tools, CV/CC/MIDI outputs
// and display code are omitted. The host play position is replaced by a beat count
// that restarts on transport Start, where the curve realigns to the downbeat as on
// host play.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
namespace amalthea {
namespace {
const char*const patternNames[]={"GATE 2","GATE 4","GATE 8","GATE 12","GATE 16","GATE 24","GATE 32","TRANCE 1","TRANCE 2","TRANCE 3","TRANCE 4","SAW 1","SAW 2","STEP 1","STEP 1 FADE IN","STEP 4 GATE","OFF BEAT","DYNAMIC 1/4","SWING","GATE OUT","GATE IN","SPEED UP","SPEED DOWN","END FADE","END GATE","TREMOLO SLOW","TREMOLO FAST","SIDECHAIN","DRUM LOOP","COPTER","AM","FADE IN","FADE OUT","FADE OUT IN"};
// GATE-12 Presets.h point lists (x y tension type, repeated; y 0 = full gain).
const char*const patternPoints[]={
 "0 0 0 0 0.25 1 0 0 0.5 0 0 0 0.75 1 0 0",
 "0 0 0 0 0.125 1 0 0 0.25 0 0 0 0.375 1 0 0 0.5 0 0 0 0.625 1 0 0 0.75 0 0 0 0.875 1 0 0",
 "0 0 0 0 0.0625 1 0 0 0.125 0 0 0 0.1875 1 0 0 0.25 0 0 0 0.3125 1 0 0 0.375 0 0 0 0.4375 1 0 0 0.5 0 0 0 0.5625 1 0 0 0.625 0 0 0 0.6875 1 0 0 0.75 0 0 0 0.8125 1 0 0 0.875 0 0 0 0.9375 1 0 0",
 "0 0 0 0 0.0416667 1 0 0 0.0833333 0 0 0 0.125 1 0 0 0.166667 0 0 0 0.208333 1 0 0 0.25 0 0 0 0.291667 1 0 0 0.333333 0 0 0 0.375 1 0 0 0.416667 0 0 0 0.458333 1 0 0 0.5 0 0 0 0.541667 1 0 0 0.583333 0 0 0 0.625 1 0 0 0.666667 0 0 0 0.708333 1 0 0 0.75 0 0 0 0.791667 1 0 0 0.833333 0 0 0 0.875 1 0 0 0.916667 0 0 0 0.958333 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0 0 0 0.09375 1 0 0 0.125 0 0 0 0.15625 1 0 0 0.1875 0 0 0 0.21875 1 0 0 0.25 0 0 0 0.28125 1 0 0 0.3125 0 0 0 0.34375 1 0 0 0.375 0 0 0 0.40625 1 0 0 0.4375 0 0 0 0.46875 1 0 0 0.5 0 0 0 0.53125 1 0 0 0.5625 0 0 0 0.59375 1 0 0 0.625 0 0 0 0.65625 1 0 0 0.6875 0 0 0 0.71875 1 0 0 0.75 0 0 0 0.78125 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.875 0 0 0 0.90625 1 0 0 0.9375 0 0 0 0.96875 1 0 0",
 "0 0 0.496 3 1 1 0 1 1 1 0 0",
 "0 0 0.572 3 1 1 0 1 1 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0 0 0 0.09375 1 0 0 0.125 0 0 0 0.21875 1 0 0 0.25 0 0 0 0.28125 1 0 0 0.3125 0 0 0 0.34375 1 0 0 0.375 0 0 0 0.46875 1 0 0 0.5 0 0 0 0.53125 1 0 0 0.5625 0 0 0 0.59375 1 0 0 0.625 0 0 0 0.71875 1 0 0 0.75 0 0 0 0.78125 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.875 0 0 0 0.96875 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0 0 0 0.09375 1 0 0 0.125 0 0 0 0.15625 1 0 0 0.1875 0 0 0 0.25 0 0 0 0.28125 1 0 0 0.3125 0 0 0 0.34375 1 0 0 0.375 0 0 0 0.40625 1 0 0 0.4375 0 0 0 0.46875 1 0 0 0.5 0 0 0 0.5625 0 0 0 0.59375 1 0 0 0.625 0 0 0 0.71875 1 0 0 0.75 0 0 0 0.78125 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.875 0 0 0 0.90625 1 0 0 0.9375 0 0 0 0.96875 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0 0 0 0.125 0 0 0 0.15625 1 0 0 0.1875 0 0 0 0.21875 1 0 0 0.25 0 0 0 0.28125 1 0 0 0.3125 0 0 0 0.34375 1 0 0 0.375 0 0 0 0.4375 0 0 0 0.46875 1 0 0 0.5 0 0 0 0.53125 1 0 0 0.5625 0 0 0 0.578125 1 0 0 0.59375 0 0 0 0.609375 1 0 0 0.625 0 0 0 0.71875 1 0 0 0.75 0 0 0 0.78125 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.875 0 0 0 0.90625 1 0 0 0.9375 0 0 0 0.96875 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0.75 0 0 0.078125 1 0 0 0.09375 0.25 0 0 0.109375 1 0 0 0.125 0 0 0 0.15625 1 0 0 0.1875 0.25 0 0 0.21875 1 0 0 0.25 0 0 0 0.34375 1 0 0 0.375 0.25 0 0 0.40625 1 0 0 0.4375 0 0 0 0.46875 1 0 0 0.5625 0.25 0 0 0.59375 1 0 0 0.625 0 0 0 0.65625 1 0 0 0.75 0 0 0 0.8125 1 0 0 0.875 0 0 0 0.890625 1 0 0 0.90625 0.25 0 0 0.921875 1 0 0 0.9375 0.5 0 0 0.953125 1 0 0 0.96875 0.75 0 0 0.984375 1 0 0",
 "0 1 0 1 0 0 0 1 0.0625 1 0 1 0.0625 0 0 1 0.125 1 0 1 0.125 0 0 1 0.1875 1 0 1 0.1875 0 0 1 0.25 1 0 1 0.25 0 0 1 0.3125 1 0 1 0.3125 0 0 1 0.375 1 0 1 0.375 0 0 1 0.5 1 0 1 0.5 0 0 1 0.53125 1 0 1 0.53125 0 0 1 0.5625 1 0 1 0.5625 0 0 1 0.625 1 0 1 0.625 0 0 1 0.6875 1 0 1 0.6875 0 0 1 0.75 1 0 1 0.75 0 0 1 0.875 1 0 1 0.875 0 0 1 0.9375 1 0 1 0.9375 0 0 1 1 1 0 1",
 "0 0 0 1 0.0625 1 0 1 0.0625 0.25 0 1 0.125 1 0 1 0.125 0 0 1 0.1875 1 0 1 0.1875 0.25 0 1 0.25 1 0 1 0.25 0 0 1 0.3125 1 0 1 0.3125 0.25 0 1 0.375 1 0 1 0.375 0 0 1 0.4375 1 0 1 0.4375 0.25 0 1 0.5 1 0 1 0.5 0 0 1 0.5625 1 0 1 0.5625 0.25 0 1 0.625 1 0 1 0.625 0 0 1 0.6875 1 0 1 0.6875 0.25 0 1 0.75 1 0 1 0.75 0 0 1 0.8125 1 0 1 0.8125 0.25 0 1 0.875 1 0 1 0.875 0 0 1 0.9375 1 0 1",
 "0 0 0 0 0.0625 1 0 0",
 "0 0 0 0 0.0625 1 0.258 1",
 "0 0 0 0 0.0625 1 0.258 0 0.25 0 0 0 0.3125 1 0 0 0.625 0 0 0 0.6875 1 0 0 0.75 0 0 0 0.8125 1 0 0",
 "0 1 0 0 0.125 0 0 0 0.25 1 0 0 0.375 0 0 0 0.5 1 0 0 0.625 0 0 0 0.75 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.9375 0 0 0 0.96875 1 0 0",
 "0 0.84375 0 0 0.03125 1 0 0 0.0625 0.5 0 0 0.09375 1 0 0 0.125 0 0 0 0.15625 1 0 0 0.1875 0.5 0 0 0.21875 1 0 0 0.25 0.875 0 0 0.28125 1 0 0 0.3125 0.5 0 0 0.34375 1 0 0 0.375 0 0 0 0.40625 1 0 0 0.4375 0.5 0 0 0.46875 1 0 0 0.5 0.875 0 0 0.53125 1 0 0 0.5625 0.5 0 0 0.59375 1 0 0 0.625 0 0 0 0.65625 1 0 0 0.6875 0.5 0 0 0.71875 1 0 0 0.75 0.875 0 0 0.78125 1 0 0 0.8125 0.5 0 0 0.84375 1 0 0 0.875 0 0 0 0.90625 1 0 0 0.9375 0.5 0 0 0.96875 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.09375 0 0 0 0.125 1 0 0 0.140625 0 0 0 0.171875 1 0 0 0.21875 0 0 0 0.25 1 0 0 0.265625 0 0 0 0.296875 1 0 0 0.34375 0 0 0 0.375 1 0 0 0.390625 0 0 0 0.421875 1 0 0 0.46875 0 0 0 0.5 1 0 0 0.515625 0 0 0 0.546875 1 0 0 0.59375 0 0 0 0.625 1 0 0 0.640625 0 0 0 0.671875 1 0 0 0.71875 0 0 0 0.75 1 0 0 0.765625 0 0 0 0.796875 1 0 0 0.84375 0 0 0 0.875 1 0 0 0.890625 0 0 0 0.921875 1 0 0 0.96875 0 0 0 1 1 0 0",
 "0 0 0 0 0.03125 1 0 0 0.0625 0.0625 0 0 0.09375 0.0625 0 0 0.09375 1 0 0 0.125 0.125 0 0 0.15625 1 0 0 0.1875 0.1875 0 0 0.21875 1 0 0 0.25 0.25 0 0 0.28125 1 0 0 0.3125 0.3125 0 0 0.34375 1 0 0 0.375 0.375 0 0 0.40625 1 0 0 0.4375 0.4375 0 0 0.46875 1 0 0 0.5 0.5 0 0 0.53125 1 0 0 0.5625 0.5625 0 0 0.59375 1 0 0 0.625 0.625 0 0 0.65625 1 0 0 0.6875 0.6875 0 0 0.71875 1 0 0 0.75 0.75 0 0 0.78125 1 0 0 0.8125 0.8125 0 0 0.84375 1 0 0 0.875 0.875 0 0 0.90625 1 0 0 0.9375 0.9375 0 0 0.96875 1 0 0",
 "0 0.9375 -0 0 0.03125 1 -0 0 0.0625 0.875 -0 0 0.09375 1 -0 0 0.125 0.8125 -0 0 0.15625 1 -0 0 0.1875 0.75 -0 0 0.21875 1 -0 0 0.25 0.6875 -0 0 0.28125 1 -0 0 0.3125 0.625 -0 0 0.34375 1 -0 0 0.375 0.5625 -0 0 0.40625 1 -0 0 0.4375 0.5 -0 0 0.46875 1 -0 0 0.5 0.4375 -0 0 0.53125 1 -0 0 0.5625 0.375 -0 0 0.59375 1 -0 0 0.625 0.3125 -0 0 0.65625 1 -0 0 0.6875 0.25 -0 0 0.71875 1 -0 0 0.75 0.1875 -0 0 0.78125 1 -0 0 0.8125 0.125 -0 0 0.84375 1 -0 0 0.875 0.0625 -0 0 0.90625 1 -0 0 0.9375 0 -0.274 0 0.96875 1 -0 0",
 "0 0 0 0 0.0625 1 0 0 0.125 0 0 0 0.1875 1 0 0 0.25 0 0 0 0.296875 1 0 0 0.343548 0 0 0 0.390625 1 0.024 0 0.4375 0 0 0 0.484375 1 0 0 0.515625 0 0 0 0.546875 1 0 0 0.578125 0 0 0 0.609375 1 0 0 0.640625 0 0 0 0.671875 1 0 0 0.703125 0 0 0 0.734375 1 0 0 0.762903 0 0 0 0.782153 1 0 0 0.801403 0 0 0 0.820653 1 0 0 0.839903 0 0 0 0.859153 1 0 0 0.878403 0 0 0 0.897653 1 0 0 0.916903 0 0 0 0.936153 1 0 0 0.955403 0 0 0 0.974194 1 0 0 1 1 0 0",
 "0 1 -0 0 0 0 0 0 0.0258065 1 -0 0 0.0445968 0 -0 0 0.0638468 1 -0 0 0.0830968 0 -0 0 0.102347 1 -0 0 0.121597 0 -0 0 0.140847 1 -0 0 0.160097 0 -0 0 0.179347 1 -0 0 0.198597 0 -0 0 0.217847 1 -0 0 0.237097 0 -0 0 0.265625 1 -0 0 0.296875 0 -0 0 0.328125 1 -0 0 0.359375 0 -0 0 0.390625 1 -0 0 0.421875 0 -0 0 0.453125 1 -0 0 0.484375 0 -0 0 0.515625 1 -0 0 0.5625 0 -0.024 0 0.609375 1 -0 0 0.656452 0 -0 0 0.703125 1 -0 0 0.75 0 -0 0 0.8125 1 -0 0 0.875 0 -0 0 0.9375 1 -0 0",
 "0 0 0 0 0.8125 1 0.43 1",
 "0 0 0 0 0.78125 1 0 0 0.8125 0 0 0 0.84375 1 0 0 0.875 0 0 0 0.890625 1 0 0 0.90625 0 0 0 0.921875 1 0 0 0.9375 0 0 0 0.953125 1 0 0 0.96875 0 0 0 0.984375 1 0 0",
 "0 0 0.256 4 0.9375 0.875 0 4 1 0 0 0",
 "0 0 0.392 4 0.96875 0.875 0 4 1 0 0 0",
 "0 1 0.418 1 0.25 0 0 1 0.25 1 0.474 1 0.5 0 0 1 0.5 1 0.39 1 0.75 0 0 1 0.75 1 0.456 1 1 0 0 1",
 "0 0.4375 0 1 0.0177419 0.463636 0 1 0.03125 0.6 0 1 0.0451613 0.629545 -0.226 1 0.125 0.875 0 1 0.125 0.1875 0 1 0.143548 0.218182 0 1 0.151613 0.493182 0.124 1 0.174597 0.377273 0 1 0.190323 0.393182 -0.234 1 0.25 0.875 0 1 0.25 0 0 1 0.266129 0.025 0 1 0.277419 0.215909 -0.238 1 0.375806 0.870455 0 1 0.377419 0.745455 0 1 0.404839 0.765909 0 1 0.412903 0.831818 0 1 0.4375 0.875 0 1 0.4375 0.5625 0 1 0.456452 0.581818 -0.132 1 0.5 1 0 1 0.5 0.875 0 1 0.525806 0.890909 -0.162 1 0.5625 1 0 1 0.5625 0.375 0 1 0.582258 0.413636 -0.146 1 0.625 1 0 1 0.625 0.3125 0 1 0.65 0.343182 -0.14 1 0.6875 0.875 0 1 0.6875 0.6875 0 1 0.71129 0.718182 -0.166 1 0.75 1 0 1 0.75 0.25 0 1 0.770968 0.277273 -0.226 1 0.8125 1 0 1 0.8125 0.625 0 1 0.824194 0.636364 -0.17 1 0.875 0.9375 0 1 0.875 0.8125 0 1 0.895161 0.827273 -0.186 1 0.9375 1 0 1 0.9375 0.625 0 1 0.956452 0.643182 -0.244 1 1 1 0 1",
 "0 0 0 1 0.015625 1 0 1 0.03125 0 0 1 0.046875 1 0 1 0.0625 0 0 1 0.078125 1 0 1 0.09375 0 0 1 0.109375 1 0 1 0.125 0 0 1 0.140625 1 0 1 0.15625 0 0 1 0.171875 1 0 1 0.1875 0 0 1 0.203125 1 0 1 0.21875 0 0 1 0.234375 1 0 1 0.25 0 0 1 0.265625 1 0 1 0.28125 0 0 1 0.296875 1 0 1 0.3125 0 0 1 0.328125 1 0 1 0.34375 0 0 1 0.359375 1 0 1 0.375 0 0 1 0.390625 1 0 1 0.40625 0 0 1 0.421875 1 0 1 0.4375 0 0 1 0.453125 1 0 1 0.46875 0 0 1 0.484375 1 0 1 0.5 0 0 1 0.515625 1 0 1 0.53125 0 0 1 0.546875 1 0 1 0.5625 0 0 1 0.578125 1 0 1 0.59375 0 0 1 0.609375 1 0 1 0.625 0 0 1 0.640625 1 0 1 0.65625 0 0 1 0.671875 1 0 1 0.6875 0 0 1 0.703125 1 0 1 0.71875 0 0 1 0.734375 1 0 1 0.75 0 0 1 0.765625 1 0 1 0.78125 0 0 1 0.796875 1 0 1 0.8125 0 0 1 0.828125 1 0 1 0.84375 0 0 1 0.859375 1 0 1 0.875 0 0 1 0.890625 1 0 1 0.90625 0 0 1 0.921875 1 0 1 0.9375 0 0 1 0.953125 1 0 1 0.96875 0 0 1 0.984375 1 0 1",
 "0 0 1 4 1 1 0 1",
 "0 1 0 1 1 0 0 1",
 "0 0 0 1 1 1 0 1",
 "0 0 -0.23 1 0.5 1 0.23 1 1 0 0 1",
};
constexpr int PatternCount=int(std::size(patternNames));
static_assert(PatternCount==int(std::size(patternPoints)),"one point list per name");
const char*const triggerModes[]={"SYNC","AUDIO","DRUMS"};
const NativeParam params[]={
 {"PATTERN",nativeChoiceValue(7,PatternCount),NativeUnit::Choice,0,100,patternNames,PatternCount},
 {"RATE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(4)},
 {"PHASE",0,NativeUnit::Percent,0,0},
 {"MIN",0,NativeUnit::Percent,0,30},
 {"MAX",100,NativeUnit::Percent,100,100},
 {"ATTACK",0,NativeUnit::Percent,0,25},
 {"RELEASE",0,NativeUnit::Percent,0,25},
 {"TENSION",50,NativeUnit::Bipolar,40,60},
 {"STEREO",50,NativeUnit::Bipolar,40,60},
 {"TRIGGER",0,NativeUnit::Choice,0,0,triggerModes,3},
 {"THRESHOLD",50,NativeUnit::Percent,30,70},
 {"SENSITIVITY",50,NativeUnit::Percent,30,70},
};

enum PointType{Hold,Curve,SCurve,Pulse,Wave,Triangle,Stairs,SmoothSt,HalfSine};
struct Segment{double x1,x2,y1,y2,tension;int type;};
struct PPoint{double x,y,tension;int type;};
constexpr double Pi=3.14159265358979323846;
constexpr int AudioLatency=int(5/1000.0*44100);   // AUDIO_LATENCY_MILLIS
constexpr int AntiClick=int(5*44100/1000.0);      // ANTICLICK_LOW_MILLIS (default anti-click)

// Pattern::buildSegments over Presets::parsePreset.
std::vector<Segment> buildSegments(const char*text){
 std::vector<PPoint> pts;std::istringstream iss{std::string(text)};double x,y,t;int type;
 while(iss>>x>>y>>t>>type)pts.push_back({x,y,t,type});
 std::stable_sort(pts.begin(),pts.end(),[](const PPoint&a,const PPoint&b){return a.x<b.x;});
 if(pts.empty()){pts.push_back({-1,.5,0,1});pts.push_back({2,.5,0,1});}
 else if(pts.size()==1){auto p=pts[0];pts.insert(pts.begin(),{-1,p.y,0,1});pts.push_back({2,p.y,0,1});}
 else{auto p1=pts.front(),p2=pts.back();pts.insert(pts.begin(),{p2.x-1,p2.y,p2.tension,p2.type});pts.push_back({p1.x+1,p1.y,p1.tension,p1.type});}
 std::vector<Segment> s;for(size_t i=0;i+1<pts.size();i++)s.push_back({pts[i].x,pts[i+1].x,pts[i].y,pts[i+1].y,pts[i].tension,pts[i].type});
 return s;
}

// GATE-12 Pattern::get_y_* (single tension multiplier).
struct Shape{
 double tmult=0;
 double power(const Segment&s,double&ten)const{
  bool rise=s.y1>s.y2;ten=std::clamp(s.tension+(rise?-tmult:tmult),-1.0,1.0);
  return std::pow(1.1,std::fabs(ten*50));
 }
 double curve(const Segment&s,double x)const{
  double ten,pwr=power(s,ten);if(s.x1==s.x2)return s.y2;
  if(ten>=0)return std::pow((x-s.x1)/(s.x2-s.x1),pwr)*(s.y2-s.y1)+s.y1;
  return -1*(std::pow(1-(x-s.x1)/(s.x2-s.x1),pwr)-1)*(s.y2-s.y1)+s.y1;
 }
 double scurve(const Segment&s,double x)const{
  double ten,pwr=power(s,ten);double xx=(s.x2+s.x1)/2,yy=(s.y2+s.y1)/2;if(s.x1==s.x2)return s.y2;
  if(x<xx&&ten>=0)return std::pow((x-s.x1)/(xx-s.x1),pwr)*(yy-s.y1)+s.y1;
  if(x<xx&&ten<0)return -1*(std::pow(1-(x-s.x1)/(xx-s.x1),pwr)-1)*(yy-s.y1)+s.y1;
  if(x>=xx&&ten>=0)return -1*(std::pow(1-(x-xx)/(s.x2-xx),pwr)-1)*(s.y2-yy)+yy;
  return std::pow((x-xx)/(s.x2-xx),pwr)*(s.y2-yy)+yy;
 }
 static double pulse(const Segment&s,double x){
  double t=std::max(std::floor(s.tension*s.tension*100),1.0);if(x==s.x2)return s.y2;
  double w=(s.x2-s.x1)/t,in=w==0?0:std::fmod(x-s.x1,w);
  return in<w/2?(s.tension>=0?s.y1:s.y2):(s.tension>=0?s.y2:s.y1);
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
  if(s.tension>=0){step=(s.x2-s.x1)/t;index=std::floor((x-s.x1)/step);ystep=(s.y2-s.y1)/(t-1);}
  else{step=(s.x2-s.x1)/(t-1);index=std::ceil((x-s.x1)/step);ystep=(s.y2-s.y1)/t;}
  if(x==s.x2)return s.y2;
  return s.y1+index*ystep;
 }
 static double smoothStairs(const Segment&s,double x){
  double pwr=4,t=std::max(std::floor(s.tension*s.tension*150),1.0),gx=(s.x2-s.x1)/t,gy=(s.y2-s.y1)/t,i=std::floor((x-s.x1)/gx);
  double xx1=s.x1+gx*i,xx2=s.x1+gx*(i+1),xx=(xx1+xx2)/2,yy1=s.y1+gy*i,yy2=s.y1+gy*(i+1),yy=(yy1+yy2)/2;
  if(s.x1==s.x2)return s.y2;
  if(x<xx&&s.tension>=0)return std::pow((x-xx1)/(xx-xx1),pwr)*(yy-yy1)+yy1;
  if(x<xx&&s.tension<0)return -1*(std::pow(1-(x-xx1)/(xx-xx1),pwr)-1)*(yy-yy1)+yy1;
  if(x>=xx&&s.tension>=0)return -1*(std::pow(1-(x-xx)/(xx2-xx),pwr)-1)*(yy2-yy)+yy;
  return std::pow((x-xx)/(xx2-xx),pwr)*(yy2-yy)+yy;
 }
 double halfSine(const Segment&s,double x)const{
  double ten,pwr=power(s,ten);if(s.x1==s.x2)return s.y2;
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

// GATE-12 RCSmoother.
struct RCSmoother{
 double a=1,b=1,state=0;
 void setup(double ra,double rb,double srate){a=1.0/(ra*srate+1);b=1.0/(rb*srate+1);}
 double process(double in,bool useA){state+=(useA?a:b)*(in-state);return state;}
 void reset(double v){state=v;}
};

// GATE-12 Transient detector.
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

double tweenEaseInOut(double t,double start,double target_,double duration){
 if(duration<1e-7)return target_;
 t=std::clamp(t,0.0,duration);double target=target_-start,tn=t/duration;
 if(tn<.5)return (target/2.0)*(2*tn)*(2*tn)+start;
 double tt=2*tn-1;return -(target/2.0)*(tt*(tt-2.0)-1.0)+start;
}

class GateShaperFx final:public NativeFx{
 std::vector<std::vector<Segment>> patterns;Shape shape;RCSmoother value,value2;Transient detL,detR;
 static constexpr int MaxLatency=AudioLatency+AntiClick;
 std::vector<double> latL=std::vector<double>(size_t(MaxLatency),0.0),latR=std::vector<double>(size_t(MaxLatency),0.0);
 int pattern=0,trigger=0,ltrigger=-1,latency=0,latpos=0,countdown=-1,acCooldown=-1,acSamples=0;
 double spb=22050,syncQN=4,phase=0,minY=0,maxY=1,stereo=0,threshold=.5,sense=.25;
 double beatPos=0,xpos=0,xpos2=0,ypos=0,ypos2=0,trigpos=0,trigposSinceHit=1,acStart=0,acTarget=0,acStart2=0,acTarget2=0;
 bool audioTrigger=false;uint32_t epoch=0;
 double getY(double x)const{return minY+(maxY-minY)*(1-shape.at(patterns[size_t(pattern)],x));}
 void clearLatency(){std::fill(latL.begin(),latL.end(),0.0);std::fill(latR.begin(),latR.end(),0.0);latpos=0;}
 void restartEnv(bool fromZero){
  if(fromZero)xpos=phase;
  else{xpos=beatPos/syncQN+phase;xpos-=std::floor(xpos);}
  xpos2=xpos+stereo;if(xpos2<0.0)xpos2+=1;if(xpos2>1.0)xpos2-=std::floor(xpos2);
  value.reset(getY(xpos));value2.reset(getY(xpos2));
 }
 void onPlay(){
  clearLatency();audioTrigger=false;beatPos=0;trigpos=0;trigposSinceHit=1;countdown=-1;detL.clear();detR.clear();
  if(trigger==0)restartEnv(false);
 }
 // Second (stereo-offset) channel follows the first unless STEREO is set.
 void stereoSmooth(){
  xpos2=xpos;ypos2=ypos;
  if(std::fabs(stereo)>1e-4){xpos2=xpos+stereo;if(xpos2<0.0)xpos2+=1;xpos2-=std::floor(xpos2);double y2=getY(xpos2);ypos2=value2.process(y2,y2>ypos2);}
 }
public:
 GateShaperFx(){patterns.reserve(size_t(PatternCount));for(auto*p:patternPoints)patterns.push_back(buildSegments(p));}
 void configure(const uint8_t*v,float bpm)override{
  pattern=nativeChoice(v[0],PatternCount);syncQN=nativeDivisionBeats(nativeDivision(v[1]));spb=nativeSamplesPerBeat(bpm);
  phase=v[2]*.01;minY=v[3]*.01;maxY=v[4]*.01;
  double atk=v[5]*.01,rel=v[6]*.01;atk*=atk;rel*=rel;value.setup(atk*.25,rel*.25,44100);value2.setup(atk*.25,rel*.25,44100);
  shape.tmult=(v[7]*2-100)*.01;
  stereo=(v[8]*2-100)*.01*180/360.0;
  trigger=nativeChoice(v[9],3);threshold=v[10]*.01;sense=1.0-v[11]*.01;sense*=sense;
  if(trigger!=ltrigger){latency=trigger?MaxLatency:0;clearLatency();ltrigger=trigger;}
  if(trigger==0)audioTrigger=false;
 }
 void clear()override{
  clearLatency();detL.clear();detR.clear();value.reset(0);value2.reset(0);xpos=xpos2=ypos=ypos2=0;
  audioTrigger=false;trigpos=0;trigposSinceHit=1;countdown=-1;acCooldown=-1;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;onPlay();}   // downbeat
  const double beatsPerSample=1.0/spb;
  for(int i=0;i<frames;i++){
   double l=io[i*2],r=io[i*2+1];
   if(trigger==0){
    xpos=beatPos/syncQN+phase;xpos-=std::floor(xpos);
    double y=getY(xpos);ypos=value.process(y,y>ypos);stereoSmooth();
    io[i*2]=nativeClean(float(l*ypos));io[i*2+1]=nativeClean(float(r*ypos2));
   }else{
    latL[size_t(latpos)]=l;latR[size_t(latpos)]=r;
    if(detL.detect(trigger-1,l,threshold,sense)||detR.detect(trigger-1,r,threshold,sense)){
     detL.startCooldown();detR.startCooldown();countdown=std::max(0,AudioLatency);
    }
    int readPos=(latpos+1)%latency;double dl=latL[size_t(readPos)],dr=latR[size_t(readPos)];
    if(countdown==0){   // hit: ease towards the restart value, then restart
     acCooldown=AntiClick;acSamples=acCooldown;acStart=ypos;double ph=phase<1e-7?1e-7:phase;acTarget=getY(ph);
     acStart2=ypos2;double st=stereo+ph;if(st>1.0)st-=1.0;if(st<0.0)st+=1.0;acTarget2=getY(st);
    }
    double inc=beatsPerSample/syncQN;xpos+=inc;trigpos+=inc;trigposSinceHit+=inc;xpos-=std::floor(xpos);
    if(audioTrigger){if(trigpos>=1.0){audioTrigger=false;xpos=phase?phase:1.0;}}
    else xpos=phase?phase:1.0;
    if(acCooldown>=0){
     ypos=tweenEaseInOut(double(acSamples-acCooldown),acStart,acTarget,double(acSamples));
     xpos2=xpos;ypos2=ypos;
     if(std::fabs(stereo)>1e-4){xpos2=xpos+stereo;if(xpos2<0.0)xpos2+=1;xpos2-=std::floor(xpos2);ypos2=tweenEaseInOut(double(acSamples-acCooldown),acStart2,acTarget2,double(acSamples));}
    }else{double y=getY(xpos);ypos=value.process(y,y>ypos);stereoSmooth();}
    io[i*2]=nativeClean(float(dl*ypos));io[i*2+1]=nativeClean(float(dr*ypos2));
    latpos=(latpos+1)%latency;
    if(countdown>-1)countdown-=1;
   }
   beatPos+=beatsPerSample;
   if(acCooldown>=0){
    if(acCooldown==0&&trigger){audioTrigger=true;trigpos=0;trigposSinceHit=0;restartEnv(true);}
    acCooldown-=1;
   }
  }
 }
};
}
const NativeInfo gateShaperInfo{FxType::GateShaper,"GATE SHAPER","GATE","GS",6,0,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeGateShaper(){return std::make_unique<GateShaperFx>();}
}
