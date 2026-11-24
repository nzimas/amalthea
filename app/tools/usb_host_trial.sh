#!/bin/sh
# One-shot bottom-port trial. Start detached; waits for the laptop to be unplugged.
# No persistent role change; device mode is restored after 600 seconds or on error.
BASE=/mnt/UDISK/tracker-linux
CTRL=/sys/devices/platform/soc/usbc0
STATE=/sys/class/udc/5100000.udc-controller/state
LOG="$BASE/logs/usb/host-trial.txt"
mkdir -p "$BASE/logs/usb"
exec >> "$LOG" 2>&1
trap '' HUP
switched=0
restore() {
  if test "$switched" = 1; then
    echo 'Restoring bottom port to device mode'
    echo 0 > "$CTRL/axp_5v"
    cat "$CTRL/usb_device"
    switched=0
  fi
}
trap 'restore' EXIT
trap 'exit 1' INT TERM
echo 'Waiting up to 600 seconds for laptop disconnect; no role change while connected'
cat /proc/uptime
left=600
while test "$left" -gt 0; do
  state=$(cat "$STATE")
  if test "$state" = 'not attached'; then break; fi
  sleep 1
  left=$((left - 1))
done
if test "$left" -eq 0; then echo 'Timed out; role unchanged'; exit 1; fi
sleep 2
if test "$(cat "$STATE")" != 'not attached'; then echo 'Reconnected; cancelling'; exit 1; fi
switched=1
cat "$CTRL/usb_host"
echo 1 > "$CTRL/axp_5v"
echo 'Host trial active for 600 seconds'
cat /proc/uptime
sleep 600
# Preserve the connected-device evidence before returning to device mode.
cp "$BASE/logs/usb/latest.txt" "$BASE/logs/usb/host-trial-snapshot.txt"
cat /proc/asound/cards
cat /proc/uptime
restore
