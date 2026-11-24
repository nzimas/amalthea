# Amalthea Linux

Bespoke minimal AArch64 Linux userspace for the TRIMUI Brick TG3040, with a custom
RAM boot/recovery environment and Amalthea appliances. This replaces the earlier
SD-card launcher profile; that profile was not a replacement distribution.

**Current status: installed on the TG3040 and hardware-booted from its replacement
root partition.** The latest deployed audio update is recorded in
`validation/USB-STARTUP-0.21.7.md`; `validation/HARDWARE.md` records the initial
installation checks and retained components.

## System contents

- BusyBox 1.37.0, built statically from its checksum-verified upstream source with
  an explicitly selected set of system and recovery utilities.
- New BusyBox init configuration, boot/mount logic, recovery ADB supervision,
  appliance supervision and shutdown logic. No OpenWrt procd, init scripts,
  overlay root, package manager, network service stack or SD boot hook.
- Amalthea's native framebuffer/ALSA appliance binary. No SDL runtime dependency,
  desktop, window system, sound server, gaming menu or emulators.
- Exact validated board kernel and bootloader retained. A new initramfs is packed
  with that kernel into the boot image; the kernel bytes are checked unchanged.
- Three individually retained BSP helpers: virtual gamepad input, board power/
  volume handling and hardware settings. Only their recursive ELF dependencies
  are copied. They are documented board support, not a stock frontend fallback.
- Matched display modules and GPU firmware are currently available as a fallback
  if the built-in framebuffer is absent; they are not loaded automatically when
  `/dev/fb0` exists. Unnecessary camera, Wi-Fi, Bluetooth and firewall modules are
  omitted. Kernel charging and thermal protection remain active.

The root tree is assembled from an empty directory by `tools/assemble.py`. Every
retained file is allowlisted, hashed and recorded in the build manifest. Debug
sections are stripped from AArch64 ELF copies; original hashes are also recorded.
Firmware for other processor architectures is preserved byte-for-byte.

The root filesystem is read-only during normal operation, with tmpfs for transient
state and a separate internal ext4 data mount. It does not mount the former stock
overlay. SD storage is optional and cannot select a gaming fallback. Every
appliance startup remains blank and stopped; no `--load` is used.

## Build

The source backup is the verified TG3040 rootfs/boot backup in
`../trimui-tracker-linux/backups/2026-11-20`. No full stock filesystem is copied
into the output; the extracted donor is only a source for selected BSP files and
compatible runtime libraries. The appliance comes from `../amalthea/build/brick`.

Build the `amalthea-linux-build:ubuntu20` Docker image, verify the BusyBox source
hash, then run `tools/build_busybox.sh`, `tools/assemble.py` and
`tools/make_images.py` in that environment. `build/` and `images/` are untracked.
The root image is sized to the existing 500-MiB root partition; approximately
12 MiB of actual system files are selected, before final hardware-driven pruning.
The custom recovery initramfs is about 1.4 MiB and fits the original boot partition.

`sources.lock.json` records the verified upstream BusyBox archive and original
partition/kernel hashes. The build depends on the appliance repository’s
`amalthea-build:ubuntu20` ARM64 toolchain image. With that image, extracted donor
and verified BusyBox archive available:

```sh
docker build -t amalthea-linux-build:ubuntu20 .
docker run --rm -v "$PWD:/work" -v ../amalthea:/app:ro \
  amalthea-linux-build:ubuntu20 sh -c \
  "sh tools/build_busybox.sh && python3 tools/assemble.py && python3 tools/make_images.py && python3 tools/validate_images.py"
```

Images and donor files are ignored by Git; validated deployment manifests and the
selected BusyBox configuration are retained under `validation/`.

For a root-only update with unchanged boot/recovery sources, use
`python3 tools/make_images.py --root-only`. This checks the existing boot and
initramfs against the previous manifest and retains those artifacts while
rebuilding the root image. It does not validate source equivalence: review boot
and recovery source changes before choosing this mode. Hardware trial and RAM
installation checks still apply.

## Installation sequence

1. Validate original boot/rootfs backup hashes against the live device. Completed:
   both partitions match their original verified backups; battery was charging
   at 87% during the preflight inspection.
2. Check the new root in a container and on the target, including library closure,
   shell, USB recovery, native framebuffer, gamepad and codec. Container shell,
   headless appliance startup and ext4 checks pass. Target display, input, USB
   recovery, watchdog and continuous native codec playback also pass.
3. Boot the new root from an internal image using the custom RAM boot manager.
   This is a trial of the real replacement root, not a launcher over stock init.
4. Once hardware boot is verified, select `install` mode. The RAM installer
   verifies image SHA-256, byte size, target partition size, unmounted target and
   battery charge before writing. It selects recovery before the write, checks
   the complete written partition, and selects `system` only after success.
5. Boot from the replacement root partition and verify PID 1, root identity,
   absence of gaming processes, recovery, controls/audio and independence from SD.

No bootloader/environment, partition table, recovery partition, ROMs, SD contents
or existing saved sessions are to be overwritten. Backup copies remain available
for recovery but are never a normal gaming frontend fallback.

## Hardware startup requirements

The bootloader leaves the hardware watchdog armed. The RAM environment starts
BusyBox watchdog before image verification and retains it across `switch_root`.
Recovery includes devpts and the `/bin/adb_shell` wrapper required by board adbd.
The daemon can bind the USB controller itself; successful automatic binding is
accepted. Normal boot enables both codec output routes before starting audio and
keeps headphone attenuation at raw zero.

Boot modes live on internal data storage: `system` mounts the replacement root
partition read-only, `trial` mounts a verified internal image, and `rescue` stays
in RAM with ADB. No mode launches the old OS. Kernel/bootloader and selected BSP
helpers remain vendor components; this is a custom userspace distribution, not a
new board kernel or rewritten bootloader. The initial bootloader logo remains.

The owner reports a clean multi-track FM1 stress test on this system. Version
0.21.4 adds capture recovery re-priming and restores the ALSA hardware MIDI
definition. Version 0.21.5 checkpoints transport/kernel/system diagnostics to
internal storage during operation, with separate boot and run identities. The original intermittent crackle
cause remains unproven; the userspace replacement and one clean run do not
establish live-performance reliability. See the latest audio validation record.


The latest startup correction normalizes the external USB controller through
NULL, DEVICE initialization, then DEVICE recreation before gadget binding. The
retained kernel reads `usb_detect_mode`, absent from the board DT (which supplies
`usb_detect_type`), so its effective boot path falls back to polling. This is
handled explicitly in userspace; boot/kernel/DT artifacts remain unchanged.
Persistent startup state logs are retained. No automatic waveform or input-level
analysis is enabled. The owner's audible clean-handoff/bad-restart reproduction
remains the basis of the investigation; controller ordering checks alone do not
certify crackle resolution.
