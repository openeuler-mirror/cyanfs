#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
CMD="$ROOT_DIR/build/utils/cyanfs.edit"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-statfs.XXXXXX")"
DISK="$TEST_DIR/cyanfs.disk"
trap 'if [ -f "$DISK" ]; then unlink "$DISK"; fi; rmdir "$TEST_DIR"' EXIT

# Make fresh glibc allocations nonzero, including allocations normally cached.
export MALLOC_PERTURB_=165
export GLIBC_TUNABLES="${GLIBC_TUNABLES:+${GLIBC_TUNABLES}:}glibc.malloc.tcache_count=0"

function check_statfs() {
    local expected_data="$1" expected_files="$2"

    # Each invocation reopens the image, so no retired extents remain in flight.
    "$CMD" "$DISK" statfs | awk -v expected="$expected_data" '
        NR == 2 {
            seen = 1;
            if ($2 != expected || $1 + $2 + $3 != $5 ||
                $1 < 0 || $2 < 0 || $3 < 1) {
                print "unexpected statfs (expected Data=" expected "): " $0 > "/dev/stderr";
                exit 1;
            }
        }
        END { if (!seen) exit 1; }
    '
    "$CMD" "$DISK" list | awk -v data="$expected_data" -v files="$expected_files" '
        NR > 1 { count++; extents += $4; }
        END {
            if (count != files || extents != data) {
                print "file/extent totals disagree with statfs" > "/dev/stderr";
                exit 1;
            }
        }
    '
}

truncate -s 64M "$DISK"
"$CMD" "$DISK" mkfs
check_statfs 0 0

"$CMD" "$DISK" create foo
# Logical growth beyond the backend capacity must not consume data extents.
"$CMD" "$DISK" truncate foo 134217728
check_statfs 0 1
"$CMD" "$DISK" fill foo 0 2097152
check_statfs 2 1
"$CMD" "$DISK" discard foo 0 1048576
check_statfs 1 1
"$CMD" "$DISK" truncate foo 1048576
check_statfs 0 1

"$CMD" "$DISK" fill foo 0 1048576
"$CMD" "$DISK" fork foo snapshot
check_statfs 1 2
"$CMD" "$DISK" zero snapshot 0 4096
check_statfs 2 2
"$CMD" "$DISK" fill-check foo 0 1048576
"$CMD" "$DISK" zero-check snapshot 0 4096
"$CMD" "$DISK" fill-check snapshot 4096 1044480
"$CMD" "$DISK" delete snapshot
check_statfs 1 1

# Advance the journal beyond its first extent so compact replays a nonempty prefix.
awk 'BEGIN {
    print "disable-debug";
    for (i = 0; i < 10000; ++i) {
        print "create transient\ndelete transient";
        if (i % 200 == 199) print "umount\nmount";
    }
}' | "$CMD" "$DISK" batch -
check_statfs 1 1
for ((i = 0; i < 3; ++i)); do
    "$CMD" "$DISK" compact
    check_statfs 1 1
    "$CMD" "$DISK" fill-check foo 0 1048576
done

"$CMD" "$DISK" delete foo
check_statfs 0 0
"$CMD" "$DISK" compact
check_statfs 0 0

echo "statfs: PASS"
