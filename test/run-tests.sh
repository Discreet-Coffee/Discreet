#!/usr/bin/env bash
# Run all host-side tests. No hardware or ESP32 toolchain required.
set -euo pipefail
cd "$(dirname "$0")"

CC="${CC:-cc}"
FLAGS="-Wall -Wextra -Werror -O2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fail=0
for src in test_myscale_parse.c test_bbw_logic.c; do
  echo "=============================================================="
  echo "  $src"
  echo "=============================================================="
  # shellcheck disable=SC2086
  if ! $CC $FLAGS -o "$TMP/${src%.c}" "$src" -lm; then
    echo "COMPILE FAILED: $src"
    fail=1
    continue
  fi
  if ! "$TMP/${src%.c}"; then
    fail=1
  fi
  echo
done

if [ "$fail" -eq 0 ]; then
  echo "ALL SUITES PASSED"
else
  echo "SOME SUITES FAILED" >&2
fi
exit "$fail"
