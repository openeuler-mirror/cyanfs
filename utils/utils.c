#include "utils.h"

int safe_pread(int fd, void *buf, off_t offset, size_t count)
{
	size_t c = count;
	while (c) {
		ssize_t n;
		n = pread(fd, buf, c, offset);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		} else if (n == 0) {
			return -EIO;
		}
		buf = ((char *)buf) + n;
		offset += n;
		c -= n;
	}
	return 0;
}

int safe_read(int fd, void *buf, size_t count)
{
	size_t c = count;
	while (c) {
		ssize_t n;
		n = read(fd, buf, c);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		} else if (n == 0) {
			return -EIO;
		}
		buf = ((char *)buf) + n;
		c -= n;
	}
	return 0;
}

int safe_pwrite(int fd, void *buf, off_t offset, size_t count)
{
	size_t c = count;
	while (c) {
		ssize_t n;
		n = pwrite(fd, buf, c, offset);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		buf = ((char *)buf) + n;
		offset += n;
		c -= n;
	}
	return 0;
}

int safe_write(int fd, void *buf, size_t count)
{
	size_t c = count;
	while (c) {
		ssize_t n;
		n = write(fd, buf, c);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		buf = ((char *)buf) + n;
		c -= n;
	}
	return 0;
}

int stat_device_size(int fd, uint64_t *size)
{
	struct stat stat;
	int r;

	r = fstat(fd, &stat);
	if (r < 0)
		return -errno;

	if (S_ISREG(stat.st_mode)) {
		*size = stat.st_size;
		return 0;
	} else if (S_ISBLK(stat.st_mode)) {
		return ioctl(fd, BLKGETSIZE64, size);
	} else {
		return -EINVAL;
	}
}
