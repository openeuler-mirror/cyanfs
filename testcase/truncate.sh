#!/bin/bash

set -ex

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
pushd ${ROOT_DIR}

DISK="/dev/shm/cyanfs.disk"
CMD="build/utils/cyanfs.edit"

rm -f $DISK
dd if=/dev/urandom of=$DISK bs=1M count=1024
echo -n | awk 'BEGIN{
  printf("mkfs\n");
  printf("create foo\n");
  printf("truncate foo 10485760\n");
  printf("fill foo 0 10485760\n");
  printf("fill-check foo 0 10485760\n");
  printf("truncate foo 104857600\n");
  printf("fill-check foo 0 10485760\n");
  printf("zero-check foo 10485760 94371840\n");
  printf("truncate foo 1048576\n");
  printf("fill-check foo 0 1048576\n");
  printf("truncate foo 10485760\n");
  printf("fill-check foo 0 1048576\n");
  printf("zero-check foo 1048576 9437184\n");
}' | $CMD $DISK batch -

exit 0
