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

static DEFINE_MUTEX(cyanfs_ctl_lock);

static CYANFS_RB_HEAD(cyanfs_backend_rb, cyanfs_backend) cyanfs_backend_rb;
static inline int __cyanfs_backend_compare(struct cyanfs_backend *a, struct cyanfs_backend *b)
{
	if (a->dev_id < b->dev_id)
		return -1;
	else if (a->dev_id > b->dev_id)
		return 1;
	else
		return 0;
}
CYANFS_RB_GENERATE_INTERNAL(cyanfs_backend_rb, cyanfs_backend, node, __cyanfs_backend_compare, static inline)

static CYANFS_RB_HEAD(cyanfs_block_device_rb, cyanfs_block_device) cyanfs_block_device_rb;
static inline int __cyanfs_block_device_compare(struct cyanfs_block_device *a, struct cyanfs_block_device *b)
{
	int r = __cyanfs_backend_compare(a->backend, b->backend);
	if (r)
		return r;
	else if (a->file_id < b->file_id)
		return -1;
	else if (a->file_id > b->file_id)
		return 1;
	else
		return 0;
}
CYANFS_RB_GENERATE_INTERNAL(cyanfs_block_device_rb, cyanfs_block_device, node, __cyanfs_block_device_compare,
			    static inline)

static struct cyanfs_backend *cyanfs_ctl_backend_lookup(struct cyanfs_dev *dev)
{
	struct cyanfs_backend tmp = { .dev_id = MKDEV(dev->major, dev->minor) };
	return CYANFS_RB_FIND(cyanfs_backend_rb, &cyanfs_backend_rb, &tmp);
}

static int cyanfs_ctl_backend_mount(struct file *file, cyanfs_ioctl_backend_mount_t *v)
{
	dev_t dev = MKDEV(v->dev.major, v->dev.minor);
	struct cyanfs_backend *backend;
	backend = cyanfs_backend_open(dev);
	if (IS_ERR(backend))
		return PTR_ERR(backend);
	CYANFS_RB_INSERT(cyanfs_backend_rb, &cyanfs_backend_rb, backend);
	return 0;
}

static int cyanfs_ctl_backend_umount(struct file *file, cyanfs_ioctl_backend_umount_t *v)
{
	struct cyanfs_backend *backend;
	struct cyanfs_block_device tmp_device, *cbd;

	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;

	tmp_device.backend = backend;
	tmp_device.file_id = 0;
	cbd = CYANFS_RB_NFIND(cyanfs_block_device_rb, &cyanfs_block_device_rb, &tmp_device);
	if (cbd && cbd->backend == backend)
		return -EBUSY;

	CYANFS_RB_REMOVE(cyanfs_backend_rb, &cyanfs_backend_rb, backend);
	cyanfs_backend_put(backend);
	return 0;
}

static int cyanfs_ctl_backend_list(struct file *file, cyanfs_ioctl_backend_list_t *v)
{
	struct cyanfs_backend tmp = { .dev_id = MKDEV(v->dev.major, v->dev.minor + 1) };
	struct cyanfs_backend *backend;
	backend = CYANFS_RB_NFIND(cyanfs_backend_rb, &cyanfs_backend_rb, &tmp);
	if (!backend)
		return -ENODEV;
	v->dev.major = MAJOR(backend->dev_id);
	v->dev.minor = MINOR(backend->dev_id);
	return 0;
}

static int cyanfs_ctl_backend_statfs(struct file *file, cyanfs_ioctl_backend_statfs_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_statfs(backend->super, &v->meta);
}

static int cyanfs_ctl_backend_sync(struct file *file, cyanfs_ioctl_backend_sync_t *v)
{
	int r;
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	r = cyanfs_super_flush(backend->super, true);
	if (r < 0)
		return r;
	flush_work(&backend->work);
	return cyanfs_super_status(backend->super);
}

static int cyanfs_ctl_device_map(struct file *file, cyanfs_ioctl_device_map_t *v)
{
	struct cyanfs_backend *backend;
	struct cyanfs_block_device *cbd;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	cbd = cyanfs_block_device_alloc(backend, v->id, !v->readonly);
	if (IS_ERR(cbd))
		return PTR_ERR(cbd);
	CYANFS_RB_INSERT(cyanfs_block_device_rb, &cyanfs_block_device_rb, cbd);
	v->vdisk = cbd->id;
	return 0;
}

