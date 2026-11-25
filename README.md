<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/assets/logo-dark.svg">
  <img src="docs/assets/logo-light.svg" alt="Amalthea" width="460">
</picture>

# Amalthea

**Turn your TrimUI Brick into multiple instruments.**

Amalthea is not an app, a launcher or an overlay on the stock firmware. It **replaces the Brick's operating
system**. The installer writes a new boot partition and a new root filesystem: Amalthea Linux, a minimal
distribution built for one purpose: a platform for musical instruments, running them with the lowest latency
and the steadiest audio the hardware allows. The gaming frontend, the emulators, the service stack and everything else the Brick
shipped with are gone (until you put them back with the restore script).

Amalthea nursed Zeus; in the tradition where she is the goat herself, one of her horns became the cornucopia,
the horn of plenty. Amalthea is also Jupiter's red moon. The platform offers plenty: synthesis engines, more
than seventy effects, generators and a master chain, shared by every instrument and used by each in its own way.
New instruments arrive with new builds.

**Website, manual and installation guide: <https://nzimas.github.io/amalthea/>**

State: **build 36**. Each build is described in [CHANGELOG.md](CHANGELOG.md).

## A platform for instruments

Amalthea is built to host a growing fleet of instruments. Underneath all of them sit the same resources:

- **Synthesis engines**: VAP, PERC, CHIP, FIBER, HIVE, OP4, ANIMA, RUSSOLO, BAY AREA, RESURGENT, CALIGULA and
  FM DRUM.
- **Effects**: more than seventy inserts (drives and destruction, filters, delays, reverbs, granular, resonators,
  cuts and repeats, spectral effects, vocoders), with values in real units.
- **Generators** that draw melodies, rhythms, sounds, effects and their parameters at random, at the density you set.
- **A master chain**: ABRASION, CORROSION, MODULATION, compressor, delay and reverb.
- **Session banks**, saved and loaded only when you do it.
- **The audio and USB stack**: low-latency ALSA output, hosting an M-VAVE FM-1's multitrack audio and MIDI clock,
  and class-compliant USB audio out.

Each instrument puts them to a different use: one sequences the engines, another runs effects over external
audio, another grows networks of effects. New instruments arrive with new builds, and the installer adds them in
place.

### On the platform today

Each instrument has a tile on the home screen:

- **TRACKER** — a 12-track generative tracker. Each track has five independently clocked columns (NOTE, MOD,
  INST, FX1, FX2), its own length, clock and one of 19 playback directions, conditional events, per-step effects
  and its own twelve sounds from the synthesis engines. Every column can be drawn at random (or euclidean) at the density you choose;
  melodies follow 38 scales. 32 patterns per session, 64 sessions.
- **FX** — a twelve-slot effect rack for the multitrack USB audio of an M-VAVE FM-1 (running Bigorna, Tonitor,
  Lilliput or Norilsk): the Brick hosts the FM-1 over USB, takes its separate tracks and passes each through two
  insert slots, each slot with its own 16-step parameter sequencer, clocked by the FM-1's MIDI clock.
- **LIFEBLOOD** — the FM-1's tracks sent through a generated network of effects, drawn as a living vascular
  field: nodes, vessels and corpuscles flowing from IN to OUT. Networks grow from recipes, rewire on demand and
  keep mutating on their own (angiogenesis).

While its USB port is not hosting an FM-1, the Brick is also a class-compliant USB audio input: plug it into a
computer or phone and record its stereo output without drivers.

## Why a whole operating system

The stock firmware is a general-purpose gaming system: a frontend, a service manager, networking, Bluetooth,
an overlay filesystem and dozens of background processes, all competing with an audio engine that must deliver
a block every 5.8 milliseconds. We went for the greatest optimisation possible, and that meant owning the whole
userspace:

- **BusyBox init, built from source**, a read-only root of about 13 MB, a dedicated supervisor and nothing
  else running: no procd, no ubus, no network stack, no SD-card hooks.
- **Only what the hardware needs is kept from the stock firmware**: the board's Linux 4.9 kernel (unchanged), the
  kernel modules and three small board helpers (gamepad input, power and volume keys, board settings),
  individually listed and hashed. See [VENDOR-COMPONENTS](installer/VENDOR-COMPONENTS.txt).
- **A custom boot manager and RAM recovery** in the boot image: it verifies images by SHA-256 before they are
  written, writes the root partition only from RAM, and falls back to a USB (ADB) recovery shell instead of a
  half-written system.
- **Audio first**: the CPU performance governor while the instruments run, real-time scheduling for USB capture,
  playback and the DSP workers spread over the cores, and a codec configuration verified on the hardware.

## Installing

> [!WARNING]
> **Amalthea replaces the stock operating system of your Brick.** Install it at your own risk. It comes with
> no warranty of any kind (GPL-3.0, sections 15 and 16). It has been developed and tested on one TRIMUI Brick
> (TG3040) running stock firmware 1.0.6; other stock versions are untested. The installer saves a copy of the
> stock system on your computer first, and `restore.sh` / `restore.bat` writes it back.

You need the Brick charged to at least 50 %, a USB-C **data** cable and a computer running macOS, Linux or
Windows. The stock firmware exposes ADB over USB, which is all the installer uses; if `adb` is not installed,
the installer offers to download Google's platform-tools.

