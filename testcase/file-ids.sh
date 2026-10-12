#!/bin/bash

set -euo pipefail

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
CMD="$ROOT_DIR/build/utils/cyanfs.edit"
TEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cyanfs-file-ids.XXXXXX")"
DISK="$TEST_DIR/cyanfs.disk"
trap 'if [ -f "$DISK" ]; then unlink "$DISK"; fi; rmdir "$TEST_DIR"' EXIT

truncate -s 64M "$DISK"
"$CMD" "$DISK" mkfs
"$CMD" "$DISK" create keeper
"$CMD" "$DISK" create gone
"$CMD" "$DISK" delete gone
"$CMD" "$DISK" compact
"$CMD" "$DISK" create after
"$CMD" "$DISK" list | awk '
    NR > 1 { count++ }
    $6 == "keeper" { if ($1 != 1) exit 1; keeper = 1 }
    $6 == "after" { if ($1 != 3) exit 1; after = 1 }
    END { if (!keeper || !after || count != 2) exit 1 }
'

"$CMD" "$DISK" delete keeper
"$CMD" "$DISK" delete after
"$CMD" "$DISK" compact
"$CMD" "$DISK" create final
"$CMD" "$DISK" list | awk '
    NR > 1 { count++ }
    $6 == "final" { if ($1 != 4) exit 1; final = 1 }
    END { if (!final || count != 1) exit 1 }
'

printf '%s\n' 'file-ids: PASS'
