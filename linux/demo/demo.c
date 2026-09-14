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
#define _GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "../include/cyanfs.h"

static int fd;

static int parse_dev(const char *path, struct cyanfs_dev *dev)
{
	struct stat stat_buf;
	if (stat(path, &stat_buf) < 0) {
		fprintf(stderr, "failed to stat device %s\n", path);
		return -ENOENT;
	}
	if (!S_ISBLK(stat_buf.st_mode)) {
		fprintf(stderr, "%s is not block device\n", path);
		return -ENODEV;
	}
	dev->major = major(stat_buf.st_rdev);
	dev->minor = minor(stat_buf.st_rdev);
	return 0;
}

static int __do_file_lookup(struct cyanfs_dev dev, cyanfs_file_name_t name, struct cyanfs_file_meta *meta)
{
	cyanfs_ioctl_file_lookup_t lookup = { .dev = dev, .name = name };
	if (ioctl(fd, CYANFS_IOCTL_FILE_LOOKUP, &lookup) < 0)
		return -errno;
	*meta = lookup.meta;
	return 0;
}

static int do_list_mounts(int argc, const char *argv[])
{
	cyanfs_ioctl_backend_list_t lists = { 0 };
	printf("MAJOR:MINOR\n");
	while (ioctl(fd, CYANFS_IOCTL_BACKEND_LIST, &lists) == 0) {
		printf("%5" PRIu32 " %5" PRIu32 "\n", lists.dev.major, lists.dev.minor);
	}
	return 0;
}

static int do_mount(int argc, const char *argv[])
{
	cyanfs_ioctl_backend_mount_t mount;
	int r;
	if (argc < 1)
		return -EINVAL;
	r = parse_dev(argv[0], &mount.dev);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_BACKEND_MOUNT, &mount) < 0)
		return -errno;
	return 0;
}

static int do_umount(int argc, const char *argv[])
{
	cyanfs_ioctl_backend_umount_t umount;
	int r;
	if (argc < 1)
		return -EINVAL;
	r = parse_dev(argv[0], &umount.dev);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_BACKEND_UMOUNT, &umount) < 0)
		return -errno;
	return 0;
}

static int do_statfs(int argc, const char *argv[])
{
	cyanfs_ioctl_backend_statfs_t statfs;
	int r;
	if (argc < 1)
		return -EINVAL;
	r = parse_dev(argv[0], &statfs.dev);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_BACKEND_STATFS, &statfs) < 0)
		return -errno;
	printf("    Free     Data  Journal Reserved    Total         Size\n");
	printf("%8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %8" PRIu32 " %12" PRIu64 "\n",
	       statfs.meta.free_extents, statfs.meta.data_extents, statfs.meta.journal_extents,
	       statfs.meta.reserved_extents, statfs.meta.total_extents, statfs.meta.size);
	return 0;
}

static int do_sync(int argc, const char *argv[])
{
	cyanfs_ioctl_backend_sync_t sync;
	int r;
	if (argc < 1)
		return -EINVAL;
	r = parse_dev(argv[0], &sync.dev);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_BACKEND_SYNC, &sync) < 0)
		return -errno;
	return 0;
}

static int do_map(int argc, const char *argv[])
{
	cyanfs_ioctl_device_map_t map;
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &map.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(map.dev, name, &meta);
	if (r < 0)
		return r;
	map.id = meta.id;
	map.readonly = 0;
	if (ioctl(fd, CYANFS_IOCTL_DEVICE_MAP, &map) < 0)
		return -errno;
	printf("map to /dev/" CYANFS_DEVNAME "%u\n", map.vdisk);
	return 0;
}

static int do_unmap(int argc, const char *argv[])
{
	cyanfs_ioctl_device_unmap_t unmap;
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &unmap.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(unmap.dev, name, &meta);
	if (r < 0)
		return r;
	unmap.id = meta.id;
	if (ioctl(fd, CYANFS_IOCTL_DEVICE_UNMAP, &unmap) < 0)
		return -errno;
	return 0;
}

