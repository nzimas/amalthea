#!/bin/sh
# UAC2 feasibility trial 2. RAM-only. Stages the audio function while ADB is bound,
# then lets adbd rebind. Restores ADB-only + device role unless /tmp/uac2-keep appears,
# and immediately if the vendor scanner switches the port to host mode.
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
G=/sys/kernel/config/usb_gadget/g1
F=$G/functions/uac2.usb0
L=$G/configs/c.1/uac2.usb0
UDC=5100000.udc-controller
P=/sys/devices/platform/soc/usbc0
exec >>/tmp/uac2-trial.log 2>&1
echo "== trial2 start $(cat /proc/uptime)"
rm -f /tmp/uac2-keep
device_role() {
  if test "$(cat $P/otg_role)" != usb_device; then
    echo 0 > $P/axp_5v; cat $P/usb_device >/dev/null; echo "role restored: $(cat $P/otg_role)"
  fi
}
revert() {
  echo "== revert ($1) $(cat /proc/uptime)"
  rm -f $L
  echo 0x00 > $G/bDeviceClass; echo 0x00 > $G/bDeviceSubClass; echo 0x00 > $G/bDeviceProtocol
  echo "" > $G/UDC
  device_role
  n=0; while test -z "$(cat $G/UDC)" && test $n -lt 20; do usleep 100000; n=$((n+1)); done
  test -n "$(cat $G/UDC)" || echo $UDC > $G/UDC
  echo "reverted udc=$(cat $G/UDC) role=$(cat $P/otg_role)"
}
grep -q '^usb_f_uac2 ' /proc/modules || insmod /tmp/usb_f_uac2.ko || { echo insmod-failed; exit 1; }
test -d $F || mkdir $F || { echo mkdir-failed; exit 1; }
echo 3 > $F/p_chmask; echo 48000 > $F/p_srate; echo 2 > $F/p_ssize; echo 0 > $F/c_chmask
test -e $L || ln -s $F $L || { echo link-failed; exit 1; }
ls $G/configs/c.1
echo 0xEF > $G/bDeviceClass; echo 0x02 > $G/bDeviceSubClass; echo 0x01 > $G/bDeviceProtocol
echo "" > $G/UDC
echo "unbound $(cat /proc/uptime)"
n=0; while test -z "$(cat $G/UDC)" && test $n -lt 20; do usleep 100000; n=$((n+1)); done
test -n "$(cat $G/UDC)" || { echo "self-bind"; echo $UDC > $G/UDC || echo bind-failed; }
echo "bound udc=$(cat $G/UDC) after ${n}x100ms $(cat /proc/uptime)"
usleep 500000
cat /proc/asound/cards
# No time limit: wait until the owner approves the accessory on the host.
# Revert only if the vendor scanner moves the port to host mode.
while :; do
  test -e /tmp/uac2-keep && { echo "== kept $(cat /proc/uptime)"; exit 0; }
  test "$(cat $P/otg_role)" = usb_device || { revert host-switch; exit 1; }
  sleep 1
done
