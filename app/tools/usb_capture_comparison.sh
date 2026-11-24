#!/bin/sh
# Run detached with the appliance dashboard visible. No session operations.
# Ten minutes for each cable swap, then an eight-second standalone ingress test.
set -eu
mode=${1:-alsa}
case "$mode" in alsa|packets) ;; *) echo 'Expected alsa or packets'; exit 2 ;; esac
BASE=/mnt/UDISK/tracker-linux
CTRL=/sys/devices/platform/soc/usbc0
STATE=/sys/class/udc/5100000.udc-controller/state
OUT="$BASE/logs/audio/comparison"
mkdir -p "$OUT"
exec >> "$OUT/test.log" 2>&1
trap '' HUP
test ! -e /tmp/amalthea-usb-role-owner || { echo 'Exit FX before running comparison'; exit 1; }
switched=0
power=$(cat "$CTRL/axp_5v")
case "$power" in 0|1) ;; *) echo 'Unknown original port power; refusing test'; exit 1 ;; esac
restore() {
 if test "$switched" = 1; then
  echo "$power" > "$CTRL/axp_5v"
  cat "$CTRL/usb_device"
  switched=0
  echo 'Device mode restored; reconnect laptop to retrieve recording'
 fi
}
trap restore EXIT
trap 'exit 1' INT TERM
echo 'Waiting up to ten minutes for laptop removal. Stay on appliance dashboard.'
date -u
left=600
while test "$(cat "$STATE")" != 'not attached'; do
 test "$left" -gt 0 || exit 1
 sleep 1;left=$((left-1))
done
sleep 3
test "$(cat "$STATE")" = 'not attached' || exit 1
test ! -e /tmp/amalthea-usb-role-owner || exit 1
switched=1
cat "$CTRL/usb_host"
echo 1 > "$CTRL/axp_5v"
echo 'Waiting up to ten minutes for Bigorna. Keep its sequence stopped.'
left=600
while ! grep -q Bigorna /proc/asound/cards; do
 test "$left" -gt 0 || exit 1
 sleep 1;left=$((left-1))
done
sleep 2
cat /proc/asound/cards
if test "$mode" = packets; then
 "$BASE/bin/fm1_packet_probe" "$OUT/usbfs-matched.wav"
else
 LD_LIBRARY_PATH=/usr/trimui/lib:/usr/lib:/lib "$BASE/bin/fm1_capture_probe" "$OUT/brick-matched.wav"
fi
for f in /proc/asound/card*/stream*; do test ! -r "$f" || cat "$f"; done
dmesg | tail -n 80
restore
