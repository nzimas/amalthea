# TG3040 hardware deployment, 2026-11-22

Status: installed and verified after a normal restart from the replacement root
partition. The owner confirmed both Tracker and FX tiles are visible on the
physical screen. Native audio remains running.

Original partitions were read back before any write and matched the verified
2026-11-20 backups:

- boot (15 MiB): `306c04bfa6f2e21c606f255a5626032bb0e9c52f39d2beb42bdfb90a0e80a79f`
- rootfs (500 MiB): `ceb1ec233f9cfd30b0b686142a6ee9c30a20386b6f9fe45680f17956bff74952`

The replacement root was first tested in a chroot, then booted as the actual root
from an internal read-only loop image with BusyBox init as PID 1. The stock root
and old overlay were not mounted during that boot. The new root used its own
libraries, board helpers, framebuffer, ALSA configuration and appliance binary.
The appliance displayed its dashboard, and Enter opened a blank, stopped tracker.
No session was saved or loaded. Saved-session hashes remained unchanged.

Hardware findings incorporated before final installation:

- The bootloader leaves the hardware watchdog enabled. Start BusyBox watchdog
  immediately after devtmpfs, before image checks; otherwise the board resets.
- Vendor adbd executes `/bin/adb_shell`. Supply a new wrapper in both roots.
- ADB shell requires devpts in RAM recovery as well as the normal root.
- Vendor adbd binds the USB controller automatically. A successful automatic bind
  must not be reported as an error by a second manual bind attempt.
- The codec needs `Headphone Switch=1` and `HpSpeaker Switch=1` before audio starts.
  Without the routes, playback reports RUNNING but the DMA pointer stalls at 32.
  Enabling both produced sustained DMA progress and a stable appliance process.
- Preserve `Headphone Volume=0`: this is attenuation, and zero is maximum output.

The trial mounted no SD filesystem and loaded no optional GPU module. The kernel
provided framebuffer, virtual-input interface, codec, USB and watchdog directly.
The retained kernel remains Linux 4.9.191 BSP build 804; this userspace replacement
does not claim to fix the separate FM1 USB crackling investigation.

## Final installed verification

- Boot partition read-back SHA-256:
  `3963af4bd04e757c15cf3848811fdbc7c6fbbb3f653b6cd83bc21cd0214b5ecd`
- Root partition read-back SHA-256 (also independently checked after installation):
  `6416d3b58cd3b3d33c4c472e351ac549cd647367112cb0afebd9e613a2dda5d8`
- `/proc/1/exe` is `/bin/busybox`; PID 1 is `init`.
- `/dev/mmcblk0p5` is mounted directly as `/`, read-only ext4. No loop image,
  stock overlay or SD mount is used on the final normal restart.
- Internal `boot.mode` is `system`; the RAM installer recorded the matching full
  partition checksum before selecting it.
- The final normal restart opened native audio at monotonic 6.685 seconds.
  The same appliance process remained alive and its hardware frame counter
  advanced from 1,046,500 to 1,968,848 during final verification.
- USB ADB remains available with the new shell wrapper and terminal mount.
- Three retained board helpers and the watchdog run; no MainUI, procd, ubusd or
  netifd process runs. Optional GPU modules remain unloaded.
- Both preserved session-file hashes still match the pre-install values.
- Final screenshot: `installed-dashboard.png`. Raw evidence:
  `installed-system.txt`. Image and individual-file manifests are alongside it.

## Recovery operation

From the normally running system, `adb shell` can select RAM recovery:

```sh
adb shell 'echo rescue > /mnt/UDISK/amalthea-linux/boot.mode; sync; reboot'
```

In RAM recovery, `/tmp/amalthea-recovery-reason` explains the selected/failing
mode. The root partition is not mounted. To return to the installed system:

```sh
adb shell 'echo system > /mnt/UDISK/amalthea-linux/boot.mode; sync; reboot -f'
```

`reboot -f` is used in RAM because its PID 1 is the recovery shell. Normal boots
use BusyBox init and ordinary `reboot`/`poweroff`. Keep the verified images and
original backups for repair. Do not select `install` unless deliberately
installing a checksum-verified replacement root image for this TG3040.

Only boot (p4) and rootfs (p5) were written. The partition table, bootloader,
environments, old overlay partition, private/recovery/pstore partitions, SD and
saved sessions were preserved. The former overlay is not mounted or used.
