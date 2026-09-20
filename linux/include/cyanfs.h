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
#ifndef __CYANFS_HEADER__
#define __CYANFS_HEADER__

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "../../core/core.h"
#else
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CYANFS_DEVNAME "cbd"

#define CYANFS_IOCTL_DEVNAME "cyanfs"
#define CYANFS_IOCTL_MAJOR_NUM 0x9C

#ifndef __KERNEL__

#define CYANFS_EXTENT_SHIFT (20ULL)
#define CYANFS_EXTENT_SIZE (1ULL << CYANFS_EXTENT_SHIFT)
#define CYANFS_EXTENT_INDEX_BITS 30
#define CYANFS_EXTENT_COUNT_MAX (1ULL << CYANFS_EXTENT_INDEX_BITS)
#define CYANFS_EXTENT_INDEX_MAX (CYANFS_EXTENT_COUNT_MAX - 1ULL)
#define CYANFS_FILE_MAX_SIZE (CYANFS_EXTENT_COUNT_MAX << CYANFS_EXTENT_SHIFT)

#define CYANFS_FILE_ALIGN_SIZE (1ULL << 12)
#define CYANFS_FILE_ALIGN_MASK (CYANFS_FILE_ALIGN_SIZE - 1ULL)

/* Shared with core/core.h without requiring it for standalone UAPI users. */
#ifndef __CYANFS_FILE_META_TYPES_DEFINED__
#define __CYANFS_FILE_META_TYPES_DEFINED__

typedef uint64_t cyanfs_file_id_t;

typedef struct {
	uint8_t data[127];
	uint8_t zero;
} cyanfs_file_name_t;

struct cyanfs_file_meta {
	cyanfs_file_id_t id;
	cyanfs_file_id_t parent_id;
	cyanfs_file_name_t name;
	uint64_t size;
	uint32_t extents;
	uint32_t children;
};

static const struct cyanfs_file_meta cyanfs_list_init_iter = { 0 };
#endif

#ifndef __CYANFS_SUPER_META_TYPES_DEFINED__
#define __CYANFS_SUPER_META_TYPES_DEFINED__

typedef struct {
	uint32_t data[4];
} cyanfs_uuid_t;

struct cyanfs_super_meta {
	cyanfs_uuid_t uuid;
	uint32_t total_extents;
	uint32_t free_extents;
	uint32_t journal_extents;
	uint32_t data_extents;
	uint32_t reserved_extents;
	uint64_t size;
	uint32_t files;
};
#endif
#endif

static inline int cyanfs_file_name_copy(cyanfs_file_name_t *dst, const char *src)
{
	size_t len;

	if (!dst || !src)
		return -EINVAL;

	len = strnlen(src, sizeof(dst->data) + 1);
	if (len > sizeof(dst->data))
		return -ENAMETOOLONG;

	memset(dst, 0, sizeof(*dst));
	memcpy(dst->data, src, len);
	return 0;
}

struct cyanfs_dev {
	uint32_t major;
	uint32_t minor;
};

typedef struct {
	struct cyanfs_dev dev;
} cyanfs_ioctl_backend_mount_t;
#define CYANFS_IOCTL_BACKEND_MOUNT _IO(CYANFS_IOCTL_MAJOR_NUM, 0x0)

typedef struct {
	struct cyanfs_dev dev;
} cyanfs_ioctl_backend_list_t;
#define CYANFS_IOCTL_BACKEND_LIST _IO(CYANFS_IOCTL_MAJOR_NUM, 0x1)

typedef struct {
	struct cyanfs_dev dev;
} cyanfs_ioctl_backend_umount_t;
#define CYANFS_IOCTL_BACKEND_UMOUNT _IO(CYANFS_IOCTL_MAJOR_NUM, 0x2)

typedef struct {
	struct cyanfs_dev dev;
	struct cyanfs_super_meta meta;
} cyanfs_ioctl_backend_statfs_t;
#define CYANFS_IOCTL_BACKEND_STATFS _IO(CYANFS_IOCTL_MAJOR_NUM, 0x3)

typedef struct {
	struct cyanfs_dev dev;
} cyanfs_ioctl_backend_sync_t;
#define CYANFS_IOCTL_BACKEND_SYNC _IO(CYANFS_IOCTL_MAJOR_NUM, 0x4)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	uint32_t readonly;
	uint32_t vdisk;
} cyanfs_ioctl_device_map_t;
#define CYANFS_IOCTL_DEVICE_MAP _IO(CYANFS_IOCTL_MAJOR_NUM, 0x10)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
} cyanfs_ioctl_device_unmap_t;
#define CYANFS_IOCTL_DEVICE_UNMAP _IO(CYANFS_IOCTL_MAJOR_NUM, 0x11)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	uint32_t vdisk;
} cyanfs_ioctl_device_list_t;
#define CYANFS_IOCTL_DEVICE_LIST _IO(CYANFS_IOCTL_MAJOR_NUM, 0x12)

typedef struct {
	struct cyanfs_dev dev;
	struct cyanfs_file_meta meta;
} cyanfs_ioctl_file_list_t;
#define CYANFS_IOCTL_FILE_LIST _IO(CYANFS_IOCTL_MAJOR_NUM, 0x20)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_name_t name;
	struct cyanfs_file_meta meta;
} cyanfs_ioctl_file_lookup_t;
#define CYANFS_IOCTL_FILE_LOOKUP _IO(CYANFS_IOCTL_MAJOR_NUM, 0x21)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_name_t name;
	cyanfs_file_id_t id;
} cyanfs_ioctl_file_create_t;
#define CYANFS_IOCTL_FILE_CREATE _IO(CYANFS_IOCTL_MAJOR_NUM, 0x22)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t pid;
	cyanfs_file_name_t name;
	cyanfs_file_id_t id;
} cyanfs_ioctl_file_fork_t;
#define CYANFS_IOCTL_FILE_FORK _IO(CYANFS_IOCTL_MAJOR_NUM, 0x23)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	cyanfs_file_name_t name;
} cyanfs_ioctl_file_rename_t;
#define CYANFS_IOCTL_FILE_RENAME _IO(CYANFS_IOCTL_MAJOR_NUM, 0x24)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	uint64_t size;
} cyanfs_ioctl_file_truncate_t;
#define CYANFS_IOCTL_FILE_TRUNCATE _IO(CYANFS_IOCTL_MAJOR_NUM, 0x25)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
} cyanfs_ioctl_file_delete_t;
#define CYANFS_IOCTL_FILE_DELETE _IO(CYANFS_IOCTL_MAJOR_NUM, 0x26)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	struct cyanfs_file_meta meta;
} cyanfs_ioctl_file_stat_t;
#define CYANFS_IOCTL_FILE_STAT _IO(CYANFS_IOCTL_MAJOR_NUM, 0x27)

typedef struct {
	struct cyanfs_dev dev;
	cyanfs_file_id_t id;
	uint32_t readonly;
} cyanfs_ioctl_file_bind_t;
#define CYANFS_IOCTL_FILE_BIND _IO(CYANFS_IOCTL_MAJOR_NUM, 0x28)

#ifdef __cplusplus
}
#endif

#endif
