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
