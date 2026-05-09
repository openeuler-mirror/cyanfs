/**
 * Copyright (c) 2025 ~ 2026 KylinSec Co., Ltd.
 * cyanfs is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 *
 * Author: huhuikai <huhuikai@kylinsec.com.cn>
 * Author: wenyunchuan <wenyunchuan@kylinsec.com.cn>
 * Author: yuanzhu <yuanzhu@kylinsec.com.cn>
*/
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
