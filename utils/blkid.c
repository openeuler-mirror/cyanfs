#include "utils.h"

int main(int argc, const char *argv[])
{
	int fd, r;
	struct cyanfs_super_header h;
	uint8_t *u = (uint8_t *)(&h.uuid);
	char block[CYANFS_SUPER_BLOCK_SIZE];

	if (argc != 2) {
		fprintf(stderr, "example: %s /dev/sda\n", argv[0]);
		return -1;
	}

	fd = open(argv[1], O_RDONLY, 0);
	if (fd < 0) {
		fprintf(stderr, "failed to open device\n");
		return -1;
	}
	r = safe_pread(fd, block, 0, CYANFS_SUPER_BLOCK_SIZE);
	if (r < 0) {
		fprintf(stderr, "failed to read super block\n");
		return -1;
	}
	close(fd);

	r = cyanfs_super_parse(&h, block);
	if (r < 0) {
		fprintf(stderr, "not a valid cyanfs\n");
		return -1;
	}

	printf("%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", //
	       u[0], u[1], u[2], u[3], //
	       u[4], u[5], //
	       u[6], u[7], //
	       u[8], u[9], //
	       u[10], u[11], u[12], u[13], u[14], u[15]);
	return 0;
}
