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
  printf("disable-debug\n");
  for (j = 0; j < 50; j++) {
    for (i = 0; i < 200000; i++) {
      printf("create fileA\n");
      printf("truncate fileA 1048576\n");
      printf("rename fileA fileB\n");
      printf("delete fileB\n");
    }
    printf("umount\n");
    printf("mount\n");
  }
  printf("create done_file\n");
  printf("enable-debug\n");
}' | $CMD $DISK batch -
$CMD $DISK list | grep done_file | grep 10000001
$CMD $DISK compact
$CMD $DISK statfs | awk '{print $1}' | grep -E "^102(1|2)$"

exit 0
