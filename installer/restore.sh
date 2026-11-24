#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Put the stock TRIMUI system back on a Brick running Amalthea (macOS and Linux).
#
#   ./restore.sh                     use the backup taken by ./install.sh (backup-<serial>/)
#   ./restore.sh path/to/backup-dir  use the backup in that folder
#
# The Brick restarts into Amalthea's RAM recovery, writes the stock root and boot
# partitions from the backup, checks them, and restarts into the stock system.
# Saved Amalthea sessions on internal storage are left where they are.
set -eu
cd "$(dirname "$0")"
HERE=$(pwd)
. "$HERE/lib/common.sh"

YES=0; LOCAL_BACKUP=
for a in "$@"; do
  case "$a" in
    --yes|-y) YES=1 ;;
    --help|-h) sed -n '3,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) die "Unknown option: $a" ;;
    *) LOCAL_BACKUP=$a ;;
  esac
done

banner "Restore the stock TRIMUI system"
confirm "Replace Amalthea with the stock system from your backup?"

find_adb
wait_for_brick
push_installer
probe
[ "$P_HW" = TG3040 ] || die "The connected device is not a TRIMUI Brick (TG3040). Nothing was changed."

case "$P_SYSTEM" in
  stock) say "This Brick is already running the stock system. Nothing to do."; exit 0 ;;
  amalthea)
    if [ "$P_BACKUP" != present ]; then
      [ -n "$LOCAL_BACKUP" ] || LOCAL_BACKUP="$HERE/backup-$SERIAL"
      [ -f "$LOCAL_BACKUP/SHA256SUMS" ] || die "No stock backup on the Brick and none at $LOCAL_BACKUP. Pass the backup folder: ./restore.sh path/to/backup-folder"
      (cd "$LOCAL_BACKUP" && check_sums SHA256SUMS) || die "The backup in $LOCAL_BACKUP is damaged."
      step "Copying the stock backup to the Brick"
      adb_ shell "mkdir -p $DEV_BACKUP" >/dev/null
      for f in stock-boot.img stock-rootfs.img SHA256SUMS; do push "$LOCAL_BACKUP/$f" "$DEV_BACKUP/$f"; done
    fi
    step "Preparing recovery"
    device restore-prepare
    step "Restarting into Amalthea's RAM recovery"
    adb_ shell reboot >/dev/null 2>&1 || true
    sleep 8
    wait_for_brick 180
    probe
    [ "$P_SYSTEM" = recovery ] || die "The Brick did not restart into recovery (it reports: $P_SYSTEM). Run ./restore.sh again."
    ;;
  recovery) ;;
  *) die "The Brick is running a system this installer does not recognise. Nothing was changed." ;;
esac

step "Writing the stock system (about a minute; keep the cable connected)"
device restore-write
step "Restarting into the stock system"
adb_ shell 'reboot -f' >/dev/null 2>&1 || true
say ""
say "Done. The Brick is restarting into the stock TRIMUI system."
say "Amalthea's saved sessions remain in internal storage (/mnt/UDISK/tracker-linux)."
