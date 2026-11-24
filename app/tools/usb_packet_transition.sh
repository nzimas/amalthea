#!/bin/sh
# Passive, bounded packet capture. Remains inert until the laptop is unplugged.
set -eu
BASE=/mnt/UDISK/tracker-linux
CTRL=/sys/devices/platform/soc/usbc0
STATE=/sys/class/udc/5100000.udc-controller/state
OUT=${1:?Supply a unique output directory}
case "$OUT" in /mnt/UDISK/tracker-linux/logs/audio/transition-*) ;; *) exit 2 ;; esac
test ! -e /tmp/amalthea-packet-transition.pid || exit 3
test ! -e /tmp/amalthea-usb-role-owner || { echo 'Exit FX first';exit 4; }
mkdir -p "$OUT"
exec >> "$OUT/test.log" 2>&1
trap '' HUP
echo $$ > /tmp/amalthea-packet-transition.pid
power=$(cat "$CTRL/axp_5v")
case "$power" in 0|1) ;; *) rm /tmp/amalthea-packet-transition.pid;exit 5 ;; esac
switched=0
status(){ echo "$*";echo "$*" > /tmp/amalthea-packet-transition.status;cat /proc/uptime; }
finish(){
 result=$?
 trap - EXIT
 if test "$switched" = 1; then
  echo "$power" > "$CTRL/axp_5v"
  cat "$CTRL/usb_device"
 fi
 echo "Exit status: $result" > "$OUT/result.txt"
 status "Finished: laptop mode restored; exit=$result"
 sync
 rm -f /tmp/amalthea-packet-transition.pid
 exit "$result"
}
trap finish EXIT
trap 'exit 130' INT TERM
status 'Armed: waiting up to ten minutes for laptop disconnect'
left=600
while test "$(cat "$STATE")" != 'not attached'; do
 test "$left" -gt 0 || exit 6
 sleep 1;left=$((left-1))
done
sleep 3
test "$(cat "$STATE")" = 'not attached' || exit 7
test ! -e /tmp/amalthea-usb-role-owner || exit 8
switched=1
cat "$CTRL/usb_host"
echo 1 > "$CTRL/axp_5v"
status 'Host: waiting up to ten minutes for Bigorna; keep FX closed'
left=600
while ! grep -q Bigorna /proc/asound/cards; do
 test "$left" -gt 0 || exit 9
 sleep 1;left=$((left-1))
done
sleep 2
status 'Recording 180 seconds: track 4 first; add kick near 60s; clear kick near 120s'
cat /proc/asound/cards
for f in /proc/asound/card*/stream*; do test ! -r "$f" || cat "$f";done
set +e
"$BASE/bin/fm1_packet_probe" "$OUT/transition.wav" 180 passive
result=$?
set -e
dmesg > "$OUT/kernel.log"
exit "$result"
