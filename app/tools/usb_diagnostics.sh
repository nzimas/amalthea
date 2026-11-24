#!/bin/sh
# Bounded, passive USB/ALSA evidence. Never opens PCM or changes USB roles.
BASE=/mnt/UDISK/tracker-linux
LOG="$BASE/logs/usb"
mkdir -p "$LOG"
PID=/tmp/amalthea-usb-diagnostics.pid
if test -r "$PID"; then
  read old < "$PID"
  if test -n "$old" && kill -0 "$old" 2>/dev/null &&
     ! grep -q "State:.*Z" "/proc/$old/status" 2>/dev/null; then exit 0; fi
fi
echo $$ > "$PID"
trap 'rm -f "$PID" /tmp/amalthea-usb-snapshot.$$' EXIT
previous=
while :; do
  snapshot=/tmp/amalthea-usb-snapshot.$$
  {
    echo '=== USB devices ==='
    for d in /sys/bus/usb/devices/*; do
      test -r "$d/idVendor" || continue
      echo "DEVICE ${d##*/}"
      for f in idVendor idProduct manufacturer product bcdDevice speed authorized bConfigurationValue busnum devnum; do
        if test -r "$d/$f"; then printf '%s: ' "$f"; cat "$d/$f"; fi
      done
    done
    echo '=== USB role / host power ==='
    for f in /sys/class/usb_role/*/role /sys/class/udc/*/state /sys/devices/platform/soc/usbc0/otg_role; do
      if test -r "$f"; then echo "$f"; cat "$f"; fi
    done
    echo '=== ALSA cards and devices ==='
    cat /proc/asound/cards /proc/asound/pcm /proc/asound/devices 2>/dev/null
    for f in /proc/asound/card*/usbid /proc/asound/card*/stream* /proc/asound/card*/midi* /proc/asound/card*/pcm*/sub*/hw_params; do
      if test -r "$f"; then echo "$f"; head -c 16384 "$f"; fi
    done
    echo '=== FX host status ==='
    cat /tmp/amalthea-usb-status 2>/dev/null
    echo '=== Audio health ==='
    cat /tmp/amalthea-audio-health 2>/dev/null
    echo '=== USB kernel messages ==='
    dmesg 2>/dev/null | grep -i -E 'usb|ehci|ohci|snd|audio' | tail -n 100
  } > "$snapshot"
  current=$(sha256sum "$snapshot")
  if test "$current" != "$previous"; then
    test -f "$LOG/latest.txt" && mv "$LOG/latest.txt" "$LOG/previous.txt"
    { date -u; cat /proc/uptime; cat "$snapshot"; } > "$LOG/latest.txt"
    if grep -q "idVendor: 1209" "$snapshot"; then
      cp "$LOG/latest.txt" "$LOG/fm1-last-seen.txt"
    fi
    previous=$current
  fi
  sleep 2
done
