# UAC2 gadget feasibility kit (TG3040, Linux 4.9.191 #804)

Presents the Brick to a USB host as a class-compliant UAC2 audio input, alongside
ADB, without replacing the kernel. Feasibility only: nothing here is installed by
the launcher, the OS image or the deployer. Everything runs from `/tmp` and
configfs, so a reboot returns the stock ADB-only gadget.

| File | Purpose |
| --- | --- |
| `build.sh` | Builds `usb_f_uac2.ko` from upstream 4.9.191 with the device config |
| `tg3040-layout.patch` | Two struct layout differences measured in the running kernel |
| `f_uac2-in-only.patch` | Device-to-host-only topology when `c_chmask=0`, rebind-safe ALSA card, host-visible name, layout asserts |
| `abicmp.py` | Compares upstream-compiled functions with the device kernel image |
| `uac2-trial.sh` / `uac2-revert.sh` | Stage/bind the audio function next to ADB, and undo it |
| `uac_tone.c` / `uac_count.c` | Brick-side ALSA writers: stereo tone and frame counter |
| `uac2-tone-loop.sh` | Keeps the tone available to any host (reopens after unplug) |
| `uac2-adbd-cycle.sh` | Restarts adbd: reproduces the unplug unbind/rebind without touching cables |
| `ca_capture.cpp` | Mac-side input-only CoreAudio capture to raw int16 |

## Layout differences from upstream 4.9.191

Measured by disassembling the device kernel (`kernel-image.bin`, `/proc/kallsyms`):

- `struct usb_request`: an extra 32-bit field after `list`. `status` is at 84 and
  `actual` at 88 (upstream 80/84). Source: `composite_setup`, `sunxi_udc_done`.
- `struct usb_function_instance`: an extra pointer after `fd` (Android BSP).
  `set_inst_name` is at 168 and `free_func_inst` at 176. Source:
  `usb_put_function_instance`, `function_make`.

The module has `BUILD_BUG_ON` checks for these offsets. Every other compared
function (USB endpoint API, composite helpers, ALSA PCM/card, platform/driver
core, configfs) matched instruction-for-instruction or by field-offset set.

## Rebind crash in stock 4.9 f_uac2 (fixed)

On this BSP every cable unplug makes adbd reopen FunctionFS, which unbinds and
rebinds the whole gadget. Stock 4.9 f_uac2 re-registers the same embedded
platform device on the second bind ("kobject ... tried to init an initialized
object"), then oopses in adbd (use after free in `kobject_get`). USB stays dead
until reboot. The patch removes the platform device, parents the ALSA card on
the gadget device and frees it with `snd_card_free_when_closed`, so an
application holding the PCM open cannot block or break unbind.

Verified: repeated adbd restarts with and without the PCM held open, no kernel
errors; the writer reopens the card and the Mac capture is correct afterwards.

## Gadget sequence

The BSP `adbd` rebinds the UDC itself immediately after any unbind. Link the
audio function into `configs/c.1` while ADB is still bound (configfs only stages
it), then unbind; the rebind then includes audio. Linking after unbinding
loses the race, and the vendor role scanner then switches the port to host mode
3 s after an unconfigured connection.

The Mac (Apple silicon) holds a new composite device behind accessory approval
("blocked by transport restrictions") until the owner allows it; ADB is
unavailable while it is held.

BusyBox on the device has no `rmdir`, so `functions/uac2.usb0` remains after a
revert and the module cannot be unloaded until reboot. This is harmless.
