# TRIMUI Brick USB audio inspection

Inspected 21 November 2026, TG3040 serial <adb-serial>. No USB mode,
kernel, firmware, or gadget binding was changed.

## Findings on this unit

The installed vendor kernel is Linux 4.9.191, build 804, 15 December 2024, aarch64.
Its live /proc/config.gz enables SND_USB_AUDIO, SND_PCM, USB_GADGET,
USB_CONFIGFS, SUNXI EHCI/OHCI host controllers and SUNXI_UDC0.
USB_CONFIGFS_F_UAC1 and USB_CONFIGFS_F_UAC2 are explicitly not set; no
installed UAC modules were found. The bound gadget g1 on 5100000.udc-controller
contains only ffs.adb and currently negotiates high-speed.

The sole ALSA card is the internal SUNXI-CODEC, with playback and capture.
Analog codec capture does not establish USB audio input capability. No external
USB audio interface was attached, so USB streaming was not tested.

## What this means

* **External USB interface on the top host port:** the installed kernel includes
  the standard host audio driver. A class-compliant interface can provide playback
  and capture in principle. Actual duplex operation, latency and channel formats
  remain unverified on this unit. A developer reports external USB audio input and
  output on this platform in [the primary audio notes](https://github.com/sydilaxe/Trimui-Brick-Resources/blob/claude/relaxed-brahmagupta-yhd2my/docs/audio.md).
* **Brick directly connected to a laptop as a normal duplex USB sound card:**
  unavailable with the installed kernel/gadget configuration. This needs UAC
  support and controller validation, rather than an application setting.

A separate developer's [hardware probe](https://github.com/sydilaxe/Trimui-Brick-Resources/blob/claude/relaxed-brahmagupta-yhd2my/docs/hardware.md)
on an older vendor build 535 reports binding one isochronous direction at a time,
but failure binding both simultaneously. That is evidence of a possible vendor
UDC limitation, not a result measured on our build 804. It does not prove the
hardware fundamentally incapable. Upstream Linux 4.9 has a
[UAC2 implementation with playback and capture](https://raw.githubusercontent.com/torvalds/linux/v4.9/drivers/usb/gadget/function/f_uac2.c).

A custom two-way bulk protocol is a separate possible investigation, requiring a
host helper and latency tests; it would not automatically appear as a normal USB
sound card. The FX RACK appliance now implements host-side capture through ALSA; gadget audio remains unchanged.

## Planned appliance capability

The owner specifically wants direct multichannel FM1 capture and separate
application routing on the Brick. This uses host mode, independent of the gadget
limitations above. [JIANT's primary manual](https://github.com/juanjiant-bit/JIANT/blob/main/docs/MANUAL.md)
documents eight USB channels from FM1: six mono tracks plus stereo FX returns.
Host capture is implemented in 0.20.0; see [FX RACK appliance](FX RACK-APPLIANCE.md). Physical FM1 streaming remains unverified until a source is connected.

## Bottom-port diagnosis — 21 November 2026

The owner's FM1 test used the bottom port. Live inspection found
`/sys/devices/platform/soc/usbc0/otg_role` reporting `usb_device`; the kernel
recorded gadget disconnection/reconnection, with no FM1 host enumeration and only
the internal codec in ALSA. The original FX RACK backend did not switch this port's
role. Therefore its host capture could not discover the attached FM1 in that mode.

Vendor `/usr/trimui/bin/usb_host.sh` switches through a **read-trigger**:
`cat /sys/devices/platform/soc/usbc0/usb_host`, then enables `axp_5v`.
`usb_device.sh` uses the corresponding `usb_device` read-trigger. These files are
commands despite being readable; do not include them in passive diagnostic dumps.
The passive `otg_role` file is safe to inspect. A role switch immediately drops ADB
on the bottom port. Actual bottom-port FM1 streaming and electrical operation are
still unverified; role-control availability alone is not a streaming test.

`tools/usb_diagnostics.sh` passively records USB identities, ALSA stream descriptors,
role and relevant kernel messages in `logs/usb/latest.txt` and `previous.txt` under
the installed application directory. Only changed snapshots are persisted. It does
not change the role or open capture. The FX RACK host additionally publishes its latest
scan/open failure in `/tmp/amalthea-usb-status`, avoiding an unexplained waiting
state when an ALSA candidate exists but cannot stream.

A staged, manually armed `tools/usb_host_trial.sh` waits for physical laptop
removal before changing roles, enables the vendor host power, and restores device
mode after 600 seconds. It preserves `host-trial-snapshot.txt` before restoration.
It is not run by the launcher and must only be armed during a coordinated test.
The device RTC is unset; use the recorded kernel uptime for chronology.

## Successful direct Bigorna trial

Retrieved evidence from the owner's ten-minute test: product `FM-1 Bigorna`,
ALSA capture **Running**, interface 2/alt 1, S16_LE, six channels, 44100 Hz,
async IN endpoint 4. The owner confirmed audible operation. Raw MIDI was opened
but the retained snapshot had Rx bytes 0; MIDI clock was not demonstrated by this
trial. Firmware-specific clock transmission may require separate configuration.
Do not claim external sync verified based on audio capture alone.

Version 0.21 device QA confirmed short-X density, hold-X generated triggers and
an internal playhead visible on the second row. Startup remained blank/stopped;
no live sessions were saved, loaded or restored.

## Permanent FX RACK role handling — 0.21.1

The timed host trial is no longer needed for normal FX RACK use. Native `BrickUsbRole`
in the non-audio USB thread observes the passive UDC state while FX RACK is selected.
It waits for `not attached` to remain stable for three seconds before triggering
host mode and enabling `axp_5v`. A configured/attached laptop cancels that transition.
There is no host timeout. Exiting FX RACK closes capture, restores the previous power
setting and triggers device mode. The ownership marker identifies the process;
a later supervisor restart recovers a role left by a dead owner. The manager does
not restore a host role it did not own. No role changes happen from the audio
callback and no kernel/gadget configuration is replaced.

For another laptop connection, exit FX RACK to the appliance dashboard first. Plugging
two USB hosts together while staying in FX RACK cannot establish ADB. Startup remains
blank and stopped; host mode is tied to explicit entry into FX RACK.

## USB audio output to a host (UAC2 gadget) — feasibility, 22 November 2026

Owner request: stream appliance audio to a computer or phone over USB, with a
stereo mix and, later, multitrack stems. The installed kernel lacks
`USB_CONFIGFS_F_UAC2`, but has `CONFIG_MODULES=y`, no `MODVERSIONS` and no module
signing. Vendor kernel source is not published.

An out-of-tree `usb_f_uac2.ko` was built from upstream 4.9.191 with the device
config and two measured layout corrections, patched for device-to-host-only
operation (one isochronous IN endpoint). See `tools/uac2_gadget/README.md`.
Vermagic matches (`4.9.191 SMP preempt mod_unload aarch64`).

Trial (RAM-only, kernel uptime 10092 s, no reboot, no session saved/loaded):

- Module loaded with no errors; composite gadget ADB + UAC2 bound; ALSA card 1
  `UAC2Gadget` appeared on the Brick. ADB kept working on the composite device.
- macOS enumerated it at 480 Mbps (class 0xEF/IAD) after the owner allowed the
  accessory. CoreAudio input: 2 channels, 48000 Hz, transport USB. Its name
  currently comes from the stock interface string ("Capture Inactive").
- Tone test: correct channel assignment, −15.05 dBFS RMS as written.
- Frame-counter test, native CoreAudio capture: 60 s, 2,878,976 frames, every step
  +1, zero drops/repeats, zero host timeline jumps; Brick writer zero xruns; no
  kernel errors. The stream is bit-exact.
- ffmpeg `avfoundation` capture of the same stream dropped whole 512-frame host
  buffers; that is a capture-tool artefact, not a USB loss.
- Unplugging the Brick from the Mac oopsed the kernel (adbd context, module
  loaded): stock 4.9 f_uac2 cannot survive the unbind/rebind that adbd performs
  on every unplug. The first Poco OTG attempt therefore saw nothing. Fixed in the
  module (no platform device; card freed when closed). After a reboot, repeated
  adbd-restart cycles, including with the PCM held open, gave no kernel errors
  and correct Mac capture (440.00/660.01 Hz, −15.05 dBFS). Host name is now
  "Amalthea Audio".

Not yet verified: Android/phone hosts, more than 2 channels, 24-bit, long-duration
streaming, and coexistence with FX RACK host mode (the bottom port is the only OTG
port, so FX RACK host capture and gadget output cannot run on it simultaneously).
The gadget is paced by the host's USB clock, independent of the codec clock;
an appliance feeding both needs drift handling between them.

Poco X7 Pro (OTG on), Brick in the dashboard (device role), 22 November 2026: a
passive logger on the Brick recorded `otg_id` (gpio-232) high and `axp2202-usb
online=0` for the whole attachment (1121–1180 s uptime). The phone supplied no
VBUS and the Brick's role line never changed; no gadget connection occurred. The
same phone and cable host the FM1. With FX RACK entered, the Brick sources 5 V as host
and the phone enumerates as a USB device instead. The Brick exposes no Type-C
controller to Linux (no TCPC driver, `/sys/class/typec` absent).

Type-C role, 22 November 2026: the Brick has no Type-C driver; CC is handled by
the AXP2202 at i2c-6/0x34. Its registers match the AXP717 datasheet layout
(`0xE3` CC mode control, `0xE7` CC status): live `E3=0x13` = **DRP, prefer SNK**;
`E7=0xA3` on the Mac = `ATTACH_SNK`, 1.5 A advertised. The `otg_id` GPIO does not
follow CC. Writing `E3=0x11` (SINK only) from userspace (`tools/uac2_gadget/axp_cc.c`)
renegotiated to `ATTACH_SNK` on the Mac in ~1.4 s; the gadget rebound cleanly and
audio capture remained correct. Original value to restore: `0x13`.

Poco X7 Pro with `E3=0x11` (SINK only), USB-C to USB-C, Brick in the dashboard:
CC `ATTACH_SNK` at 1488 s, gadget configured by the phone at 1489 s, clean
detach at 1549 s, no kernel errors. The owner confirmed the camera app recorded
the Brick's test tone over USB. `E3` was then restored to `0x13`.
No appliance feeds the gadget yet; the recorded audio was the test tone only.

## Appliance USB output — build 6, 22 November 2026

Installed build 6 (app SHA-256 `2a6d219e…5b42b`, module `ea13a419…3d2e`) started
blank and stopped. Log: `USB-C port mode: sink`, `USB audio function added`,
`USB audio output open: 2 ch 44100 Hz period 256 buffer 1024`. The Mac lists
"Amalthea Audio", 2 input channels, 44100 Hz. With an owner-started tracker
sequence, a 31 s native CoreAudio capture had peak −0.4 dBFS, RMS −13.9 dBFS,
no zero-run dropouts, no click candidates and no host timeline jumps.
Phone capture of appliance audio and FX RACK-mode switching with the new build are
not yet verified. Stems are not yet exposed.

Replug fault and fix (builds 7–8): the owner's Poco capture after an unplug from
the Mac recorded a buzz. The app still held `/dev/snd/pcmC1D0p (deleted)`: the
rebind on unplug had replaced the card and the writer had not noticed; the Mac
then received a −30 dBFS buzz too (it had been clean before any unplug). Fixed in
the writer (kernel status ioctl every 250 ms) and in the module (clear stream
state on unbind; idle IN sends zeros). After reboot with the new module (SHA-256
`96168b03…a874`): Mac capture with the tracker stopped was exact digital silence
before and after an adbd-restart rebind, and the writer reopened the live card.
After reboot the app had to set sink mode, confirming the board resets `E3` to
dual-role at boot.

Build 8 owner verification, 22 November 2026 (kernel log, uptime): Poco X7 Pro
recorded the playing tracker sequence over USB (configured 419–487 s). FX RACK then
switched the port to host (513 s), hosted the FM1 at full speed (524–560 s),
and returned to device mode on exit (568 s); the Mac reattached at 584 s. The
owner confirmed both scenarios work as intended. No kernel errors.
