#!/bin/sh
export PATH=/sbin:/bin:/usr/sbin:/usr/bin
G=/sys/kernel/config/usb_gadget/g1
exec >>/tmp/uac2-trial.log 2>&1
echo "== manual revert $(cat /proc/uptime)"
echo "" > $G/UDC; rm -f $G/configs/c.1/uac2.usb0
echo 0x00 > $G/bDeviceClass; echo 0x00 > $G/bDeviceSubClass; echo 0x00 > $G/bDeviceProtocol
test -n "$(cat $G/UDC)" || echo 5100000.udc-controller > $G/UDC
rmdir $G/functions/uac2.usb0; rmmod usb_f_uac2
echo "reverted udc=$(cat $G/UDC)"
