/* Small-capacity contracts for the real batch tokenizer. */
#include <assert.h>
#define main cyanfs_edit_main
#include "../utils/edit.c"
#undef main

int main(void)
{
	static const char sentinel[] = "untouched";
	const char *args[3] = { sentinel, sentinel, sentinel };
	char empty[] = " \t\r\n";
	char exact[] = " \talpha  beta\r\n";
	char extra[] = "alpha beta gamma";
	char single[] = "alpha";

	assert(split_line(empty, args, 2) == 0);
	assert(args[0] == sentinel && args[1] == sentinel && args[2] == sentinel);
	assert(split_line(exact, args, 2) == 2);
	assert(!strcmp(args[0], "alpha") && !strcmp(args[1], "beta"));
	assert(args[2] == sentinel);
	assert(split_line(extra, args, 2) == -E2BIG);
	assert(args[2] == sentinel);
	args[0] = sentinel;
	assert(split_line(single, args, 0) == -E2BIG);
	assert(args[0] == sentinel);
	assert(split_line(single, args, 1) == 1 && !strcmp(args[0], "alpha"));
	puts("edit-batch: PASS");
	return 0;
}
