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
  printf("fill foo 0 104857600\n");
  printf("fork foo bar\n");
  printf("fill-check foo 0 104857600\n");
  printf("fill-check bar 0 104857600\n");
  printf("zero bar 0 1024\n");
  printf("zero bar 1048064 1024\n");
  printf("zero bar 4194304 1048576\n");
  printf("zero bar 8388096 1049600\n");
  printf("zero-check bar 0 1024\n");
  printf("zero-check bar 1048064 1024\n");
  printf("zero-check bar 4194304 1048576\n");
  printf("zero-check bar 8388096 1049600\n");
  printf("fill-check bar 1024 1047040\n");
  printf("fill-check bar 1049088 3145216\n");
  printf("fill-check bar 5242880 3145216\n");
  printf("fill-check bar 9437696 95419904\n");
  printf("fill-check foo 0 104857600\n");
  printf("fill bar 0 1024\n");
  printf("fill bar 1048064 1024\n");
  printf("fill bar 4194304 1048576\n");
  printf("fill bar 8388096 1049600\n");
  printf("fill-check bar 0 104857600\n");
}' | $CMD $DISK batch -

$CMD $DISK extents foo | wc -l | grep -E "^100$"

$CMD $DISK extents bar | grep "extent: 0"
$CMD $DISK extents bar | grep "extent: 1048576"
$CMD $DISK extents bar | grep "extent: 4194304"
$CMD $DISK extents bar | grep "extent: 7340032"
$CMD $DISK extents bar | grep "extent: 8388608"
$CMD $DISK extents bar | grep "extent: 9437184"
$CMD $DISK extents bar | wc -l | grep -E "^6$"

$CMD $DISK truncate foo 1048576 && exit 1
$CMD $DISK truncate bar 1048576
$CMD $DISK fill-check bar 0 1048576

echo -n | awk 'BEGIN{
  printf("zero bar 0 1024\n");
  printf("fork bar zoo\n");
  printf("zero-check zoo 0 1024\n");
  printf("fill-check zoo 1024 1047552\n");
}' | $CMD $DISK batch -

$CMD $DISK list | grep zoo | grep " 1048576 "

echo -n | awk 'BEGIN{
  printf("truncate zoo 104857600\n");
  printf("zero-check zoo 0 1024\n");
  printf("fill-check zoo 1024 1047552\n");
  printf("zero-check zoo 1048576 103809024\n");
  printf("fill zoo 8388608 1024\n");
  printf("fill-check zoo 8388608 1024\n");
  printf("zero-check zoo 8389632 1047522\n");
}' | $CMD $DISK batch -

exit 0
