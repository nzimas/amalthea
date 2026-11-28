# Tonitor DSP subset: OBRADOR

Owner: Nuno Zimas. Ported at the owner's request from Tonitor for the M-VAVE FM-1
(`~/development/mvave-fm1`, published as nzimas/tonitor), HEAD `b1ffc18` (Tonitor build 75);
`firmware/src/obrador.c` last changed in commit `309fa36d28f968b5b94324d8071fdbda55a28dc0`.
GPL-3.0-only; see LICENSE.

- `obrador.c` — the engine, unchanged except one line: `ob_measure_on`'s window array is a
  local rather than a function-local static, because Amalthea cooks patches on the producer
  and UI threads.
- `dsp.h` — Tonitor's fixed-point helpers, unchanged.
- `tonitor_tables.h` — the first part of the header Tonitor's `tools/gen_tables.py` writes
  (SINE, PITCH_OCT / pitch_inc, EXP2_FRAC, LOG2_FRAC, TANH_Q15, TAN_G); the Anima, mu-law and
  FM6 tables after it are not needed and are left out. Regenerated from that commit; identical
  to Tonitor's own build output.

Tonitor's FM-1 version is used (four macros: CUTOFF, MORPH, RESONANCE, DETUNE), not the
SMK-37 fork's eight. The adapter is `src/engine_obrador.c`.
