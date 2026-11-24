# Browser installer (experimental prototype)

`docs/web-install.html` (source `site/web-install.html` + `docs/js/web-install.js`, built by `tools/make_site.py`)
runs the command-line installer's steps from Chrome or Edge over WebUSB. It is linked from `install.html` as
experimental and is **not yet tested on a real Brick**.

## What it does

The same sequence as `install.sh` / `restore.sh` (`lib/common.sh`), and the same device side: it pushes
`payload/*` to `/mnt/UDISK/amalthea-install` and runs `busybox sh amalthea-device.sh probe|backup|stage|arm|
retry-install|restore-prepare|restore-write`, reading the `LOG` and `RESULT=` lines. Every command goes through
the `shell:` service (Tango's `subprocess.noneProtocol` uses `exec:`, which the stock adbd refuses, so the page
never uses it). Install from stock, update (sessions copied first; instruments-only restart when the root is
current), retry from RAM recovery, and restore (pushing the computer's backup back when the Brick has none) are
all there, with the same checks and messages.

- **Payload:** the user downloads `amalthea-<build>-installer.zip` from the latest release and chooses it in the
  page; the page unzips `payload/` in memory and checks `SHA256SUMS` before anything is sent. It cannot fetch the
  asset itself: GitHub's release storage (`release-assets.githubusercontent.com`) sends no CORS header (checked
  2026-11-25; the API's release metadata does, and the page uses it to link the latest archive). Mirroring the
  15 MB archive into `docs/` at release time would allow a one-click download, at 15 MB of git history per build.
- **Backups:** written with the File System Access API into a folder the user picks: `backup-<serial>/`
  (hashed while it streams, then checked against the Brick's `SHA256SUMS`) and `sessions-<serial>-<date>/`,
  as the CLI does. An existing verified `backup-<serial>` is reused.
- **Reboot:** after `reboot`, the page waits (400 s, like `wait_for_amalthea`) for the device to come back in
  `navigator.usb.getDevices()` and probes it. If it has not reappeared after 45 s, it also offers a button to
  choose it again (a WebUSB permission prompt needs a click).
- **Library:** Tango (ya-webadb) 2.6.4 + fflate 0.8.2 + @noble/hashes 2.0.1, pinned in `tools/webadb/package.json`
  and lock file, bundled into `docs/js/vendor/webadb.js` by `tools/make_webadb.sh` (licences alongside).

## How it was tested

`installer/tests/web/run.sh` runs the page in headless Chrome against the simulated Brick of `installer/tests`
(Docker container; `SIM_CONTAINER`, default `amalthea-websim`, so it does not clash with `../run.sh`).
`fake_adbd.py` stands in for the stock daemon: ADB protocol 0x01000000, 4 KB payloads, checksums checked, no
features, `exec:` refused, sync v1 only, no exit codes, the transport dropped at `reboot` and the device absent for
a few seconds; optionally (default in run.sh) it asks for ADB authentication. `?sim=ws://…` makes the page speak
ADB over a WebSocket instead of WebUSB; nothing else in the page changes. Scenarios: install from stock, update,
restore from the computer's backup (stock root bit for bit), interrupted root write + retry, low battery refused.

## What still needs a real Brick

1. **WebUSB access to the stock adbd:** Chrome's chooser lists it (ADB interface class ff/42/01), `claimInterface`
   succeeds (macOS: no `adb` server running; Windows: WinUSB/Google driver bound; Linux: udev rule for 18d1), and
   the CNXN handshake completes. Whether the stock daemon asks for authentication is unknown; the page handles it
   if it does (key kept in IndexedDB), but nothing on the stock firmware is known to prompt for accepting a key.
2. **Speed and stability of the 515 MB pull** over WebUSB with 4 KB packets (about 14 MB/s over the WebSocket
   emulation; USB will differ), including the Windows `NetworkError` quirk Tango works around.
3. **Reconnection after reboot:** whether the Brick under Amalthea (`amalthea-usb-recovery` sets 18d1:d002 and the
   serial from `androidboot.serialno`) comes back as the same WebUSB device so the permission carries over, and
   likewise in RAM recovery and back in stock after a restore. If not, the page's "Choose the Brick" button covers it.
4. **Long silent shell commands** (`backup`, `stage`, `restore-write`, minutes without output) over USB.
5. **Browser support** beyond desktop Chrome on macOS (headless) — Edge, Windows, Linux, ChromeOS untested.
