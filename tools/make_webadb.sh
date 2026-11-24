#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build docs/js/vendor/webadb.js, the one third-party script of the browser installer (docs/web-install.html):
# Tango / ya-webadb (ADB over WebUSB), fflate (unzip) and @noble/hashes (streaming SHA-256), at the exact
# versions pinned in tools/webadb/package.json + package-lock.json. All MIT; their notices go next to it.
set -eu
W=$(cd "$(dirname "$0")/webadb" && pwd)
OUT=$W/../../docs/js/vendor
cd "$W"
npm ci --no-audit --no-fund >/dev/null
mkdir -p "$OUT"
npx esbuild entry.js --bundle --format=esm --target=chrome120 --minify --legal-comments=none \
  --outfile="$OUT/webadb.js"
{
  echo "docs/js/vendor/webadb.js bundles, unmodified (tools/make_webadb.sh):"
  for p in @yume-chan/adb @yume-chan/adb-daemon-webusb @yume-chan/adb-credential-web @yume-chan/stream-extra \
           @yume-chan/struct @yume-chan/event @yume-chan/async @yume-chan/no-data-view fflate @noble/hashes; do
    v=$(node -p "require('./node_modules/$p/package.json').version")
    echo; echo "== $p $v"; cat "node_modules/$p/LICENSE"* 2>/dev/null || echo "(MIT; see https://www.npmjs.com/package/$p)"
  done
} > "$OUT/webadb.LICENSES.txt"
echo "webadb: docs/js/vendor/webadb.js ($(wc -c < "$OUT/webadb.js") bytes)"
