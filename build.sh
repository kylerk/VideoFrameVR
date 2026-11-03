#!/usr/bin/env bash
# Builds videoframevr into ./build.
# The full compiler output is kept in build/last_build.log.
set -uo pipefail
cd "$(dirname "$0")"

if [ ! -f build/CMakeCache.txt ]; then
  cmake -S . -B build || exit 1
fi

cmake --build build -j > build/last_build.log 2>&1
status=$?
grep -E "error|warning" build/last_build.log

if [ $status -ne 0 ]; then
  echo "BUILD FAILED (full log: build/last_build.log)"
  exit 1
fi
echo "BUILD OK  –  start with: ./build/videoframevr [video]"
