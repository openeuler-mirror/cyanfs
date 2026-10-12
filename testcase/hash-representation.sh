#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-hash-representation.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

flags=(-std=gnu11 -Wall -Werror -Wstrict-aliasing=2 -fstrict-aliasing -O2 -g)
if [ -n "${SANITIZERS:-}" ]; then
    flags+=(-fno-omit-frame-pointer -fsanitize="$SANITIZERS")
fi
for platform in CYANFS_GLIBC CYANFS_PTHREAD; do
    "$CC" "${flags[@]}" -pthread -D"$platform"=1 -I"$ROOT_DIR" \
        "$ROOT_DIR/testcase/hash-representation.c" -o "$TEST_DIR/check"
    "$TEST_DIR/check"
done

echo "hash-representation: PASS"
