#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The installer against a simulated Brick: an arm64 container whose "partitions" are files, driven by
# mock_adb.py (which also simulates the reboot into Amalthea's RAM installer). Runs the real device-side
# script under the real static BusyBox. Needs Docker on an arm64 host (Apple Silicon) and a release
# archive: tools/make_release.sh first.
#   install from stock, update (instruments only), restore, interrupted root write + retry, low battery
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"; docker rm -f amalthea-sim >/dev/null 2>&1 || true' EXIT
ZIP=$(ls "$ROOT"/build/amalthea-*-installer.zip | tail -n 1)
(cd "$WORK" && unzip -q "$ZIP")
I=$(ls -d "$WORK"/amalthea-*-installer)
export ADB="$HERE/mock_adb.py"
sh "$HERE/sim_setup.sh" "$I" >/dev/null
state() { docker exec amalthea-sim cat /sim/state; }
expect() { [ "$(state)" = "$1" ] || { echo "FAIL: $2 (state $(state))"; exit 1; }; echo "ok: $2"; }

"$I/install.sh" --yes >/dev/null; expect amalthea "install from stock"
[ -f "$I"/backup-SIMSERIAL01/SHA256SUMS ] && echo "ok: stock backup on the computer"
"$I/install.sh" --yes | grep -q "Restarting the instruments"; echo "ok: update restarts only the instruments"
docker exec amalthea-sim rm -rf /mnt/UDISK/amalthea-stock-backup
"$I/restore.sh" --yes >/dev/null; expect stock "restore from the computer's backup"
[ "$(docker exec amalthea-sim sh -c 'sha256sum /sim/p5 | cut -d " " -f 1')" = "$(docker exec amalthea-sim cat /sim/stockroot.sha)" ] && echo "ok: stock root restored bit for bit"
docker exec amalthea-sim sh -c 'touch /sim/fail-root-write'
"$I/install.sh" --yes >/dev/null 2>&1 || true; expect recovery "interrupted root write stops in recovery"
docker exec amalthea-sim rm /sim/fail-root-write
"$I/install.sh" --yes >/dev/null; expect amalthea "retry from recovery"
"$I/restore.sh" --yes >/dev/null; docker exec amalthea-sim sh -c 'echo 35 > /sim/batt/capacity'
if "$I/install.sh" --yes >/dev/null 2>&1; then echo "FAIL: low battery accepted"; exit 1; fi; expect stock "low battery refused, nothing changed"
echo "all installer scenarios passed"
