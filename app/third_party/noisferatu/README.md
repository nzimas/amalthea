# Noisferatu DSP -> Russolo

Robert Heel, https://github.com/rob-scape/noisferatu,
commit eeed0aa797b4e622bdf059920c87d776e7d08d8b. Code: GPLv3, as specified
by the upstream README. Original algos.h, params.h and sample.h are retained as
upstream-* files for comparison. The upstream display/hardware libraries,
Arduino runtime, ISR, ADC and DAC code are excluded.

NoisferatuVoice.h wraps all 45 audio algorithms and their parameter mapping in
one instance. Global mutable state and function-local statics are private
instance members; PRNG draws use that instance's seed. Arduino min/max/constrain,
random and flash-byte reads have tiny native equivalents. sample.h is immutable
crackle data shared by all voices. Sample-clock service replaces main-loop
buffer regeneration. A keyed initial Bernoulli event and a nonzero starting
noise/saw window prevent silent initial notes while retaining subsequent chance
behavior. The adapter adds note-relative 16kHz playback, velocity, envelope,
seeded crushing and DC removal in src/native_engines.cpp.
