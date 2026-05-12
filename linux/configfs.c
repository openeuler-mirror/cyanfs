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
#include "cyanfs.h"

static inline struct cyanfs_block_device *to_block_device(struct config_item *item)
{
	return container_of(to_config_group(item), struct cyanfs_block_device, group);
}

static ssize_t cyanfs_block_device_id_show(struct config_item *item, char *page)
{
	struct cyanfs_block_device *cbd = to_block_device(item);

	return sprintf(page, "%d\n", cbd->id);
}

CONFIGFS_ATTR_RO(cyanfs_block_device_, id);

static struct configfs_attribute *cyanfs_block_device_item_attrs[] = {
	&cyanfs_block_device_attr_id,
	NULL,
};

static void cyanfs_release_block_device(struct config_item *item)
{
	struct cyanfs_block_device *cbd = to_block_device(item);
	cyanfs_block_device_put(cbd);
}

static struct configfs_item_operations cyanfs_block_device_item_ops = {
	.release = cyanfs_release_block_device,
};

static struct config_item_type cyanfs_block_device_item_type = {
	.ct_owner = THIS_MODULE,
	.ct_attrs = cyanfs_block_device_item_attrs,
	.ct_item_ops = &cyanfs_block_device_item_ops,
};

struct config_group *cyanfs_make_block_device(struct cyanfs_backend *backend, const char *name, int write)
{
	struct cyanfs_block_device *cbd;
	cyanfs_file_name_t filename = { 0 };
	struct cyanfs_file_meta meta;
	int error;

	strcpy((char *)(&filename), name);
	error = cyanfs_lookup(backend->super, filename, &meta);
	if (error < 0)
		goto out;

	cbd = cyanfs_block_device_alloc(backend, meta.id, write);
	if (IS_ERR(cbd)) {
		error = PTR_ERR(cbd);
		goto out;
	}
	config_group_init_type_name(&cbd->group, name, &cyanfs_block_device_item_type);

	return &cbd->group;
out:
	return ERR_PTR(error);
}

struct config_group *cyanfs_make_block_device_ro(struct config_group *group, const char *name)
{
	struct cyanfs_backend *backend = container_of(group, struct cyanfs_backend, devices_ro_group);
	return cyanfs_make_block_device(backend, name, 0);
}

struct config_group *cyanfs_make_block_device_rw(struct config_group *group, const char *name)
{
	struct cyanfs_backend *backend = container_of(group, struct cyanfs_backend, devices_rw_group);
	return cyanfs_make_block_device(backend, name, 1);
}

static struct configfs_group_operations cyanfs_block_devices_ro_group_ops = {
	.make_group = cyanfs_make_block_device_ro,
};

static struct config_item_type cyanfs_block_devices_ro_item_type = {
	.ct_group_ops = &cyanfs_block_devices_ro_group_ops,
	.ct_owner = THIS_MODULE,
};

static struct configfs_group_operations cyanfs_block_devices_rw_group_ops = {
	.make_group = cyanfs_make_block_device_rw,
};

static struct config_item_type cyanfs_block_devices_rw_item_type = {
	.ct_group_ops = &cyanfs_block_devices_rw_group_ops,
	.ct_owner = THIS_MODULE,
};

static inline struct cyanfs_backend *to_backend(struct config_item *item)
{
	return container_of(to_config_group(item), struct cyanfs_backend, group);
}

static ssize_t cyanfs_backend_cmd_statfs_show(struct config_item *item, char *page)
{
	int n = 0;
	struct cyanfs_backend *backend = to_backend(item);
	struct cyanfs_super_meta meta;

	n = cyanfs_statfs(backend->super, &meta);
	if (n < 0)
		return n;

	n += sprintf(page, "    Free     Data  Journal Reserved    Total         Size\n");
	n += sprintf(page + n, "%8u %8u %8u %8u %8u %12llu\n", meta.free_extents, meta.data_extents,
		     meta.journal_extents, meta.reserved_extents, meta.total_extents, meta.size);
	return n;
}

CONFIGFS_ATTR_RO(cyanfs_backend_cmd_, statfs);

static ssize_t cyanfs_backend_cmd_list_show(struct config_item *item, char *page)
{
	int n = 0;
	struct cyanfs_backend *backend = to_backend(item);
	struct cyanfs_file_meta iter = cyanfs_list_init_iter;

	n = sprintf(page, "    ID Parent Child  Extents         Size Name\n");
	page += n;
	while (cyanfs_list(backend->super, &iter) == 0) {
		int r = sprintf(page, "%6llu %6llu %5u %8u %12llu %s\n", iter.id, iter.parent_id, iter.children,
				iter.extents, iter.size, (const char *)(&iter.name));
		n += r;
		page += r;
	}
	return n;
}

CONFIGFS_ATTR_RO(cyanfs_backend_cmd_, list);

static ssize_t cyanfs_backend_cmd_create_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_file_name_t name = { 0 };
	int r;

	if (sscanf(page, "%s", (char *)(&name)) != 1)
		return -EINVAL;

	r = cyanfs_create(backend->super, name, NULL);
	if (r < 0)
		return r;
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, create);

static ssize_t cyanfs_backend_cmd_fork_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_file_name_t from = { 0 }, to = { 0 };
	struct cyanfs_file_meta meta;
	int r;

	if (sscanf(page, "%s %s", (char *)(&from), (char *)(&to)) != 2)
		return -EINVAL;

	r = cyanfs_lookup(backend->super, from, &meta);
	if (r < 0)
		return r;
	r = cyanfs_fork(backend->super, meta.id, to, NULL);
	if (r < 0)
		return r;
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, fork);

