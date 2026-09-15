/* Count ownership without opening any real backend or random source. */
#include <assert.h>
#include "../utils/utils.h"

static int test_open(const char *, int, ...);
static int test_close(int);
#if defined(TEST_BLKID)
static int test_pread(int, void *, off_t, size_t);
#define safe_pread test_pread
#else
static int test_read(int, void *, size_t);
static int test_pwrite(int, void *, off_t, size_t);
#define safe_read test_read
#define safe_pwrite test_pwrite
#endif
#define open test_open
#define close test_close
#define main cyanfs_tool_main
#if defined(TEST_EDIT)
#include "../utils/edit.c"
#elif defined(TEST_MKFS)
#include "../utils/mkfs.c"
#elif defined(TEST_BLKID)
#include "../utils/blkid.c"
#else
#error Select a tool
#endif
#undef main
#undef open
#undef close
#undef safe_read
#undef safe_pread
#undef safe_pwrite

static int scenario, opens, closes, reads, writes;
static int active[2];

static int test_open(const char *path, int flags, ...)
{
	assert(opens < 2 && !active[opens]);
	active[opens] = 1;
	return 101 + opens++;
}

static int test_close(int fd)
{
	int index = fd - 101;
	assert(index >= 0 && index < 2 && active[index]);
	active[index] = 0;
	++closes;
	return 0;
}

#if defined(TEST_BLKID)
static int test_pread(int fd, void *buffer, off_t offset, size_t count)
{
	cyanfs_uuid_t uuid = { { 1, 2, 3, 4 } };
	assert(fd == 101 && active[0] && offset == 0 && count == CYANFS_SUPER_BLOCK_SIZE);
	++reads;
	if (scenario == 1)
		return -EIO;
	cyanfs_super_make(uuid, buffer);
	return 0;
}
#else
static int test_read(int fd, void *buffer, size_t count)
{
	assert(fd == 101 && active[0] && count == sizeof(cyanfs_uuid_t));
	++reads;
	if (scenario == 1)
		return -EIO;
	memset(buffer, 0x42, count);
	return 0;
}

static int test_pwrite(int fd, void *buffer, off_t offset, size_t count)
{
	assert(!active[0] && offset == 0 && count == CYANFS_SUPER_BLOCK_SIZE);
#if defined(TEST_EDIT)
	assert(fd == 77); /* Borrowed from the caller; must never be closed here. */
#else
	assert(fd == 102 && active[1]);
#endif
	++writes;
	return scenario == 2 ? -EIO : 0;
}
#endif

int main(void)
{
	int iteration;
	for (iteration = 0; iteration < 20; ++iteration) {
		for (scenario = 0; scenario < 3; ++scenario) {
			int result, expected_opens;
#if defined(TEST_BLKID)
			if (scenario == 2)
				continue;
#endif
			opens = closes = reads = writes = 0;
			assert(!active[0] && !active[1]);
#if defined(TEST_EDIT)
			{
				struct disk disk = { .fd = 77 };
				result = do_mkfs(&disk, 0, NULL);
				assert(result == (scenario ? -EIO : 0));
			}
			expected_opens = 1;
#else
			{
				const char *args[] = { "tool", "disk" };
				result = cyanfs_tool_main(2, args);
				assert(result == (scenario ? -1 : 0));
			}
#if defined(TEST_BLKID)
			expected_opens = 1;
#else
			expected_opens = scenario == 1 ? 1 : 2;
#endif
#endif
			assert(opens == expected_opens && closes == opens);
			assert(!active[0] && !active[1] && reads == 1);
#if defined(TEST_BLKID)
			assert(writes == 0);
#else
			assert(writes == (scenario == 1 ? 0 : 1));
#endif
		}
	}
	puts("utils-fds: PASS");
	return 0;
}
