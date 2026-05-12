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
  printf("truncate foo 104857600\n");
  printf("fill foo 0 10485760\n");
  printf("discard foo 0 4194304\n");
  printf("zero-check foo 0 4194304\n");
  printf("fill-check foo 4194304 6291456\n");
  printf("fork foo bar\n");
  printf("zero bar 0 20971520\n");
  printf("zero-check bar 0 20971520\n");
  printf("discard bar 0 20971520\n");
  printf("zero-check bar 0 20971520\n");
}' | $CMD $DISK batch -

$CMD $DISK extents foo | wc -l | grep -E "^6$"
$CMD $DISK extents bar | wc -l | grep -E "^6$"

exit 0