static int cyanfs_ctl_device_unmap(struct file *file, cyanfs_ioctl_device_unmap_t *v)
{
	struct cyanfs_block_device *cbd;
	struct cyanfs_backend tmp_backend = { .dev_id = MKDEV(v->dev.major, v->dev.minor) };
	struct cyanfs_block_device tmp_device = { .backend = &tmp_backend, .file_id = v->id };
	cbd = CYANFS_RB_FIND(cyanfs_block_device_rb, &cyanfs_block_device_rb, &tmp_device);
	if (!cbd)
		return -ENODEV;
	CYANFS_RB_REMOVE(cyanfs_block_device_rb, &cyanfs_block_device_rb, cbd);
	cyanfs_block_device_put(cbd);
	return 0;
}

static int cyanfs_ctl_device_list(struct file *file, cyanfs_ioctl_device_list_t *v)
{
	struct cyanfs_block_device *cbd;
	struct cyanfs_backend tmp_backend = { .dev_id = MKDEV(v->dev.major, v->dev.minor) };
	struct cyanfs_block_device tmp_device = { .backend = &tmp_backend, .file_id = v->id + 1 };
	cbd = CYANFS_RB_NFIND(cyanfs_block_device_rb, &cyanfs_block_device_rb, &tmp_device);
	if (!cbd)
		return -ENODEV;
	v->dev.major = MAJOR(cbd->backend->dev_id);
	v->dev.minor = MINOR(cbd->backend->dev_id);
	v->id = cbd->file_id;
	v->vdisk = cbd->id;
	return 0;
}

static int cyanfs_ctl_file_list(struct file *file, cyanfs_ioctl_file_list_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_list(backend->super, &v->meta);
}

static int cyanfs_ctl_file_lookup(struct file *file, cyanfs_ioctl_file_lookup_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_lookup(backend->super, v->name, &v->meta);
}

static int cyanfs_ctl_file_create(struct file *file, cyanfs_ioctl_file_create_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_create(backend->super, v->name, &v->id);
}

static int cyanfs_ctl_file_fork(struct file *file, cyanfs_ioctl_file_fork_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_fork(backend->super, v->pid, v->name, &v->id);
}

static int cyanfs_ctl_file_rename(struct file *file, cyanfs_ioctl_file_rename_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_rename(backend->super, v->id, v->name);
}

static int cyanfs_ctl_file_truncate(struct file *file, cyanfs_ioctl_file_truncate_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_truncate(backend->super, v->id, v->size);
}

static int cyanfs_ctl_file_delete(struct file *file, cyanfs_ioctl_file_delete_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_delete(backend->super, v->id);
}

static int cyanfs_ctl_file_stat(struct file *file, cyanfs_ioctl_file_stat_t *v)
{
	struct cyanfs_backend *backend;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	return cyanfs_stat(backend->super, v->id, &v->meta);
}

static void __complete(void *c)
{
	complete(c);
}

struct cyanfs_ctl_ctx {
	struct cyanfs_file *file;
	struct cyanfs_backend *backend;
};

