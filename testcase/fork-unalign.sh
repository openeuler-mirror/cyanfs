#!/bin/bash

set -ex

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
pushd ${ROOT_DIR}

DISK="/dev/shm/cyanfs.disk"
CMD="build/utils/cyanfs.edit"

rm -f $DISK
touch $DISK
truncate $DISK -s 1G
echo -n | awk 'BEGIN{
  printf("mkfs\n");
  printf("create foo\n");
  printf("truncate foo 1114112\n");
  printf("fill foo 0 1114112\n");
  printf("fill-check foo 0 1114112\n");
  printf("fork foo bar\n");
  printf("fill-check bar 0 1114112\n");
  printf("truncate bar 1572864\n");
  printf("fill bar 1114112 65536\n");
  printf("fill-check bar 0 1179648\n");
  printf("zero-check bar 1179648 393216\n");
}' | $CMD $DISK batch -

exit 0
