#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake -S . -B build/mac -DCMAKE_BUILD_TYPE=Release
cmake --build build/mac -j4
ctest --test-dir build/mac --output-on-failure
