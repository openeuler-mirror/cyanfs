/* Controlled syscall results for the real userspace transfer helpers. */
#include <assert.h>
#include <stdarg.h>
#include "../utils/utils.h"

static ssize_t test_write(int, const void *, size_t);
static ssize_t test_pwrite(int, const void *, size_t, off_t);
static int test_fstat(int, struct stat *);
static int test_ioctl(int, unsigned long, ...);
#define write test_write
#define pwrite test_pwrite
#define fstat test_fstat
#define ioctl test_ioctl
#include "../utils/utils.c"
#undef write
#undef pwrite
#undef fstat
#undef ioctl

struct step {
	ssize_t result;
	int error;
};
static const struct step *steps;
static int step_count, calls, positional;
static size_t progressed, requested;
static char buffer[8];
static mode_t device_mode;
static int stat_error, ioctl_error, ioctl_calls;

static ssize_t transfer(int fd, const void *buf, size_t count, off_t offset)
{
	struct step step;
	assert(fd == 17 && calls < step_count);
	assert(buf == buffer + progressed && count == requested - progressed);
	if (positional)
		assert(offset == 4096 + (off_t)progressed);
	step = steps[calls++];
	if (step.result < 0) {
		errno = step.error;
	} else {
		assert((size_t)step.result <= count);
		progressed += step.result;
	}
	return step.result;
}

static ssize_t test_write(int fd, const void *buf, size_t count)
{
	assert(!positional);
	return transfer(fd, buf, count, 0);
}

static ssize_t test_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	assert(positional);
	return transfer(fd, buf, count, offset);
}

static int test_fstat(int fd, struct stat *info)
{
	assert(fd == 23);
	if (stat_error) {
		errno = stat_error;
		return -1;
	}
	memset(info, 0, sizeof(*info));
	info->st_mode = device_mode;
	info->st_size = 12345;
	return 0;
}

static int test_ioctl(int fd, unsigned long request, ...)
{
	va_list args;
	uint64_t *size;
	assert(fd == 23 && request == BLKGETSIZE64 && S_ISBLK(device_mode));
	++ioctl_calls;
	va_start(args, request);
	size = va_arg(args, uint64_t *);
	va_end(args);
	if (ioctl_error) {
		errno = ioctl_error;
		return -1;
	}
	*size = 1073741824;
	return 0;
}

static void check_size(mode_t mode, int stat_err, int ioctl_err, int expected, uint64_t bytes, int queries)
{
	uint64_t size = 99;
	device_mode = mode;
	stat_error = stat_err;
	ioctl_error = ioctl_err;
	ioctl_calls = 0;
	assert(stat_device_size(23, &size) == expected);
	assert(size == bytes && ioctl_calls == queries);
}

static void check(const struct step *plan, int count, size_t length, int expected, size_t done)
{
	int result;
	steps = plan;
	step_count = count;
	calls = 0;
	progressed = 0;
	requested = length;
	result = positional ? safe_pwrite(17, buffer, 4096, length) : safe_write(17, buffer, length);
	assert(result == expected && calls == count && progressed == done);
}

int main(void)
{
	const struct step success[] = { { -1, EINTR }, { 3, 0 }, { 5, 0 } };
	const struct step zero[] = { { 0, 0 } };
	const struct step short_zero[] = { { 3, 0 }, { 0, 0 } };
	const struct step short_error[] = { { 3, 0 }, { -1, ENOSPC } };
	const struct step error[] = { { -1, EIO } };
	for (positional = 0; positional < 2; ++positional) {
		check(success, 3, 8, 0, 8);
		check(zero, 1, 8, -EIO, 0);
		check(short_zero, 2, 8, -EIO, 3);
		check(short_error, 2, 8, -ENOSPC, 3);
		check(error, 1, 8, -EIO, 0);
		check(NULL, 0, 0, 0, 0);
	}
	check_size(S_IFREG, 0, 0, 0, 12345, 0);
	check_size(S_IFBLK, 0, 0, 0, 1073741824, 1);
	check_size(S_IFBLK, 0, EIO, -EIO, 99, 1);
	check_size(S_IFBLK, 0, ENOTTY, -ENOTTY, 99, 1);
	check_size(S_IFBLK, 0, EINTR, -EINTR, 99, 1);
	check_size(S_IFREG, EBADF, 0, -EBADF, 99, 0);
	check_size(S_IFCHR, 0, 0, -EINVAL, 99, 0);
	puts("utils-io: PASS");
	return 0;
}
