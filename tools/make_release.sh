#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Assemble the installer archive for a GitHub release:
#   build/amalthea-<build>-installer.zip   (the installer folder + payload/)
# Inputs (built in the development trees; see app/README.md and system/README.md):
#   APP_BUILD  ../amalthea/build                 brick/amalthea, uac2_gadget/uac2mod/usb_f_uac2.ko
#   SYS        ../amalthea-linux                 images/amalthea-boot.img, images/amalthea-rootfs.img, build/busybox
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
APP_BUILD=${APP_BUILD:-$HERE/../amalthea/build}
SYS=${SYS:-$HERE/../amalthea-linux}
BUILD=$(cat "$HERE/app/BUILD_NUMBER")
NAME=amalthea-$BUILD-installer
OUT=$HERE/build/$NAME
rm -rf "$OUT" "$OUT.zip"
mkdir -p "$OUT/payload" "$OUT/lib"

cp "$HERE/installer/install.sh" "$HERE/installer/restore.sh" "$HERE/installer/install.bat" \
   "$HERE/installer/restore.bat" "$HERE/installer/amalthea.ps1" "$OUT/"
cp "$HERE/installer/lib/common.sh" "$OUT/lib/"
cp "$HERE/installer/README.txt" "$OUT/README.txt"
cp "$HERE/LICENSE" "$OUT/LICENSE.txt"
cp "$HERE/installer/VENDOR-COMPONENTS.txt" "$OUT/VENDOR-COMPONENTS.txt"

P=$OUT/payload
echo "$BUILD" > "$P/BUILD"
cp "$HERE/installer/device/amalthea-device.sh" "$P/"
cp "$SYS/build/busybox" "$P/busybox"
cp "$SYS/images/amalthea-boot.img" "$P/"
gzip -9 -n -c "$SYS/images/amalthea-rootfs.img" > "$P/amalthea-rootfs.img.gz"
echo "$(shasum -a 256 "$SYS/images/amalthea-rootfs.img" | cut -d ' ' -f 1)  rootfs.img" > "$P/rootfs.sha256"
echo "$(shasum -a 256 "$P/amalthea-boot.img" | cut -d ' ' -f 1)  boot.img" > "$P/boot.sha256"
cp "$APP_BUILD/brick/amalthea" "$P/amalthea"
cp "$APP_BUILD/uac2_gadget/uac2mod/usb_f_uac2.ko" "$P/usb_f_uac2.ko"
file "$P/amalthea" | grep -q 'ARM aarch64' || { echo "payload/amalthea is not an AArch64 build" >&2; exit 1; }
file "$P/busybox" | grep -q 'statically linked' || { echo "payload/busybox must be static" >&2; exit 1; }
(cd "$P" && for f in *; do [ "$f" = SHA256SUMS ] || shasum -a 256 "$f"; done > SHA256SUMS)
chmod 755 "$OUT/install.sh" "$OUT/restore.sh" "$P/busybox"

(cd "$HERE/build" && rm -f "$NAME.zip" && zip -qr -X "$NAME.zip" "$NAME")
echo "release: build/$NAME.zip ($(du -h "$HERE/build/$NAME.zip" | cut -f 1))"
shasum -a 256 "$HERE/build/$NAME.zip"