static int do_maps(int argc, const char *argv[])
{
	struct cyanfs_dev dev;
	cyanfs_ioctl_device_list_t lists = { 0 };
	int use_dev = 0;
	if (argc > 0) {
		int r;
		use_dev = 1;
		r = parse_dev(argv[0], &dev);
		if (r < 0)
			return r;
		lists.dev = dev;
	}
	printf("MAJOR:MINOR FILE-ID VDISK\n");
	while (ioctl(fd, CYANFS_IOCTL_DEVICE_LIST, &lists) == 0) {
		if (use_dev && memcmp(&lists.dev, &dev, sizeof(dev)))
			break;
		printf("%5" PRIu32 " %5" PRIu32 " %7" PRIu64 " /dev/" CYANFS_DEVNAME "%u\n", //
		       lists.dev.major, lists.dev.minor, lists.id, lists.vdisk);
	}
	return 0;
}

static int do_file_list(int argc, const char *argv[])
{
	cyanfs_ioctl_file_list_t lists = { 0 };
	int r;
	if (argc < 1)
		return -EINVAL;
	r = parse_dev(argv[0], &lists.dev);
	if (r < 0)
		return r;
	printf("    ID Parent Child  Extents         Size Name\n");
	while (ioctl(fd, CYANFS_IOCTL_FILE_LIST, &lists) == 0) {
		printf("%6" PRIu64 " %6" PRIu64 " %5" PRIu32 " %8" PRIu32 " %12" PRIu64 " %s\n", lists.meta.id,
		       lists.meta.parent_id, lists.meta.children, lists.meta.extents, lists.meta.size,
		       (const char *)(&lists.meta.name));
	}
	return 0;
}

static int do_file_create(int argc, const char *argv[])
{
	cyanfs_ioctl_file_create_t create = { 0 };
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &create.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&create.name, argv[1]);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_FILE_CREATE, &create) < 0)
		return -errno;
	return 0;
}

static int do_file_fork(int argc, const char *argv[])
{
	cyanfs_ioctl_file_fork_t fork = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 3)
		return -EINVAL;
	r = parse_dev(argv[0], &fork.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(fork.dev, name, &meta);
	if (r < 0)
		return r;
	fork.pid = meta.id;
	r = cyanfs_file_name_copy(&fork.name, argv[2]);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_FILE_FORK, &fork) < 0)
		return -errno;
	return 0;
}

static int do_file_rename(int argc, const char *argv[])
{
	cyanfs_ioctl_file_rename_t rename = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 3)
		return -EINVAL;
	r = parse_dev(argv[0], &rename.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(rename.dev, name, &meta);
	if (r < 0)
		return r;
	rename.id = meta.id;
	r = cyanfs_file_name_copy(&rename.name, argv[2]);
	if (r < 0)
		return r;
	if (ioctl(fd, CYANFS_IOCTL_FILE_RENAME, &rename) < 0)
		return -errno;
	return 0;
}

static int do_file_truncate(int argc, const char *argv[])
{
	cyanfs_ioctl_file_truncate_t truncate = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 3)
		return -EINVAL;
	r = parse_dev(argv[0], &truncate.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(truncate.dev, name, &meta);
	if (r < 0)
		return r;
	truncate.id = meta.id;
	if (sscanf(argv[2], "%" PRIu64, &truncate.size) != 1)
		return -EINVAL;
	if (truncate.size > CYANFS_FILE_MAX_SIZE)
		return -EINVAL;
	truncate.size = (truncate.size + CYANFS_FILE_ALIGN_MASK) & ~CYANFS_FILE_ALIGN_MASK;
	if (ioctl(fd, CYANFS_IOCTL_FILE_TRUNCATE, &truncate) < 0)
		return -errno;
	return 0;
}

static int do_file_delete(int argc, const char *argv[])
{
	cyanfs_ioctl_file_delete_t delete = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &delete.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(delete.dev, name, &meta);
	if (r < 0)
		return r;
	delete.id = meta.id;
	if (ioctl(fd, CYANFS_IOCTL_FILE_DELETE, &delete) < 0)
		return -errno;
	return 0;
}

