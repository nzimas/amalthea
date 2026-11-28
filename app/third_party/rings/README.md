# Mutable Instruments Rings DSP

Emilie Gillet's Rings, from <https://github.com/pichenettes/eurorack> commit
`f4cfa7697095a28d292bff758beb51deb49b5a86`: `rings/dsp/` (every file, unchanged) and
`rings/resources.{cc,h}` (unchanged). MIT licence; the full notice is in each source file's header.
The same files were already vendored, unchanged, in vascular-ios
(`Sources/CVascularOpcodes/vendor/rings`).

They build against the stmlib in `third_party/clouds/stmlib` (upstream stmlib `d18def8`; the
only difference is a thread-local random generator state).

Used by:
- the RINGS insert (`src/discovery_inserts.cpp`): the modal `Resonator` alone, as before;
- the RINGS instrument (`src/native_engines.cpp`): `Part` (the six resonator models, 1–4 voices,
  the internal exciter, strumming, chords), `StringSynthPart` (the Disastrous Peace easter-egg
  synth and its six effects) and `Strummer`, at Rings' own 48 kHz.

The hardware layer (`rings.cc`, `cv_scaler`, `ui`, `settings`, drivers) is not included; the
instrument adapter does what `rings.cc` does with the module's controls.
