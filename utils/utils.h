#ifndef __CYANFS_UTILS_HEADER__
#define __CYANFS_UTILS_HEADER__

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <inttypes.h>

#include "../core/core.h"

extern int safe_pread(int fd, void *buf, off_t offset, size_t count);
extern int safe_pwrite(int fd, void *buf, off_t offset, size_t count);
extern int safe_read(int fd, void *buf, size_t count);
extern int safe_write(int fd, void *buf, size_t count);
extern int stat_device_size(int fd, uint64_t *size);

#endif