static int do_file_stat(int argc, const char *argv[])
{
	cyanfs_ioctl_file_stat_t stat = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &stat.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(stat.dev, name, &meta);
	if (r < 0)
		return r;
	stat.id = meta.id;
	if (ioctl(fd, CYANFS_IOCTL_FILE_STAT, &stat) < 0)
		return -errno;
	printf("    ID Parent Child  Extents         Size Name\n");
	printf("%6" PRIu64 " %6" PRIu64 " %5" PRIu32 " %8" PRIu32 " %12" PRIu64 " %s\n", stat.meta.id,
	       stat.meta.parent_id, stat.meta.children, stat.meta.extents, stat.meta.size,
	       (const char *)(&stat.meta.name));
	return 0;
}

static int do_file_read(int argc, const char *argv[])
{
	cyanfs_ioctl_file_bind_t bind = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	char buf[65536];
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &bind.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(bind.dev, name, &meta);
	if (r < 0)
		return r;
	bind.id = meta.id;
	bind.readonly = 1;
	if (ioctl(fd, CYANFS_IOCTL_FILE_BIND, &bind) < 0)
		return -errno;
	if (argc >= 3) {
		uint64_t offset;
		if (sscanf(argv[2], "%" PRIu64, &offset) != 1)
			return -EINVAL;
		if (lseek(fd, offset, SEEK_SET) < 0)
			return -errno;
	}
	for (;;) {
		r = read(fd, buf, 65536);
		if (r < 0)
			return -errno;
		if (r == 0)
			return 0;
		r = write(1, buf, r);
		if (r < 0)
			return -errno;
	}
}

static int do_file_write(int argc, const char *argv[])
{
	cyanfs_ioctl_file_bind_t bind = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	char buf[65536];
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &bind.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(bind.dev, name, &meta);
	if (r < 0)
		return r;
	bind.id = meta.id;
	bind.readonly = 0;
	if (ioctl(fd, CYANFS_IOCTL_FILE_BIND, &bind) < 0)
		return -errno;
	if (argc >= 3) {
		uint64_t offset;
		if (sscanf(argv[2], "%" PRIu64, &offset) != 1)
			return -EINVAL;
		if (lseek(fd, offset, SEEK_SET) < 0)
			return -errno;
	}
	for (;;) {
		r = read(0, buf, 65536);
		if (r < 0)
			return -errno;
		if (r == 0)
			return 0;
		r = write(fd, buf, r);
		if (r < 0)
			return -errno;
	}
}

static int do_file_import(int argc, const char *argv[])
{
	cyanfs_ioctl_file_truncate_t truncate = { 0 };
	cyanfs_ioctl_file_bind_t bind = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	struct stat s;
	const int SIZE = 65536;
	char buf[SIZE];
	int r, f;
	off_t offset = 0;
	if (argc < 3)
		return -EINVAL;
	r = parse_dev(argv[0], &bind.dev);
	if (r < 0)
		return r;
	memcpy(&truncate.dev, &bind.dev, sizeof(bind.dev));
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	f = open(argv[2], O_RDONLY);
	if (f < 0)
		return -errno;
	r = fstat(f, &s);
	if (r < 0)
		return -errno;
	r = __do_file_lookup(bind.dev, name, &meta);
	if (r < 0)
		return r;
	bind.id = meta.id;
	bind.readonly = 0;
	truncate.id = meta.id;
	truncate.size = s.st_size;
	if (ioctl(fd, CYANFS_IOCTL_FILE_TRUNCATE, &truncate) < 0)
		return -errno;
	if (ioctl(fd, CYANFS_IOCTL_FILE_BIND, &bind) < 0)
		return -errno;
	for (;;) {
		int l;
		offset = lseek(f, offset, SEEK_DATA);
		if (offset < 0)
			break;
		l = SIZE - (offset & (SIZE - 1));
		r = read(f, buf, l);
		if (r < 0)
			return -errno;
		if (r == 0)
			break;
		if (lseek(fd, offset, SEEK_SET) < 0)
			return -errno;
		r = write(fd, buf, r);
		if (r < 0)
			return -errno;
		offset += r;
	}
	close(f);
	return 0;
}

