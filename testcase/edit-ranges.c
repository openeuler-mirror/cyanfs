#include <assert.h>
#define main cyanfs_edit_main
#include "../utils/edit.c"
#undef main

int main(void)
{
	assert(valid_file_range(0, 0, 0));
	assert(valid_file_range(0, 4096, 4096));
	assert(valid_file_range(4095, 1, 4096));
	assert(valid_file_range(4096, 0, 4096));
	assert(!valid_file_range(4096, 1, 4096));
	assert(!valid_file_range(4097, 0, 4096));
	assert(!valid_file_range(4095, 2, 4096));
	assert(!valid_file_range(1, UINT64_MAX, 4096));
	assert(!valid_file_range(UINT64_MAX, 1, 4096));
	assert(!valid_file_range(UINT64_MAX, UINT64_MAX, 4096));
	assert(valid_file_range(CYANFS_FILE_MAX_SIZE, 0, CYANFS_FILE_MAX_SIZE));
	assert(valid_file_range(0, UINT64_MAX, UINT64_MAX));
	assert(valid_file_range(UINT64_MAX, 0, UINT64_MAX));
	assert(!valid_file_range(UINT64_MAX, 1, UINT64_MAX));
	puts("edit-ranges: PASS");
	return 0;
}
