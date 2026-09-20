/* Compile/run in each header combination; compare layouts across combinations. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#if defined(TEST_CORE_ONLY)
#include "core/core.h"
#include "core/core.h"
#elif defined(TEST_CORE_FIRST)
#include "core/core.h"
#include "cyanfs.h"
#include "core/core.h"
#include "cyanfs.h"
#elif defined(TEST_UAPI_FIRST)
#include "cyanfs.h"
#include "core/core.h"
#include "cyanfs.h"
#include "core/core.h"
#else
#include "cyanfs.h"
#include "cyanfs.h"
#endif

#define SIZE(type) printf("%s=%zu\n", #type, sizeof(type))
#define OFFSET(type, field) printf("%s.%s=%zu\n", #type, #field, offsetof(type, field))

int main(void)
{
	struct cyanfs_file_meta iter = cyanfs_list_init_iter;
	assert(iter.id == 0 && iter.parent_id == 0 && iter.name.data[0] == 0);
	assert(iter.name.zero == 0 && iter.size == 0 && iter.extents == 0 && iter.children == 0);
	assert(CYANFS_EXTENT_SIZE == 1048576ULL);
	assert(CYANFS_EXTENT_INDEX_MAX == 1073741823ULL);
	assert(CYANFS_FILE_MAX_SIZE == 1125899906842624ULL);
	assert(CYANFS_FILE_ALIGN_SIZE == 4096ULL);
	SIZE(cyanfs_file_id_t);
	SIZE(cyanfs_file_name_t);
	OFFSET(cyanfs_file_name_t, zero);
	SIZE(struct cyanfs_file_meta);
	OFFSET(struct cyanfs_file_meta, id);
	OFFSET(struct cyanfs_file_meta, parent_id);
	OFFSET(struct cyanfs_file_meta, name);
	OFFSET(struct cyanfs_file_meta, size);
	OFFSET(struct cyanfs_file_meta, extents);
	OFFSET(struct cyanfs_file_meta, children);
	SIZE(cyanfs_uuid_t);
	SIZE(struct cyanfs_super_meta);
	OFFSET(struct cyanfs_super_meta, uuid);
	OFFSET(struct cyanfs_super_meta, total_extents);
	OFFSET(struct cyanfs_super_meta, free_extents);
	OFFSET(struct cyanfs_super_meta, journal_extents);
	OFFSET(struct cyanfs_super_meta, data_extents);
	OFFSET(struct cyanfs_super_meta, reserved_extents);
	OFFSET(struct cyanfs_super_meta, size);
	OFFSET(struct cyanfs_super_meta, files);

#if !defined(TEST_CORE_ONLY)
	{
		cyanfs_file_name_t name;
		char longest[128], overlong[129];
		size_t i;
		memset(longest, 'x', sizeof(longest));
		longest[127] = 0;
		memset(overlong, 'x', sizeof(overlong));
		overlong[128] = 0;
		assert(!cyanfs_file_name_copy(&name, longest));
		assert(!memcmp(name.data, longest, 127) && name.zero == 0);
		assert(!cyanfs_file_name_copy(&name, "x"));
		for (i = 1; i < sizeof(name.data); ++i)
			assert(name.data[i] == 0);
		assert(name.zero == 0);
		assert(cyanfs_file_name_copy(&name, overlong) == -ENAMETOOLONG);
		assert(cyanfs_file_name_copy(NULL, "x") == -EINVAL);
		assert(cyanfs_file_name_copy(&name, NULL) == -EINVAL);
	}
#define IOCTL(name, type, value)                                                                                       \
	do {                                                                                                           \
		assert(CYANFS_IOCTL_##name == _IO(0x9C, value));                                                        \
		printf("ioctl:%s=%zu\n", #type, sizeof(cyanfs_ioctl_##type##_t));                                      \
	} while (0)
	IOCTL(BACKEND_MOUNT, backend_mount, 0x00);
	IOCTL(BACKEND_LIST, backend_list, 0x01);
	IOCTL(BACKEND_UMOUNT, backend_umount, 0x02);
	IOCTL(BACKEND_STATFS, backend_statfs, 0x03);
	IOCTL(BACKEND_SYNC, backend_sync, 0x04);
	IOCTL(DEVICE_MAP, device_map, 0x10);
	IOCTL(DEVICE_UNMAP, device_unmap, 0x11);
	IOCTL(DEVICE_LIST, device_list, 0x12);
	IOCTL(FILE_LIST, file_list, 0x20);
	IOCTL(FILE_LOOKUP, file_lookup, 0x21);
	IOCTL(FILE_CREATE, file_create, 0x22);
	IOCTL(FILE_FORK, file_fork, 0x23);
	IOCTL(FILE_RENAME, file_rename, 0x24);
	IOCTL(FILE_TRUNCATE, file_truncate, 0x25);
	IOCTL(FILE_DELETE, file_delete, 0x26);
	IOCTL(FILE_STAT, file_stat, 0x27);
	IOCTL(FILE_BIND, file_bind, 0x28);
#endif
	return 0;
}
