#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-extent-flags.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

for platform in CYANFS_GLIBC CYANFS_PTHREAD; do
    "$CC" -std=gnu11 -Wall -Werror -pthread -O2 -D"$platform"=1 -I"$ROOT_DIR" \
        "$ROOT_DIR/testcase/extent-flags.c" -o "$TEST_DIR/check"
    "$TEST_DIR/check"
done

echo "extent-flags: PASS"
