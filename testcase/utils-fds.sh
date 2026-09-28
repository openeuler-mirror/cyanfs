#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-utils-fds.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

flags=(-std=gnu11 -Wall -Werror -O2)
if [ -n "${SANITIZERS:-}" ]; then
    flags+=(-O1 -g -fno-omit-frame-pointer -fsanitize="$SANITIZERS")
fi
for tool in EDIT MKFS BLKID; do
    platform=CYANFS_GLIBC
    if [ "$tool" = EDIT ]; then platform=CYANFS_PTHREAD; fi
    "$CC" "${flags[@]}" -pthread -D"$platform"=1 -D"TEST_$tool" -I"$ROOT_DIR" \
        "$ROOT_DIR/testcase/utils-fds.c" "$ROOT_DIR/utils/utils.c" "$ROOT_DIR/core/"*.c \
        -o "$TEST_DIR/check"
    if ! "$TEST_DIR/check" >"$TEST_DIR/output" 2>&1; then
        cat "$TEST_DIR/output"
        exit 1
    fi
    echo "utils-fds $tool: PASS"
done