static int do_file_extents(int argc, const char *argv[])
{
	cyanfs_ioctl_file_bind_t bind = { 0 };
	struct cyanfs_file_meta meta;
	cyanfs_file_name_t name = { 0 };
	off_t offset = 0;
	int r;
	if (argc < 2)
		return -EINVAL;
	r = parse_dev(argv[0], &bind.dev);
	if (r < 0)
		return r;
	r = cyanfs_file_name_copy(&name, argv[1]);
	if (r < 0)
		return r;
	r = __do_file_lookup(bind.dev, name, &meta);
	if (r < 0)
		return r;
	bind.id = meta.id;
	bind.readonly = 1;
	if (ioctl(fd, CYANFS_IOCTL_FILE_BIND, &bind) < 0)
		return -errno;
	while ((offset = lseek(fd, offset, SEEK_DATA)) >= 0) {
		printf("extents: %" PRIu64 "\n", offset);
		offset += CYANFS_EXTENT_SIZE;
	}
	return 0;
}

typedef int (*cli_fn)(int argc, const char *argv[]);

struct op_map {
	const char *name;
	cli_fn fn;
};

static struct op_map op_maps[] = {
	{
		.name = "mounts",
		.fn = do_list_mounts,
	},
	{
		.name = "mount",
		.fn = do_mount,
	},
	{
		.name = "umount",
		.fn = do_umount,
	},
	{
		.name = "statfs",
		.fn = do_statfs,
	},
	{
		.name = "sync",
		.fn = do_sync,
	},
	{
		.name = "map",
		.fn = do_map,
	},
	{
		.name = "unmap",
		.fn = do_unmap,
	},
	{
		.name = "maps",
		.fn = do_maps,
	},
	{
		.name = "list",
		.fn = do_file_list,
	},
	{
		.name = "create",
		.fn = do_file_create,
	},
	{
		.name = "fork",
		.fn = do_file_fork,
	},
	{
		.name = "rename",
		.fn = do_file_rename,
	},
	{
		.name = "truncate",
		.fn = do_file_truncate,
	},
	{
		.name = "delete",
		.fn = do_file_delete,
	},
	{
		.name = "stat",
		.fn = do_file_stat,
	},
	{
		.name = "read",
		.fn = do_file_read,
	},
	{
		.name = "write",
		.fn = do_file_write,
	},
	{
		.name = "import",
		.fn = do_file_import,
	},
	{
		.name = "extents",
		.fn = do_file_extents,
	},
};

static void usage(const char *exe)
{
	fprintf(stderr, //
		"usage: %s [command]\n"
		"commands:\n"
		"  mounts\n"
		"  mount DEV-PATH\n"
		"  umount DEV-PATH\n"
		"  statfs DEV-PATH\n"
		"  map DEV-PATH FILE\n"
		"  unmap DEV-PATH FILE\n"
		"  maps [DEV-PATH]\n"
		"  list DEV-PATH\n"
		"  create DEV-PATH FILENAME\n"
		"  fork DEV-PATH FILENAME-FROM FILENAME-TO\n"
		"  rename DEV-PATH FILENAME-FROM FILENAME-TO\n"
		"  truncate DEV-PATH FILENAME SIZE\n"
		"  delete DEV-PATH FILENAME\n"
		"  stat DEV-PATH FILENAME\n"
		"  read DEV-PATH FILENAME [OFFSET]\n"
		"  write DEV-PATH FILENAME [OFFSET]\n"
		"  import DEV-PATH FILENAME RAW-IMAGE-FILE\n"
		"  extents DEV-PATH FILENAME\n",
		exe);
}

int main(int argc, const char *argv[])
{
	struct op_map *map = NULL;
	int i;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	fd = open("/dev/" CYANFS_IOCTL_DEVNAME, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "failed to init cyanfs: %s\n", strerror(errno));
		return -1;
	}

	for (i = 0; i < sizeof(op_maps) / sizeof(struct op_map); ++i) {
		if (strcmp(argv[1], op_maps[i].name) == 0) {
			map = &op_maps[i];
		}
	}
	if (!map) {
		fprintf(stderr, "%s is not a command\n", argv[1]);
		return -1;
	}
	i = map->fn(argc - 2, argv + 2);
	if (i < 0) {
		fprintf(stderr, "execute %s failed: %s\n", argv[1], strerror(-i));
		return -1;
	}
	return 0;
}
