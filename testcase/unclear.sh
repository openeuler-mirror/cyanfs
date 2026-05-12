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
  printf("truncate foo 104857600\n");
  printf("fill foo 0 1024\n");
  printf("fill foo 1049600 1024\n");
  printf("fill foo 3144704 1024\n");
  printf("fill-check foo 0 1024\n");
  printf("fill-check foo 1049600 1024\n");
  printf("fill-check foo 3144704 1024\n");
  printf("zero-check foo 3145728 101711872\n");
}' | $CMD $DISK batch -

$CMD $DISK extents foo | wc -l | grep -E "^3$"
$CMD $DISK fill-check foo 1024 1024 && exit 1
$CMD $DISK zero-check foo 1024 1024 && exit 1
$CMD $DISK fill-check foo 1048576 1024 && exit 1
$CMD $DISK zero-check foo 1048576 1024 && exit 1
$CMD $DISK fill-check foo 2097152 1024 && exit 1
$CMD $DISK zero-check foo 2097152 1024 && exit 1

function compare() {
  TMP=/tmp/$$
  $CMD $DISK read $1 $3 $4 > $TMP.foo
  $CMD $DISK read $2 $3 $4 > $TMP.bar
  FOO="$(cat $TMP.foo | sha256sum)"
  BAR="$(cat $TMP.bar | sha256sum)"
  rm -f $TMP.foo $TMP.bar
  if [ "${FOO}" != "${BAR}" ]; then
    return 1
  fi
  return 0
}

$CMD $DISK fork foo bar
compare foo bar 0 1048576

$CMD $DISK zero bar 0 1024
compare foo bar 1024 1047552

exit 0
