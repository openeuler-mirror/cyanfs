#include "utils.h"

int main(int argc, const char *argv[])
{
	int fd, r;
	cyanfs_uuid_t uuid;
	char block[CYANFS_SUPER_BLOCK_SIZE];

	if (argc != 2) {
		fprintf(stderr, "example: %s /dev/sda\n", argv[0]);
		return -1;
	}

	fd = open("/dev/urandom", O_RDONLY, 0);
	if (fd < 0) {
		fprintf(stderr, "failed to open urandom device\n");
		return -1;
	}
	r = safe_read(fd, &uuid, sizeof(uuid));
	if (r < 0) {
		fprintf(stderr, "failed to read urandom device\n");
		return -1;
	}
	close(fd);

	fd = open(argv[1], O_RDWR | O_EXCL | O_SYNC, 0);
	if (fd < 0) {
		fprintf(stderr, "failed to open device\n");
		return -1;
	}
	cyanfs_super_make(uuid, block);
	r = safe_pwrite(fd, block, 0, CYANFS_SUPER_BLOCK_SIZE);
	if (r < 0) {
		fprintf(stderr, "failed to write super block\n");
		return -1;
	}
	close(fd);

	printf("mkfs done.\n");
	return 0;
}
