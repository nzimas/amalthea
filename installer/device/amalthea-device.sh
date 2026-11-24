# SPDX-License-Identifier: GPL-3.0-only
# Amalthea installer, device side. Runs on the TRIMUI Brick (TG3040) under the static
# BusyBox shipped next to it, so it never depends on the tools of the system it replaces:
#
#   /mnt/UDISK/amalthea-install/busybox sh /mnt/UDISK/amalthea-install/amalthea-device.sh COMMAND
#
# Every command ends by printing RESULT=OK or RESULT=FAIL <reason>; the host scripts read
# that line, because the Brick's ADB daemon does not report exit codes.
#
#   probe            what is this device and what is it running (KEY=VALUE lines)
#   backup           stock only: copy the stock boot and root partitions to internal storage
#   stage            verify the payload; unpack the root image where the boot manager reads it
#   arm              select the install mode and write the Amalthea boot partition
#   retry-install    RAM recovery only: select the install mode again (after an interrupted install)
#   restore-prepare  Amalthea only: check the stock backup, select RAM recovery for the next boot
#   restore-write    RAM recovery only: write the stock root and boot partitions back
#
# Partitions (fixed on the TG3040): p4 boot (15 MiB), p5 rootfs (500 MiB), p10 UDISK (data).
# Nothing else is ever written: not the bootloader, the partition table, the stock overlay (p6),
# the SD card or any saved session.

# (The ${X:-...} overrides exist only for the installer's own tests on image files.)
UDISK=${UDISK:-/mnt/UDISK}
BB=$UDISK/amalthea-install/busybox
ST=$UDISK/amalthea-install
BASE=$UDISK/amalthea-linux
BK=$UDISK/amalthea-stock-backup
BOOT=${BOOT_DEV:-/dev/mmcblk0p4}
ROOT=${ROOT_DEV:-/dev/mmcblk0p5}
SYSBLOCK=${SYSBLOCK:-/sys/class/block}
BATT=${BATT:-/sys/class/power_supply/axp2202-battery}
CPUINFO=${CPUINFO:-/proc/cpuinfo}
MOUNTS=${MOUNTS:-/proc/mounts}
BOOT_BYTES=15728640
ROOT_BYTES=524288000

# Every tool comes from the shipped BusyBox, whatever the running system provides.
mkdir_bin() {
  "$BB" mkdir -p "$ST/bin" || exit 1
  for a in cat chmod cp cut dd df echo grep gunzip head ls mkdir mv readlink rm sed sha256sum stat sync tail test tr uname; do
    "$BB" ln -sf "$BB" "$ST/bin/$a"
  done
}
mkdir_bin
export PATH="$ST/bin:/sbin:/bin:/usr/sbin:/usr/bin"

ok() { echo "RESULT=OK"; exit 0; }
fail() { echo "RESULT=FAIL $*"; exit 1; }
say() { echo "LOG $*"; }

part_bytes() { echo $(( $(cat "$SYSBLOCK/$1/size") * 512 )); }
hash_of() { sha256sum "$1" | cut -d ' ' -f 1; }
# the hash of the first N bytes of a block device (partitions are exactly image-sized here)
hash_dev() { dd if="$1" bs=1048576 count=$(( $2 / 1048576 )) 2>/dev/null | sha256sum | cut -d ' ' -f 1; }

system_kind() {
  if test -n "$SYSTEM_KIND"; then echo "$SYSTEM_KIND"
  elif test -f /etc/amalthea-root; then echo amalthea
  elif test -f /tmp/amalthea-recovery-reason; then echo recovery
  elif test -f /etc/openwrt_release || test -d /usr/trimui; then echo stock
  else echo unknown; fi
}