static ssize_t cyanfs_backend_cmd_rename_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_file_name_t from = { 0 }, to = { 0 };
	struct cyanfs_file_meta meta;
	int r;

	if (sscanf(page, "%s %s", (char *)(&from), (char *)(&to)) != 2)
		return -EINVAL;

	r = cyanfs_lookup(backend->super, from, &meta);
	if (r < 0)
		return r;
	r = cyanfs_rename(backend->super, meta.id, to);
	if (r < 0)
		return r;
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, rename);

static ssize_t cyanfs_backend_cmd_truncate_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	uint64_t size;
	int r;

	if (sscanf(page, "%s %llu", (char *)(&name), &size) != 2)
		return -EINVAL;

	r = cyanfs_lookup(backend->super, name, &meta);
	if (r < 0)
		return r;
	r = cyanfs_truncate(backend->super, meta.id, round_up(size, CYANFS_FILE_ALIGN_SIZE));
	if (r < 0)
		return r;
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, truncate);

static ssize_t cyanfs_backend_cmd_delete_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_file_name_t name = { 0 };
	struct cyanfs_file_meta meta;
	int r;

	if (sscanf(page, "%s", (char *)(&name)) != 1)
		return -EINVAL;

	r = cyanfs_lookup(backend->super, name, &meta);
	if (r < 0)
		return r;
	r = cyanfs_delete(backend->super, meta.id);
	if (r < 0)
		return r;
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, delete);

static ssize_t cyanfs_backend_cmd_sync_store(struct config_item *item, const char *page, size_t count)
{
	int r;
	struct cyanfs_backend *backend = to_backend(item);

	r = cyanfs_super_flush(backend->super, 1);
	if (r < 0)
		return r;

	flush_work(&backend->work);
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, sync);

static ssize_t cyanfs_backend_cmd_compact_store(struct config_item *item, const char *page, size_t count)
{
	struct cyanfs_backend *backend = to_backend(item);

	cyanfs_super_compact(backend->super);

	flush_work(&backend->work);
	return count;
}

CONFIGFS_ATTR_WO(cyanfs_backend_cmd_, compact);

static struct configfs_attribute *cyanfs_backend_item_attrs[] = {
	&cyanfs_backend_cmd_attr_list, //
	&cyanfs_backend_cmd_attr_statfs, //
	&cyanfs_backend_cmd_attr_create, //
	&cyanfs_backend_cmd_attr_fork, //
	&cyanfs_backend_cmd_attr_rename, //
	&cyanfs_backend_cmd_attr_truncate, //
	&cyanfs_backend_cmd_attr_delete, //
	&cyanfs_backend_cmd_attr_sync, //
	&cyanfs_backend_cmd_attr_compact, //
	NULL,
};

static void cyanfs_release_backend(struct config_item *item)
{
	struct cyanfs_backend *backend = to_backend(item);
	cyanfs_backend_put(backend);
}

static struct configfs_item_operations cyanfs_backend_item_ops = {
	.release = cyanfs_release_backend,
};

static struct config_item_type cyanfs_backend_item_type = {
	.ct_item_ops = &cyanfs_backend_item_ops,
	.ct_attrs = cyanfs_backend_item_attrs,
	.ct_owner = THIS_MODULE,
};

static int cyanfs_lookup_bdev(const char *path, dev_t *dev)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
	return lookup_bdev(path, dev);
#else
	struct block_device *bdev;
	bdev = lookup_bdev(path);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);
	*dev = bdev->bd_dev;
	bdput(bdev);
	return 0;
#endif
}

struct config_group *cyanfs_make_backend(struct config_group *group, const char *name)
{
	int error;
	dev_t dev = 0;
	struct cyanfs_backend *backend;
	char devpath[128];

	strcpy(devpath, "/dev/");
	strcpy(devpath + 5, name);

	error = cyanfs_lookup_bdev(devpath, &dev);
	if (error < 0)
		goto out;

	backend = cyanfs_backend_open(dev);
	if (IS_ERR(backend)) {
		error = PTR_ERR(backend);
		goto out;
	}

	config_group_init_type_name(&backend->group, name, &cyanfs_backend_item_type);
	config_group_init_type_name(&backend->devices_ro_group, "devices_ro", &cyanfs_block_devices_ro_item_type);
	configfs_add_default_group(&backend->devices_ro_group, &backend->group);
	config_group_init_type_name(&backend->devices_rw_group, "devices_rw", &cyanfs_block_devices_rw_item_type);
	configfs_add_default_group(&backend->devices_rw_group, &backend->group);

	return &backend->group;

out:
	return ERR_PTR(error);
}

static struct configfs_group_operations cyanfs_config_subsys_ops = {
	.make_group = cyanfs_make_backend,
};

static const struct config_item_type cyanfs_config_subsys_type = {
	.ct_owner = THIS_MODULE,
	.ct_group_ops = &cyanfs_config_subsys_ops,
};

static struct configfs_subsystem cyanfs_config_subsys = {
	.su_group = {
		.cg_item = {
			.ci_namebuf = "cyanfs",
			.ci_type = &cyanfs_config_subsys_type,
		},
	},
};

int cyanfs_configfs_register(void)
{
	config_group_init(&cyanfs_config_subsys.su_group);
	return configfs_register_subsystem(&cyanfs_config_subsys);
}

void cyanfs_configfs_unregister(void)
{
	configfs_unregister_subsystem(&cyanfs_config_subsys);
}
