# Vascular DSP source subset

Owner: Nuno Zimas. Ported at the owner's request from vascular-ios commit
304a112a8e954dd6cf4378863334ae4040ebc8a3, Sources/CVascularOpcodes/src and vendor.
The author's cores are integrated into this GPL-3.0 appliance. Existing copyright
and DSP provenance comments are retained. Plaits and stmlib retain their MIT
notices; see PLAITS_LICENSE and the individual source headers.

No Swift, UIKit, Csound plugin host, or application framework is linked.

Amalthea adaptations:

- Dynamic audio delay buffers become bounded per-voice RtBuffer arena spans.
- Resurgent Twist placement allocation uses the same arena; no callback heap use.
- Its stmlib namespace is isolated from Clouds, and RNG state is thread-local,
  saved/restored by each native instrument.
- Anima exposes gate release and note retargeting for the tracker adapter.
- Constant Anima oscillator/filter/envelope coefficients are cached without
  changing the signal equations. Tests retain unmodified original reference
  headers under tests/reference and compare rendered samples exactly.
- Native recipes and the tracker adapter live in src/native_engines.cpp.

See docs/ENGINE-PORTS.md for feature scope, source identities and measurements.
