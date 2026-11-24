#!/bin/sh
# Builds usb_f_uac2.ko for the TG3040 vendor kernel (Linux 4.9.191 #804) in an
# arm64 Ubuntu 18.04 container (GCC 7.5). Needs the device kernel config as
# device.config (zcat /proc/config.gz). Output: $WORK/uac2mod/usb_f_uac2.ko
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=${WORK:-$HERE/../../build/uac2_gadget}
mkdir -p "$WORK/uac2mod"
cd "$WORK"
test -f device.config || { echo "Put the Brick's kernel config at $WORK/device.config" >&2; exit 1; }
if test ! -d linux-4.9.191; then
  test -f linux-4.9.191.tar.xz || curl -sSfLO https://cdn.kernel.org/pub/linux/kernel/v4.x/linux-4.9.191.tar.xz
  test -f sha256sums.asc || curl -sSfLO https://cdn.kernel.org/pub/linux/kernel/v4.x/sha256sums.asc
  grep ' linux-4.9.191.tar.xz$' sha256sums.asc | shasum -a 256 -c
  tar xJf linux-4.9.191.tar.xz
  (cd linux-4.9.191 && patch -p1 < "$HERE/tg3040-layout.patch")
fi
tr -d '\r' < device.config > linux-4.9.191/.config
cp linux-4.9.191/drivers/usb/gadget/function/f_uac2.c linux-4.9.191/drivers/usb/gadget/function/u_uac2.h uac2mod/
(cd uac2mod && patch -p0 f_uac2.c < "$HERE/f_uac2-in-only.patch")
printf 'obj-m := usb_f_uac2.o\nusb_f_uac2-y := f_uac2.o\n' > uac2mod/Kbuild
printf 'FROM ubuntu:18.04\nRUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gcc make bc bison flex libc6-dev binutils python3 && rm -rf /var/lib/apt/lists/*\nWORKDIR /k\n' > Dockerfile
docker build -q -t uac-kbuild:18.04 . >/dev/null
docker run --rm -v "$WORK:/k" uac-kbuild:18.04 sh -c 'cd linux-4.9.191 && make ARCH=arm64 olddefconfig >/dev/null && make ARCH=arm64 -j8 modules_prepare >/dev/null && make ARCH=arm64 M=/k/uac2mod modules'
shasum -a 256 uac2mod/usb_f_uac2.ko
