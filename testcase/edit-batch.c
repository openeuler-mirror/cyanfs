/* Contracts for the real batch tokenizer and end-of-input handling. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>

static FILE *test_input;
static int fail_line_allocation;
static FILE *batch_fopen(const char *, const char *);
static ssize_t batch_getline(char **, size_t *, FILE *);
#define main cyanfs_edit_main
#define fopen batch_fopen
#define getline batch_getline
#include "../utils/edit.c"
#undef main
#undef fopen
#undef getline

static FILE *batch_fopen(const char *path, const char *mode)
{
	assert(test_input);
	return test_input;
}

static ssize_t batch_getline(char **line, size_t *size, FILE *file)
{
	if (fail_line_allocation) {
		errno = ENOMEM;
		return -1;
	}
	return getline(line, size, file);
}

static ssize_t fail_read(void *cookie, char *buffer, size_t size)
{
	errno = *(int *)cookie;
	return -1;
}

static FILE *input_string(const char *text)
{
	FILE *file = tmpfile();
	assert(file);
	assert(fputs(text, file) >= 0);
	rewind(file);
	return file;
}

static void check_batch(FILE *file, int expected)
{
	struct disk disk = { 0 };
	const char *args[] = { "fixture" };
	assert(file);
	test_input = file;
	errno = EAGAIN;
	assert(do_batch(&disk, 1, args) == expected);
	/* do_batch closes its fopen result, including on error. */
	test_input = NULL;
}

int main(void)
{
	static const char sentinel[] = "untouched";
	const char *args[3] = { sentinel, sentinel, sentinel };
	char empty[] = " \t\r\n";
	char exact[] = " \talpha  beta\r\n";
	char extra[] = "alpha beta gamma";
	char single[] = "alpha";
	int read_error = EIO;
	cookie_io_functions_t io = { .read = fail_read };

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
	check_batch(input_string(""), 0);
	check_batch(input_string(" \t\r\n\n"), 0);
	check_batch(input_string("sleep 0\n"), 0);
	check_batch(input_string("sleep 0"), 0);
	check_batch(fopencookie(&read_error, "r", io), -EIO);
	read_error = 0;
	check_batch(fopencookie(&read_error, "r", io), -EIO);
	fail_line_allocation = 1;
	check_batch(input_string("sleep 0\n"), -ENOMEM);
	fail_line_allocation = 0;
	puts("edit-batch: PASS");
	return 0;
}
