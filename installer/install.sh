#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Amalthea installer for the TRIMUI Brick (TG3040), macOS and Linux.
#
#   ./install.sh            install Amalthea on a stock Brick, or update an Amalthea Brick
#   ./install.sh --yes      the same, without the confirmation prompt
#
# It needs only a USB-C data cable and ADB (offered for download if it is missing).
# On a stock Brick it first copies the stock boot and root partitions to this computer
# (backup-<serial>/), which ./restore.sh can write back later.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
. "$HERE/lib/common.sh"

YES=0
for a in "$@"; do
  case "$a" in
    --yes|-y) YES=1 ;;
    --help|-h) sed -n '3,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) die "Unknown option: $a" ;;
  esac
done

banner "Amalthea installer · build $(cat payload/BUILD)"
cat <<'EOF'
Amalthea is not an app on top of the stock firmware. It REPLACES the Brick's
operating system: the boot and root partitions are overwritten by Amalthea Linux,
a minimal custom distribution built around the Amalthea instruments. The
gaming frontend, emulators and every other stock feature will be gone until
you restore the stock system with ./restore.sh.

Your SD card, the stock settings partition and internal data are not written.
A copy of the stock system is saved on this computer before anything changes.

You do this at your own risk. There is no warranty of any kind (see LICENSE).
EOF
echo
confirm "Install Amalthea on the connected Brick?"

find_adb
wait_for_brick
push_installer
probe

[ "$P_HW" = TG3040 ] || die "The connected device is not a TRIMUI Brick (TG3040). Nothing was changed."
case "$P_SYSTEM" in
  stock) install_from_stock ;;
  amalthea) update_amalthea ;;
  recovery) retry_from_recovery ;;
  *) die "The Brick is running a system this installer does not recognise. Nothing was changed." ;;
esac
