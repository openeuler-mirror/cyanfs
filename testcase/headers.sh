#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-headers.XXXXXX")"
CC=${CC:-cc}
CXX=${CXX:-c++}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

# A copied UAPI header must work without the core source tree on the include path.
cp "$ROOT_DIR/linux/include/cyanfs.h" "$TEST_DIR/cyanfs.h"
"$CC" -std=gnu11 -Wall -Werror -I"$TEST_DIR" "$ROOT_DIR/testcase/headers.c" -o "$TEST_DIR/uapi"
"$TEST_DIR/uapi" > "$TEST_DIR/reference"
sed '/^ioctl:/d' "$TEST_DIR/reference" > "$TEST_DIR/common"

for platform in CYANFS_GLIBC CYANFS_PTHREAD; do
    for order in TEST_CORE_ONLY TEST_CORE_FIRST TEST_UAPI_FIRST; do
        "$CC" -std=gnu11 -Wall -Werror -pthread -D"$platform"=1 -D"$order" \
            -I"$ROOT_DIR" -I"$TEST_DIR" "$ROOT_DIR/testcase/headers.c" -o "$TEST_DIR/mixed"
        "$TEST_DIR/mixed" > "$TEST_DIR/layout"
        if [ "$order" = TEST_CORE_ONLY ]; then
            cmp "$TEST_DIR/common" "$TEST_DIR/layout"
        else
            cmp "$TEST_DIR/reference" "$TEST_DIR/layout"
        fi
    done
done

# The standalone UAPI header also supports C++ consumers.
"$CXX" -x c++ -std=gnu++11 -Wall -Werror -I"$TEST_DIR" \
    "$ROOT_DIR/testcase/headers.c" -o "$TEST_DIR/uapi-cxx"
"$TEST_DIR/uapi-cxx" > "$TEST_DIR/layout"
cmp "$TEST_DIR/reference" "$TEST_DIR/layout"

echo "headers: PASS"
