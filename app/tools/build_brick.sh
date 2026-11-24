#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
# Every Brick build gets the next build number. A failed build gives it back.
previous=$(cat BUILD_NUMBER)
case "$previous" in ''|*[!0-9]*) echo "BUILD_NUMBER must contain a single build number" >&2; exit 1;; esac
next=$((previous + 1))
echo "$next" > BUILD_NUMBER
trap 'echo "$previous" > BUILD_NUMBER; echo "Build $next failed; BUILD_NUMBER restored to $previous" >&2' EXIT
docker build -t amalthea-build:ubuntu20 .
docker run --rm -v "$PWD:/work" amalthea-build:ubuntu20 sh -c 'cmake -S . -B build/brick -DAMALTHEA_BRICK=ON -DCMAKE_BUILD_TYPE=Release && cmake --build build/brick -j4 && cd build/brick && ctest --output-on-failure'
trap - EXIT
echo "Built Amalthea build $next"