require_tg3040() {
  grep -q TG3040 $CPUINFO || fail "This is not a TRIMUI Brick (TG3040); nothing was changed"
  test "$(part_bytes mmcblk0p4)" = "$BOOT_BYTES" || fail "Unexpected boot partition size; nothing was changed"
  test "$(part_bytes mmcblk0p5)" = "$ROOT_BYTES" || fail "Unexpected root partition size; nothing was changed"
  grep -q " $UDISK " $MOUNTS || fail "Internal data storage (/mnt/UDISK) is not mounted"
  # the stock system names its partitions; they must be the ones this installer writes
  if test -d /dev/by-name; then
    for pair in boot:mmcblk0p4 rootfs:mmcblk0p5 UDISK:mmcblk0p10; do
      n=${pair%:*}; d=${pair#*:}
      test -e "/dev/by-name/$n" || continue
      l=$(readlink "/dev/by-name/$n"); test "${l##*/}" = "$d" || fail "Partition $n is not $d on this unit; nothing was changed"
    done
  fi
}

battery() { cat $BATT/capacity 2>/dev/null || echo 0; }

cmd_probe() {
  if grep -q TG3040 $CPUINFO; then echo "HW=TG3040"; else echo "HW=other"; fi
  echo "SYSTEM=$(system_kind)"
  echo "KERNEL=$(uname -r) $(uname -v | cut -d ' ' -f 1)"
  v=$(head -n 1 /etc/version 2>/dev/null)
  echo "STOCK_VERSION=${v:-unknown}"
  echo "BOOT_BYTES=$(part_bytes mmcblk0p4 2>/dev/null)"
  echo "ROOT_BYTES=$(part_bytes mmcblk0p5 2>/dev/null)"
  echo "BATTERY=$(battery)"
  echo "CHARGING=$(cat $BATT/status 2>/dev/null)"
  echo "FREE_KB=$(df -k "$UDISK" 2>/dev/null | tail -n 1 | tr -s ' ' | cut -d ' ' -f 4)"
  if test -f "$BK/SHA256SUMS"; then echo "BACKUP=present"; else echo "BACKUP=absent"; fi
  echo "MODE=$(cat "$BASE/boot.mode" 2>/dev/null)"
  echo "INSTALLED_ROOT=$(cut -d ' ' -f 1 "$BASE/installed-root.sha256" 2>/dev/null)"
  if test -f "$ST/boot.sha256" && test "$(hash_dev $BOOT $BOOT_BYTES)" = "$(cut -d ' ' -f 1 "$ST/boot.sha256")"; then
    echo "BOOT_CURRENT=yes"; else echo "BOOT_CURRENT=no"; fi
  test -f /tmp/amalthea-recovery-reason && echo "RECOVERY_REASON=$(cat /tmp/amalthea-recovery-reason)"
  ok
}

cmd_backup() {
  require_tg3040
  test "$(system_kind)" = stock || fail "Backups are taken from the stock system only"
  if test -f "$BK/SHA256SUMS" && (cd "$BK" && sha256sum -c -s SHA256SUMS); then
    say "A verified stock backup is already on the device"
    ok
  fi
  free=$(df -k "$UDISK" | tail -n 1 | tr -s ' ' | cut -d ' ' -f 4)
  test "$free" -gt 1200000 || fail "Not enough free internal storage for the backup (need 1.2 GB)"
  mkdir -p "$BK"
  say "Reading the stock boot partition"
  dd if=$BOOT of="$BK/stock-boot.img" bs=1048576 2>/dev/null || fail "Could not read the boot partition"
  say "Reading the stock root partition (500 MB)"
  dd if=$ROOT of="$BK/stock-rootfs.img" bs=1048576 2>/dev/null || fail "Could not read the root partition"
  sync
  b=$(hash_of "$BK/stock-boot.img"); r=$(hash_of "$BK/stock-rootfs.img")
  test "$b" = "$(hash_dev $BOOT $BOOT_BYTES)" || fail "Boot backup does not match the partition"
  test "$r" = "$(hash_dev $ROOT $ROOT_BYTES)" || fail "Root backup does not match the partition"
  { echo "$b  stock-boot.img"; echo "$r  stock-rootfs.img"; } > "$BK/SHA256SUMS"
  { uname -a; cat /etc/version 2>/dev/null; cat /etc/openwrt_release 2>/dev/null; } > "$BK/stock-version.txt"
  sync
  ok
}

cmd_stage() {
  require_tg3040
  case "$(system_kind)" in stock|amalthea) ;; *) fail "Stage from the stock system or from Amalthea" ;; esac
  (cd "$ST" && sha256sum -c -s SHA256SUMS) || fail "The payload on the device is damaged; run the installer again"
  free=$(df -k "$UDISK" | tail -n 1 | tr -s ' ' | cut -d ' ' -f 4)
  test "$free" -gt 600000 || fail "Not enough free internal storage (need 600 MB)"
  mkdir -p "$BASE/app"
  expected=$(cut -d ' ' -f 1 "$ST/rootfs.sha256")
  if ! (test -f "$BASE/rootfs.img" && test "$(hash_of "$BASE/rootfs.img")" = "$expected"); then
    say "Unpacking the Amalthea root image"
    gunzip -c "$ST/amalthea-rootfs.img.gz" > "$BASE/rootfs.img.next" || fail "Could not unpack the root image"
    test "$(stat -c %s "$BASE/rootfs.img.next")" = "$ROOT_BYTES" || fail "Unpacked root image has the wrong size"
    test "$(hash_of "$BASE/rootfs.img.next")" = "$expected" || fail "Unpacked root image checksum mismatch"
    mv "$BASE/rootfs.img.next" "$BASE/rootfs.img"
  fi
  echo "$expected  rootfs.img" > "$BASE/rootfs.sha256"
  cp "$ST/amalthea-boot.img" "$BASE/boot.img.next" && mv "$BASE/boot.img.next" "$BASE/boot.img"
  # The appliance itself: the root supervisor runs app/amalthea in preference to the
  # copy inside the read-only root, and falls back to that copy if it fails to start.
  cp "$ST/amalthea" "$BASE/app/amalthea.next" && chmod 755 "$BASE/app/amalthea.next" && mv "$BASE/app/amalthea.next" "$BASE/app/amalthea"
  cp "$ST/usb_f_uac2.ko" "$BASE/app/usb_f_uac2.ko.next" && mv "$BASE/app/usb_f_uac2.ko.next" "$BASE/app/usb_f_uac2.ko"
  test "$(hash_of "$BASE/app/amalthea")" = "$(hash_of "$ST/amalthea")" || fail "Appliance copy mismatch"
  sync
  ok
}

