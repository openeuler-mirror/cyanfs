#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-rb-init.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

flags=(-std=gnu11 -Wall -Werror "${OPT_LEVEL:--O1}" -g -pthread -DCYANFS_PTHREAD=1)
if [ -n "${SANITIZERS:-}" ]; then
    flags+=(-fno-omit-frame-pointer -fsanitize="$SANITIZERS")
else
    flags+=(-DCYANFS_TEST_POISON_LINKS=1)
fi
"$CC" "${flags[@]}" -I"$ROOT_DIR" "$ROOT_DIR/testcase/rb-init.c" -o "$TEST_DIR/check"
"$TEST_DIR/check"

echo "rb-init: PASS"