static int cyanfs_ctl_flush(struct cyanfs_ctl_ctx *ctx)
{
	cyanfs_map_type_t map;
	int r;

	if (!ctx->file || !ctx->backend)
		return 0;

	r = cyanfs_flush(ctx->file, &map);
	if (r < 0)
		return r;

	if (map == CYANFS_MAP_NOP) {
		return cyanfs_file_status(ctx->file);
	} else if (map == CYANFS_MAP_REQUEUE) {
		DECLARE_COMPLETION_ONSTACK(waiter);
		r = cyanfs_requeue(ctx->file, &waiter, __complete);
		if (r < 0)
			return r;
		wait_for_completion_io(&waiter);
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
	r = blkdev_issue_flush(ctx->backend->dev, GFP_KERNEL, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
	r = blkdev_issue_flush(ctx->backend->dev, GFP_KERNEL);
#else
	r = blkdev_issue_flush(ctx->backend->dev);
#endif
	if (r < 0)
		return r;
	return cyanfs_file_status(ctx->file);
}

static int cyanfs_ctl_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	return cyanfs_ctl_flush(file->private_data);
}

static int cyanfs_ctl_open(struct inode *inode, struct file *file)
{
	struct cyanfs_ctl_ctx *ctx;
	ctx = kzalloc(sizeof(struct cyanfs_ctl_ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	file->private_data = ctx;
	return generic_file_open(inode, file);
}

static int cyanfs_ctl_release(struct inode *inode, struct file *file)
{
	struct cyanfs_ctl_ctx *ctx = file->private_data;
	if (ctx->backend) {
		cyanfs_ctl_flush(ctx);
		cyanfs_close(ctx->file);
		cyanfs_backend_put(ctx->backend);
	}
	kfree(ctx);
	return 0;
}

static loff_t cyanfs_ctl_llseek(struct file *file, loff_t offset, int whence)
{
	int r;
	uint64_t off = offset;
	struct cyanfs_ctl_ctx *ctx = file->private_data;
	if (!ctx->file)
		return noop_llseek(file, offset, whence);
	if (whence != SEEK_DATA)
		return fixed_size_llseek(file, offset, whence, cyanfs_size(ctx->file));
	if (offset < 0)
		return -EINVAL;
	r = cyanfs_seek_extent(ctx->file, &off);
	if (r < 0) {
		if (r == -CYANFS_ERR_NOENT)
			return -ENXIO;
		return r;
	}
	return off;
}

#ifndef untagged_addr
#define untagged_addr(addr) (addr)
#endif

static void cyanfs_ctl_end_io(struct bio *bio)
{
	struct completion *waiter = bio->bi_private;
	complete(waiter);
}

static ssize_t cyanfs_ctl_io(struct cyanfs_ctl_ctx *ctx, bool is_write, char __user *buf, size_t len, loff_t *ppos)
{
	struct cyanfs_file *file = ctx->file;
	struct cyanfs_backend *backend = ctx->backend;
	unsigned long page_offset, pages_num, pages_got;
	struct bio *bio;
	struct page **pages;
	struct completion waiter;
	int i;
	ssize_t r;

	if (!file || !backend)
		return -EINVAL;
	if (!IS_ALIGNED(*ppos, SECTOR_SIZE) || !IS_ALIGNED(len, SECTOR_SIZE) || len > CYANFS_BIO_MAX_SIZE)
		return -EINVAL;

	if (*ppos + len > cyanfs_size(file)) {
		if (*ppos >= cyanfs_size(file))
			return is_write ? -ENOSPC : 0;
		len = cyanfs_size(file) - *ppos;
	}

	if (!len)
		return 0;

	page_offset = offset_in_page(buf);
	pages_num = DIV_ROUND_UP(page_offset + len, PAGE_SIZE);

	pages = kmalloc_array(pages_num, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
	pages_got =
		get_user_pages(untagged_addr((unsigned long)buf), pages_num, is_write ? FOLL_WRITE : 0, pages, NULL);
#else
	pages_got = get_user_pages(untagged_addr((unsigned long)buf), pages_num, is_write ? FOLL_WRITE : 0, pages);
#endif
	if (pages_got < 0) {
		r = -EFAULT;
		goto array_out;
	}
	if (pages_got < pages_num) {
		r = -EFAULT;
		goto pages_out;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	bio = bio_alloc(GFP_KERNEL, pages_num);
	if (bio) {
		bio->bi_opf = is_write ? REQ_OP_WRITE : REQ_OP_READ;
		bio_set_dev(bio, ctx->backend->dev);
	}
#else
	bio = bio_alloc(ctx->backend->dev, pages_num, is_write ? REQ_OP_WRITE : REQ_OP_READ, GFP_KERNEL);
#endif
	if (!bio) {
		r = -ENOMEM;
		goto pages_out;
	}

	r = len;
	bio->bi_iter.bi_sector = (*ppos) >> SECTOR_SHIFT;
	for (i = 0; i < pages_num; ++i) {
		unsigned long l = min(PAGE_SIZE - page_offset, len);
		if (!bio_add_page(bio, pages[i], l, page_offset))
			BUG();
		page_offset = 0;
		len -= l;
	}
	BUG_ON(len);

	init_completion(&waiter);
	bio->bi_private = &waiter;
	bio->bi_end_io = cyanfs_ctl_end_io;
	cyanfs_file_submit_bio(ctx->file, ctx->backend, bio);
	wait_for_completion_io(&waiter);

	if (bio->bi_status)
		r = blk_status_to_errno(bio->bi_status);
	else
		*ppos += r;
	bio_put(bio);

pages_out:
	for (i = 0; i < pages_got; ++i) {
		struct page *page = pages[i];
		if (is_write) {
			if (!PageReserved(page))
				SetPageDirty(page);
		}
		put_page(page);
	}
array_out:
	kfree(pages);
	return r;
}

static ssize_t cyanfs_ctl_read(struct file *file, char __user *buf, size_t len, loff_t *ppos)
{
	return cyanfs_ctl_io(file->private_data, false, buf, len, ppos);
}

static ssize_t cyanfs_ctl_write(struct file *file, const char __user *buf, size_t len, loff_t *ppos)
{
	return cyanfs_ctl_io(file->private_data, true, (char __user *)buf, len, ppos);
}

static int cyanfs_ctl_file_bind(struct file *file, cyanfs_ioctl_file_bind_t *v)
{
	struct cyanfs_ctl_ctx *ctx = file->private_data;
	struct cyanfs_backend *backend;
	struct cyanfs_file *f;
	int r;
	if (ctx->backend)
		return -EBUSY;
	backend = cyanfs_ctl_backend_lookup(&v->dev);
	if (!backend)
		return -ENODEV;
	r = cyanfs_open(backend->super, v->id, !v->readonly, &f);
	if (r < 0)
		return r;
	cyanfs_backend_get(backend);
	ctx->backend = backend;
	ctx->file = f;
	return 0;
}

static long cyanfs_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int r;
	switch (cmd) {
#define CYANFS_IOCTL(cmd, name, output)                                                                                \
	case (cmd): {                                                                                                  \
		cyanfs_ioctl_##name##_t v;                                                                             \
		if (sizeof(v) && copy_from_user(&v, (const void *)arg, sizeof(v)))                                     \
			return -EFAULT;                                                                                \
		r = cyanfs_ctl_##name(file, &v);                                                                       \
		if (r < 0)                                                                                             \
			return r;                                                                                      \
		if ((output) && sizeof(v) && copy_to_user((void *)arg, &v, sizeof(v)))                                 \
			return -EFAULT;                                                                                \
		return 0;                                                                                              \
	}
		CYANFS_IOCTL(CYANFS_IOCTL_BACKEND_MOUNT, backend_mount, false)
		CYANFS_IOCTL(CYANFS_IOCTL_BACKEND_UMOUNT, backend_umount, false)
		CYANFS_IOCTL(CYANFS_IOCTL_BACKEND_LIST, backend_list, true)
		CYANFS_IOCTL(CYANFS_IOCTL_BACKEND_STATFS, backend_statfs, true)
		CYANFS_IOCTL(CYANFS_IOCTL_BACKEND_SYNC, backend_sync, false)
		CYANFS_IOCTL(CYANFS_IOCTL_DEVICE_MAP, device_map, true)
		CYANFS_IOCTL(CYANFS_IOCTL_DEVICE_UNMAP, device_unmap, false)
		CYANFS_IOCTL(CYANFS_IOCTL_DEVICE_LIST, device_list, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_LIST, file_list, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_LOOKUP, file_lookup, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_CREATE, file_create, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_FORK, file_fork, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_RENAME, file_rename, false)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_TRUNCATE, file_truncate, false)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_DELETE, file_delete, false)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_STAT, file_stat, true)
		CYANFS_IOCTL(CYANFS_IOCTL_FILE_BIND, file_bind, false)
	default:
		return -ENOSYS;
	}
}

static long cyanfs_ctl_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long r;
	mutex_lock(&cyanfs_ctl_lock);
	r = cyanfs_ctl_ioctl(file, cmd, arg);
	mutex_unlock(&cyanfs_ctl_lock);
	return r;
}

static const struct file_operations cyanfs_ctl_fops = {
	.owner = THIS_MODULE,
	.open = cyanfs_ctl_open,
	.release = cyanfs_ctl_release,
	.unlocked_ioctl = cyanfs_ctl_unlocked_ioctl,
	.llseek = cyanfs_ctl_llseek,
	.read = cyanfs_ctl_read,
	.write = cyanfs_ctl_write,
	.fsync = cyanfs_ctl_fsync,
};

static struct miscdevice cyanfs_ctl_miscdevice = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = CYANFS_IOCTL_DEVNAME,
	.fops = &cyanfs_ctl_fops,
};

int cyanfs_ctl_register(void)
{
	CYANFS_RB_INIT(&cyanfs_backend_rb);
	CYANFS_RB_INIT(&cyanfs_block_device_rb);
	return misc_register(&cyanfs_ctl_miscdevice);
}

void cyanfs_ctl_unregister(void)
{
	misc_deregister(&cyanfs_ctl_miscdevice);
}
