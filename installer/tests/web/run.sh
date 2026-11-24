#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The browser installer (docs/web-install.html) against the simulated Brick of installer/tests, the same
# scenarios as ../run.sh: install from stock, update (instruments only), restore from the computer's backup,
# interrupted root write + retry, low battery. The page runs in headless Chrome (a throwaway profile; WebUSB
# is not used: ?sim= puts ADB on a WebSocket to fake_adbd.py, an emulation of the stock firmware's old adbd).
# Backups go to the profile's origin-private file system, which the restore scenario reads back.
# Needs Docker on an arm64 host, Google Chrome, and a release archive (tools/make_release.sh).
#   AUTH=0   do not make the daemon ask for ADB authentication     PORT=18765   the daemon's port
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
export SIM_CONTAINER=${SIM_CONTAINER:-amalthea-websim}
PORT=${PORT:-18765}
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
WORK=$(mktemp -d)
ADBD=; CPID=
cleanup() {
  [ -n "$CPID" ] && kill "$CPID" 2>/dev/null || true
  [ -n "$ADBD" ] && kill "$ADBD" 2>/dev/null || true
  docker rm -f "$SIM_CONTAINER" >/dev/null 2>&1 || true
  rm -rf "$WORK"
}
trap cleanup EXIT
ZIP=$(ls "$ROOT"/build/amalthea-*-installer.zip | tail -n 1)
(cd "$WORK" && unzip -q "$ZIP")
I=$(ls -d "$WORK"/amalthea-*-installer)
sh "$HERE/../sim_setup.sh" "$I" >/dev/null
AUTHFLAG=--auth; [ "${AUTH:-1}" = 0 ] && AUTHFLAG=
python3 "$HERE/fake_adbd.py" --port "$PORT" --zip "$ZIP" --result "$WORK/result.json" --boot-seconds 5 $AUTHFLAG \
  2> "$WORK/adbd.log" &
ADBD=$!
sleep 1
state() { docker exec "$SIM_CONTAINER" cat /sim/state; }
expect() { [ "$(state)" = "$1" ] || { echo "FAIL: $2 (state $(state))"; tail -n 20 "$WORK/adbd.log"; exit 1; }; echo "ok: $2"; }

# page FLOW: run the page's install or restore flow to the end; prints "ok" or "fail: <error>"
page() {
  rm -f "$WORK/result.json"
  "$CHROME" --headless=new --user-data-dir="$WORK/profile" --no-first-run --no-default-browser-check \
    "http://127.0.0.1:$PORT/web-install.html?sim=ws://127.0.0.1:$PORT/adb&zip=/payload.zip&opfs&yes&auto=$1" \
    >/dev/null 2>&1 &
  CPID=$!
  n=0
  until [ -s "$WORK/result.json" ]; do
    n=$((n + 1)); [ $n -lt 600 ] || { echo "FAIL: $1 did not finish"; exit 1; }
    sleep 1
  done
  kill $CPID 2>/dev/null || true; wait $CPID 2>/dev/null || true; CPID=
  python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("ok" if d["result"]=="ok" else "fail: "+d["error"])
open(sys.argv[2],"w").write("\n".join(d["log"])+"\n")' "$WORK/result.json" "$WORK/log.txt"
}

[ "$(page install)" = ok ] || { cat "$WORK/log.txt"; exit 1; }; expect amalthea "install from stock"
grep -q "Saved to .*backup-SIMSERIAL01" "$WORK/log.txt" && echo "ok: stock backup on the computer (OPFS)"
[ "$(page install)" = ok ] && grep -q "Restarting the instruments" "$WORK/log.txt" && echo "ok: update restarts only the instruments"
grep -q "Saved sessions copied" "$WORK/log.txt" && echo "ok: sessions copied before the update"
docker exec "$SIM_CONTAINER" rm -rf /mnt/UDISK/amalthea-stock-backup
[ "$(page restore)" = ok ] || { cat "$WORK/log.txt"; exit 1; }; expect stock "restore from the computer's backup"
grep -q "Copying the stock backup to the Brick" "$WORK/log.txt" && echo "ok: backup pushed back from the computer"
[ "$(docker exec "$SIM_CONTAINER" sh -c 'sha256sum /sim/p5 | cut -d " " -f 1')" = "$(docker exec "$SIM_CONTAINER" cat /sim/stockroot.sha)" ] \
  && echo "ok: stock root restored bit for bit"
grep -q "running the stock TRIMUI system again" "$WORK/log.txt" && echo "ok: stock system seen after the restart"
docker exec "$SIM_CONTAINER" touch /sim/fail-root-write
r=$(page install); case "$r" in fail:*recovery*) ;; *) echo "FAIL: interrupted install reported: $r"; exit 1 ;; esac
expect recovery "interrupted root write stops in recovery"
docker exec "$SIM_CONTAINER" rm /sim/fail-root-write
[ "$(page install)" = ok ] || { cat "$WORK/log.txt"; exit 1; }; expect amalthea "retry from recovery"
[ "$(page restore)" = ok ] || { cat "$WORK/log.txt"; exit 1; }
docker exec "$SIM_CONTAINER" sh -c 'echo 35 > /sim/batt/capacity'
r=$(page install); case "$r" in fail:*battery*) ;; *) echo "FAIL: low battery accepted: $r"; exit 1 ;; esac
expect stock "low battery refused, nothing changed"
if grep -q "bad checksum\|packet before connection" "$WORK/adbd.log"; then echo "FAIL: protocol errors"; grep "bad checksum\|before" "$WORK/adbd.log" | head; exit 1; fi
echo "ok: no protocol errors seen by the old-adbd emulation${AUTHFLAG:+ (with ADB authentication)}"
echo "all browser installer scenarios passed"
