#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-utils-io.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

flags=(-std=gnu11 -Wall -Werror -O2 -DCYANFS_GLIBC=1)
if [ -n "${SANITIZERS:-}" ]; then
    flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize="$SANITIZERS")
fi
"$CC" "${flags[@]}" -I"$ROOT_DIR" "$ROOT_DIR/testcase/utils-io.c" -o "$TEST_DIR/check"
"$TEST_DIR/check"
