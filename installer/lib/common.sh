# SPDX-License-Identifier: GPL-3.0-only
# Shared by install.sh and restore.sh (sourced, POSIX sh).

DEV_STAGE=/mnt/UDISK/amalthea-install
DEV_BACKUP=/mnt/UDISK/amalthea-stock-backup
DEV_SESSIONS=/mnt/UDISK/tracker-linux
TESTED_KERNEL='#804'          # stock firmware 1.0.6 (December 2024)
ADB=${ADB:-}
SERIAL=

say() { printf '%s\n' "$*"; }
step() { printf '\n==> %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }
banner() { printf '\n%s\n%s\n\n' "$1" "$(printf '%s' "$1" | sed 's/./=/g')"; }

confirm() {
  [ "$YES" = 1 ] && return 0
  printf '%s Type yes to continue: ' "$1"
  read -r answer || answer=
  [ "$answer" = yes ] || die "Cancelled. Nothing was changed."
}

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d ' ' -f 1
  else shasum -a 256 "$1" | cut -d ' ' -f 1; fi
}

check_sums() { # check_sums SUMSFILE (in the current directory)
  while read -r sum name; do
    [ -n "$sum" ] || continue
    [ "$(sha256_of "$name")" = "$sum" ] || { say "checksum mismatch: $name"; return 1; }
  done < "$1"
}

find_adb() {
  if [ -n "$ADB" ]; then :
  elif command -v adb >/dev/null 2>&1; then ADB=$(command -v adb)
  elif [ -x "$HERE/platform-tools/adb" ]; then ADB="$HERE/platform-tools/adb"
  else
    case "$(uname -s)" in
      Darwin) os=darwin ;;
      Linux) os=linux ;;
      *) die "Install Android platform-tools (adb) and run this again." ;;
    esac
    say "ADB (Android platform-tools) was not found. It can be downloaded from Google"
    say "into this folder: https://dl.google.com/android/repository/platform-tools-latest-$os.zip"
    confirm "Download platform-tools now?"
    command -v curl >/dev/null 2>&1 && command -v unzip >/dev/null 2>&1 || die "curl and unzip are needed to download platform-tools."
    curl -fL# -o "$HERE/platform-tools.zip" "https://dl.google.com/android/repository/platform-tools-latest-$os.zip" || die "Download failed."
    (cd "$HERE" && unzip -oq platform-tools.zip && rm -f platform-tools.zip) || die "Could not unpack platform-tools."
    ADB="$HERE/platform-tools/adb"
  fi
  "$ADB" start-server >/dev/null 2>&1 || true
}

adb_() { "$ADB" -s "$SERIAL" "$@"; }

wait_for_brick() { # [timeout seconds]
  left=${1:-120}; hinted=0
  while :; do
    list=$("$ADB" devices 2>/dev/null | tr -d '\r' | sed '1d' | grep -v '^$' || true)
    ready=$(printf '%s\n' "$list" | grep -c '	device$' || true)
    if [ "$ready" = 1 ]; then SERIAL=$(printf '%s\n' "$list" | grep '	device$' | cut -f 1); return 0; fi
    [ "$ready" -gt 1 ] && die "More than one Android device is connected. Leave only the Brick connected."
    if printf '%s\n' "$list" | grep -q 'no permissions'; then
      die "ADB has no permission to use the Brick. On Linux, add a udev rule for USB vendor 18d1 (or install your distribution's android-udev-rules / android-sdk-platform-tools-common) and reconnect."
    fi
    if [ "$hinted" = 0 ]; then
      say "Waiting for the Brick: switch it on and connect it with a USB-C data cable."
      say "(On Amalthea, stay on the home screen: the FX and LIFEBLOOD appliances use the port for the FM-1.)"
      hinted=1
    fi
    left=$((left - 2)); [ "$left" -gt 0 ] || die "No Brick found over ADB."
    sleep 2
  done
}

push() { "$ADB" -s "$SERIAL" push "$1" "$2" >/dev/null || die "Could not copy $(basename "$1") to the Brick."; }

