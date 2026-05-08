#!/bin/bash

set -ex

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
pushd ${ROOT_DIR}

DISK="/dev/shm/cyanfs.disk"
CMD="build/utils/cyanfs.edit"

rm -f $DISK
touch $DISK
truncate $DISK -s 1G
$CMD $DISK mkfs
$CMD $DISK create foo
$CMD $DISK truncate foo 104857600
$CMD $DISK fill foo 0 1024
$CMD $DISK fill-check foo 0 1024
$CMD $DISK zero-check foo 0 1024 && exit 1
$CMD $DISK zero foo 0 1024
$CMD $DISK zero-check foo 0 1024
$CMD $DISK fill-check foo 0 1024 && exit 1
$CMD $DISK zero-check foo 1024 104857600 && exit 1

exit 0