set_mode() { echo "$1" > "$BASE/boot.mode.next" && sync && mv "$BASE/boot.mode.next" "$BASE/boot.mode" && sync; }

cmd_arm() {
  require_tg3040
  kind=$(system_kind)
  case "$kind" in stock|amalthea) ;; *) fail "Arm from the stock system or from Amalthea" ;; esac
  test "$kind" = stock && ! test -f "$BK/SHA256SUMS" && fail "No stock backup on the device; run the installer again"
  test "$(battery)" -ge 40 || fail "Battery below 40%; charge the Brick and run the installer again"
  test -f "$BASE/rootfs.img" && test -f "$BASE/boot.img" || fail "Nothing staged; run the installer again"
  (cd "$BASE" && sha256sum -c -s rootfs.sha256) || fail "Staged root image damaged; run the installer again"
  want=$(hash_of "$BASE/boot.img")
  test "$want" = "$(cut -d ' ' -f 1 "$ST/boot.sha256")" || fail "Staged boot image damaged; run the installer again"
  previous=$(cat "$BASE/boot.mode" 2>/dev/null)
  set_mode install
  if test "$(hash_dev $BOOT $BOOT_BYTES)" != "$want"; then
    say "Writing the Amalthea boot partition"
    dd if="$BASE/boot.img" of=$BOOT bs=1048576 2>/dev/null; sync
    if test "$(hash_dev $BOOT $BOOT_BYTES)" != "$want"; then
      # put back what was there, so the next boot is the system that is running now
      if test "$kind" = stock; then
        dd if="$BK/stock-boot.img" of=$BOOT bs=1048576 2>/dev/null; sync
      fi
      if test -n "$previous"; then set_mode "$previous"; else rm -f "$BASE/boot.mode"; sync; fi
      fail "Boot partition write could not be verified; the previous boot partition was restored"
    fi
  else
    say "The Amalthea boot partition is already current"
  fi
  ok
}

cmd_retry_install() {
  grep -q TG3040 $CPUINFO || fail "This is not a TRIMUI Brick (TG3040)"
  test "$(system_kind)" = recovery || fail "Retry is for Amalthea's RAM recovery only"
  test -f "$BASE/rootfs.img" && (cd "$BASE" && sha256sum -c -s rootfs.sha256) || fail "No intact staged root image; restore the stock system with restore.sh"
  test "$(hash_dev $BOOT $BOOT_BYTES)" = "$(hash_of "$BASE/boot.img")" || fail "The boot partition is not Amalthea's; nothing was changed"
  test "$(battery)" -ge 40 || fail "Battery below 40%; charge the Brick first"
  set_mode install
  ok
}

cmd_restore_prepare() {
  require_tg3040
  test "$(system_kind)" = amalthea || fail "Restore starts from a running Amalthea system"
  (cd "$BK" && sha256sum -c -s SHA256SUMS) || fail "No verified stock backup on the device"
  test "$(battery)" -ge 40 || fail "Battery below 40%; charge the Brick first"
  set_mode rescue
  ok
}

cmd_restore_write() {
  grep -q TG3040 $CPUINFO || fail "This is not a TRIMUI Brick (TG3040)"
  test "$(system_kind)" = recovery || fail "The stock partitions are written from Amalthea's RAM recovery only"
  grep -q "^$ROOT " $MOUNTS && fail "The root partition is mounted; refusing to write it"
  (cd "$BK" && sha256sum -c -s SHA256SUMS) || fail "Stock backup damaged"
  b=$(grep stock-boot.img "$BK/SHA256SUMS" | cut -d ' ' -f 1)
  r=$(grep stock-rootfs.img "$BK/SHA256SUMS" | cut -d ' ' -f 1)
  # Root first: until the boot partition changes, a failure here still boots Amalthea's recovery.
  say "Writing the stock root partition"
  dd if="$BK/stock-rootfs.img" of=$ROOT bs=1048576 2>/dev/null; sync
  test "$(hash_dev $ROOT $ROOT_BYTES)" = "$r" || fail "Stock root write could not be verified; recovery stays selected, try again"
  say "Writing the stock boot partition"
  dd if="$BK/stock-boot.img" of=$BOOT bs=1048576 2>/dev/null; sync
  test "$(hash_dev $BOOT $BOOT_BYTES)" = "$b" || fail "Stock boot write could not be verified; do not power off, try again"
  ok
}

case "$1" in
  probe) cmd_probe ;;
  backup) cmd_backup ;;
  stage) cmd_stage ;;
  arm) cmd_arm ;;
  retry-install) cmd_retry_install ;;
  restore-prepare) cmd_restore_prepare ;;
  restore-write) cmd_restore_write ;;
  *) fail "unknown command: $1" ;;
esac
