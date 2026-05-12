#!/bin/bash

set -ex

ROOT_DIR="$(readlink -f "$(dirname "$0")/../")"
pushd ${ROOT_DIR}

DISK="/dev/shm/cyanfs.disk"
CMD="build/utils/cyanfs.edit"

rm -f $DISK
dd if=/dev/urandom of=$DISK bs=1M count=32
echo -n | awk 'BEGIN{
  printf("mkfs\n");
  printf("create foo\n");
  printf("truncate foo 104857600\n");
}' | $CMD $DISK batch -

$CMD $DISK fill foo 0 104857600 && exit 1
if [ "$($CMD $DISK statfs | awk '{print $1}')" == "0" ]; then
  exit 1
fi

echo -n | awk 'BEGIN{
  printf("disable-debug\n");
  for (j = 0; j < 1000; j++) {
    for (i = 0; i < 1000; i++) {
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

exit 0