push_installer() {
  step "Copying the installer to the Brick"
  adb_ shell "mkdir -p $DEV_STAGE" >/dev/null
  for f in "$HERE"/payload/*; do push "$f" "$DEV_STAGE/$(basename "$f")"; done
  adb_ shell "chmod 755 $DEV_STAGE/busybox" >/dev/null
}

# device COMMAND: run the device-side script, print its LOG lines, fail on RESULT=FAIL
device() {
  OUT=$(adb_ shell "$DEV_STAGE/busybox sh $DEV_STAGE/amalthea-device.sh $1" 2>&1 | tr -d '\r') || true
  printf '%s\n' "$OUT" | sed -n 's/^LOG /    /p'
  result=$(printf '%s\n' "$OUT" | grep '^RESULT=' | tail -n 1)
  case "$result" in
    RESULT=OK) return 0 ;;
    RESULT=FAIL*) die "${result#RESULT=FAIL }" ;;
    *) die "The Brick did not answer as expected:
$OUT" ;;
  esac
}

probe() {
  device probe
  val() { printf '%s\n' "$OUT" | sed -n "s/^$1=//p" | head -n 1; }
  P_HW=$(val HW); P_SYSTEM=$(val SYSTEM); P_KERNEL=$(val KERNEL); P_STOCK=$(val STOCK_VERSION)
  P_BATTERY=$(val BATTERY); P_BACKUP=$(val BACKUP); P_MODE=$(val MODE); P_BOOT_CURRENT=$(val BOOT_CURRENT)
  P_INSTALLED_ROOT=$(val INSTALLED_ROOT); P_REASON=$(val RECOVERY_REASON)
  say "    Brick $SERIAL: $P_SYSTEM system, kernel $P_KERNEL, battery ${P_BATTERY}%"
}

need_battery() {
  [ "${P_BATTERY:-0}" -ge 50 ] || die "The battery is at ${P_BATTERY}%. Charge the Brick to at least 50% and run this again."
}

pull_stock_backup() {
  dest="$HERE/backup-$SERIAL"
  if [ -f "$dest/SHA256SUMS" ] && (cd "$dest" && check_sums SHA256SUMS >/dev/null 2>&1); then
    say "    A verified copy is already on this computer: $dest"
    return 0
  fi
  mkdir -p "$dest"
  for f in SHA256SUMS stock-version.txt stock-boot.img stock-rootfs.img; do
    "$ADB" -s "$SERIAL" pull "$DEV_BACKUP/$f" "$dest/$f" >/dev/null || die "Could not copy $f from the Brick."
  done
  (cd "$dest" && check_sums SHA256SUMS) || die "The copied backup does not match the Brick's. Run the installer again."
  say "    Saved to $dest (keep this folder: ./restore.sh uses it)"
}

backup_sessions() {
  dest="$HERE/sessions-$SERIAL-$(date +%Y%m%d-%H%M%S)"
  mkdir -p "$dest"
  for f in session-slots fx-session-slots lifeblood-session-slots session.amalthea device-settings.amalthea; do
    "$ADB" -s "$SERIAL" pull "$DEV_SESSIONS/$f" "$dest/" >/dev/null 2>&1 || true
  done
  say "    Saved sessions copied to $dest"
}

wait_for_amalthea() {
  step "Waiting for the Brick to install and start Amalthea (one to three minutes; do not disconnect it)"
  sleep 15
  wait_for_brick 400
  probe
  if [ "$P_SYSTEM" = amalthea ] && [ "$P_MODE" = system ]; then
    say ""
    say "Done. Amalthea is installed and running."
    say "Read the manual: https://nzimas.github.io/amalthea/manual.html"
    return 0
  fi
  if [ "$P_SYSTEM" = recovery ]; then
    die "The Brick stopped in Amalthea's RAM recovery: ${P_REASON:-no reason recorded}.
Nothing is lost. Run ./install.sh again to retry, or ./restore.sh to return to the stock system."
  fi
  die "The Brick restarted into an unexpected state ($P_SYSTEM, mode ${P_MODE:-none})."
}

install_from_stock() {
  case "$P_KERNEL" in
    *"$TESTED_KERNEL"*) ;;
    *) say ""
       say "WARNING: this Brick runs stock firmware ${P_STOCK:-unknown} (kernel $P_KERNEL)."
       say "Amalthea was developed on stock 1.0.6 (kernel $TESTED_KERNEL). It brings its own kernel,"
       say "but on this firmware it is untested."
       confirm "Continue anyway?" ;;
  esac
  need_battery
  step "Backing up the stock system on the Brick"
  device backup
  step "Copying the stock backup to this computer"
  pull_stock_backup
  step "Unpacking Amalthea on the Brick"
  device stage
  step "Writing the Amalthea boot partition"
  device arm
  step "Restarting the Brick"
  adb_ shell reboot >/dev/null 2>&1 || true
  wait_for_amalthea
}

update_amalthea() {
  say ""
  say "This Brick already runs Amalthea: it will be updated to build $(cat "$HERE/payload/BUILD")."
  say "The instruments restart blank and stopped, as on every start: save your session first."
  confirm "Update now?"
  step "Copying your saved sessions to this computer"
  backup_sessions
  step "Unpacking the update on the Brick"
  device stage
  probe
  if [ "$P_INSTALLED_ROOT" = "$(cut -d ' ' -f 1 "$HERE/payload/rootfs.sha256")" ] && [ "$P_BOOT_CURRENT" = yes ]; then
    step "Restarting the instruments"
    adb_ shell 'kill -TERM $(pidof amalthea)' >/dev/null 2>&1 || true
    say ""
    say "Done. Amalthea build $(cat "$HERE/payload/BUILD") is running."
    return 0
  fi
  need_battery
  step "Writing the Amalthea boot partition"
  device arm
  step "Restarting the Brick"
  adb_ shell reboot >/dev/null 2>&1 || true
  wait_for_amalthea
}

retry_from_recovery() {
  say ""
  say "The Brick is in Amalthea's RAM recovery: ${P_REASON:-no reason recorded}."
  confirm "Retry the installation?"
  device retry-install
  step "Restarting the Brick"
  adb_ shell 'reboot -f' >/dev/null 2>&1 || true
  wait_for_amalthea
}
