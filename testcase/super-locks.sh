#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-super-locks.XXXXXX")"
CC=${CC:-cc}
trap 'find "$TEST_DIR" -maxdepth 1 -type f -delete; rmdir "$TEST_DIR"' EXIT

flags=(-std=gnu11 -Wall -Werror -O1 -g -pthread -DCYANFS_PTHREAD=1)
if [ -n "${SANITIZERS:-}" ]; then
    flags+=(-fno-omit-frame-pointer -fsanitize="$SANITIZERS")
fi
"$CC" "${flags[@]}" -I"$ROOT_DIR" "$ROOT_DIR/testcase/super-locks.c" \
    "$ROOT_DIR/core/"*.c -Wl,--wrap=malloc,--wrap=free \
    -Wl,--wrap=pthread_rwlock_init,--wrap=pthread_mutex_init \
    -Wl,--wrap=pthread_rwlock_destroy,--wrap=pthread_mutex_destroy -o "$TEST_DIR/check"
"$TEST_DIR/check"