1. Download `amalthea-<build>-installer.zip` from the [latest release](https://github.com/nzimas/amalthea/releases/latest) and unzip it.
2. Switch the Brick on (stock system, as it boots) and connect it to the computer.
3. Run the installer:
   - macOS / Linux: open a terminal in the unzipped folder and run `./install.sh`
   - Windows: double-click `install.bat`
4. Confirm with `yes`. The installer then, on its own:
   1. checks the device (TG3040, partition sizes, battery);
   2. copies the stock boot and root partitions into the Brick's internal storage and then to your computer
      (`backup-<serial>/`, about 515 MB, checked by SHA-256). **Keep that folder**;
   3. copies Amalthea to the Brick and verifies every file;
   4. writes the Amalthea boot partition, checks it, and restarts the Brick;
   5. the Brick's new boot manager writes the root filesystem from RAM, checks it and starts Amalthea.

The whole process takes about five minutes. Only the boot partition (p4) and the root partition (p5) are
written. The bootloader, the partition table, the stock settings partition, the internal data partition, the
SD card and your files on them are left alone.

**Updating:** run the same installer from a newer release with Amalthea running (stay on the home screen). It
copies your saved sessions to your computer first, then updates; when only the instruments changed, it does
not even restart the Brick.

**Going back to stock:** `./restore.sh` (or `restore.bat`). The Brick restarts into Amalthea's RAM recovery,
writes your stock backup to both partitions, checks them and restarts into the stock system exactly as it was.

The full guide, with troubleshooting, is at <https://nzimas.github.io/amalthea/install.html>.

## Using it

The [manual](MANUAL.md) (also [on the website](https://nzimas.github.io/amalthea/manual.html)) covers every
control of every instrument. The essentials:

| Button | Everywhere |
| --- | --- |
| Cross | Move |
| Start (bottom right) | Enter: open, begin and commit an edit |
| Menu (bottom left) | Play / stop |
| L1 / R1 | Shift / FUNC |
| L2 / R2 | Back / Home of the current appliance |
| X | Draw at random (tap for options, hold to draw) |
| FUNC, then Shift, both held | Leave the appliance (with a save prompt) |
| Power, held 0.8 s | Power-off prompt |

Every start is **blank and stopped**. Nothing is saved or recalled behind your back: sessions are saved and
loaded only when you do it, from the bank pages.

## Repository

| Folder | What |
| --- | --- |
| [app/](app) | The Amalthea appliance (Tracker, FX, LIFEBLOOD): C++17 and C, no UI framework, renders straight to the framebuffer and ALSA. Also builds for macOS as a simulator. |
| [system/](system) | Amalthea Linux: the boot manager and RAM installer (initramfs), the root filesystem scripts, and the tools that assemble the root from an empty directory and pack the boot image. |
| [installer/](installer) | The installer: `install.sh` / `restore.sh` (macOS, Linux), `amalthea.ps1` with `install.bat` / `restore.bat` (Windows), and the device-side script. |
| [docs/](docs) | The website (GitHub Pages), generated by `tools/make_site.py` from `site/`, `MANUAL.md` and `CHANGELOG.md`. |
| [tools/](tools) | Release tooling: `sync_sources.sh`, `make_release.sh`, `make_site.py`. |

### Building from source

The appliance builds natively on macOS (Apple Silicon) as a simulator, and for the Brick in an Ubuntu 20.04
AArch64 container:

```sh
cd app
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release && cmake --build build/mac -j8   # simulator and tests
sh tools/build_brick.sh                                                            # Brick binary + tests (Docker)
```

The system images are assembled in a container from the BusyBox source (checksum-pinned in
`system/sources.lock.json`), the appliance binary and the retained board files. The board files and the kernel
come from your own stock backup, the one `install.sh` saves; no vendor file is needed from anywhere else:

```sh
cd system
mkdir -p build && cp ../installer/backup-<serial>/stock-boot.img build/boot.img     # the kernel is taken from it
docker build -t amalthea-linux-build:ubuntu20 .                                    # needs app's amalthea-build:ubuntu20
docker run --rm -v "$PWD:/work" -v "$PWD/../installer/backup-<serial>:/backup:ro" amalthea-linux-build:ubuntu20 \
  sh -c 'debugfs -R "rdump / /work/donor" /backup/stock-rootfs.img'                # the stock root, read-only source
docker run --rm -v "$PWD:/work" -v "$PWD/../app:/app:ro" amalthea-linux-build:ubuntu20 sh -c \
  "sh tools/build_busybox.sh && python3 tools/assemble.py && python3 tools/make_images.py && python3 tools/validate_images.py"
```

See [system/README.md](system/README.md) for what each step checks, and `tools/make_release.sh` for the installer
archive.

## Licence

Amalthea is free software under the [GNU GPL v3](LICENSE). It contains ports of, and code from, many open-source
projects; each is credited in its source file and in [app/docs/EFFECTS-AND-REFERENCES.md](app/docs/EFFECTS-AND-REFERENCES.md),
[app/docs/NATIVE-EFFECTS.md](app/docs/NATIVE-EFFECTS.md), [app/docs/INSERT-PORTS.md](app/docs/INSERT-PORTS.md) and
[app/docs/ENGINE-PORTS.md](app/docs/ENGINE-PORTS.md). The vendor components in the images are listed in
[installer/VENDOR-COMPONENTS.txt](installer/VENDOR-COMPONENTS.txt); they remain the property of their owners.
TRIMUI and Brick are trademarks of their owner; this project is not affiliated with or endorsed by TRIMUI.

Developed by [Nuno Zimas](https://nunozimas.com).
