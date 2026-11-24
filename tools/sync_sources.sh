#!/bin/bash
# Refresh app/ and system/ from the development repositories.
#   app/     <- ../amalthea        (the appliances: Tracker, FX, LIFEBLOOD)
#   system/  <- ../amalthea-linux  (Amalthea Linux: root filesystem, boot manager, image tools)
# Only the listed sources are copied: no build output, device images, vendor donor tree,
# session backups, development handoffs or device serial numbers.
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
APP=${APP:-$HERE/../amalthea}
SYS=${SYS:-$HERE/../amalthea-linux}
test -d "$APP/src" || { echo "app sources not found at $APP" >&2; exit 1; }
test -d "$SYS/initramfs" || { echo "system sources not found at $SYS" >&2; exit 1; }

copy() { # copy SRC_ROOT DEST_ROOT PATH...
  src=$1; dst=$2; shift 2
  for p in "$@"; do
    test -e "$src/$p" || { echo "missing: $src/$p" >&2; exit 1; }
    mkdir -p "$dst/$(dirname "$p")"
    rsync -a --delete --exclude '__pycache__' --exclude '.DS_Store' --exclude '*.amalthea' "$src/$p" "$dst/$(dirname "$p")/"
  done
}

rm -rf "$HERE/app" "$HERE/system"
copy "$APP" "$HERE/app" .clang-format .dockerignore CMakeLists.txt Dockerfile LICENSE BUILD_NUMBER README.md \
  src tests third_party assets tools \
  docs/APPLIANCE-SHELL.md docs/ENGINE-PORTS.md docs/EFFECTS-AND-REFERENCES.md docs/FX-APPLIANCE.md \
  docs/INSERT-PORTS.md docs/LIFEBLOOD.md docs/MACRO-EFFECTS.md docs/NATIVE-EFFECTS.md \
  docs/TAL-REVERB-AND-TAILS.md docs/USB-AUDIO.md
# development-only device tools (they address the developer's own unit)
rm -f "$HERE/app/tools/deploy_brick.py" "$HERE/app/tools/backup_sessions.py"

copy "$SYS" "$HERE/system" .dockerignore .gitignore Dockerfile README.md sources.lock.json \
  initramfs rootfs tools validation/HARDWARE.md validation/busybox.config

# Scrub local paths, and any private strings listed (tab-separated: string, placeholder) in the
# git-ignored .private-strings, such as the developer's device serial numbers.
PRIV=$HERE/.private-strings
find "$HERE/app" "$HERE/system" -type f \( -name '*.md' -o -name '*.json' -o -name '*.py' -o -name '*.sh' -o -name '*.txt' \) | while read -r f; do
  sed -i '' -e 's#/Users/[A-Za-z0-9_.-]*/development/#../#g' "$f"
  if [ -f "$PRIV" ]; then
    while IFS='	' read -r secret placeholder; do
      [ -n "$secret" ] && sed -i '' -e "s#$secret#<$placeholder>#g" "$f"
    done < "$PRIV"
  fi
done
if grep -rIl -e '/Users/' "$HERE/app" "$HERE/system" || { [ -f "$PRIV" ] && grep -rIlF -f <(cut -f 1 "$PRIV") "$HERE/app" "$HERE/system"; }; then
  echo "private strings remain in the files above" >&2; exit 1
fi
cp "$APP/CHANGELOG.md" "$HERE/CHANGELOG.md"
echo "synced: app/ (build $(cat "$HERE/app/BUILD_NUMBER")), system/"
