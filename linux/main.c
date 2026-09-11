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

#define CYANFS_BACKEND_PRINTK(level, backend, fmt, args...)                                                            \
	CYANFS_RAW_##level("(cyanfs %d:%d): " fmt, MAJOR((backend)->dev->bd_dev), MINOR((backend)->dev->bd_dev), ##args)
#define CYANFS_BACKEND_INFO(backend, fmt, args...) CYANFS_BACKEND_PRINTK(INFO, backend, fmt, ##args)
#define CYANFS_BACKEND_DEBUG(backend, fmt, args...) CYANFS_BACKEND_PRINTK(DEBUG, backend, fmt, ##args)

#ifdef CYANFS_DEBUG_ENABLED
module_param(debug_enable, int, 0644);
module_param(debug_dump_journal, int, 0644);
#endif

#ifndef GIT_COMMIT
#define GIT_COMMIT_STRING ""
#else
#define GIT_COMMIT_STRING __stringify(GIT_COMMIT)
#endif

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("CyanFS block device driver");
MODULE_VERSION(GIT_COMMIT_STRING);

#define CYANFS_FLUSH_INTERVAL (60 * HZ)

int cyanfs_major;
static struct workqueue_struct *cyanfs_workqueue;

static inline void cyanfs_bio_endio_status(struct bio *bio, blk_status_t status)
{
	bio->bi_status = status;
	bio_endio(bio);
}

static inline void cyanfs_bio_endio_error(struct bio *bio, int err)
{
	cyanfs_bio_endio_status(bio, errno_to_blk_status(err));
}

static void cyanfs_file_map_bio(struct bio *bio);
static void __cyanfs_file_map_bio(void *ctx)
{
	cyanfs_file_map_bio(ctx);
}

static int cyanfs_file_split_bio(cyanfs_map_type_t type, uint64_t b_off, uint64_t len, void *ctx)
{
	struct bio *bio = ctx;
	struct cyanfs_file *file = (struct cyanfs_file *)bio->bi_next;

	if (bio->bi_iter.bi_size > len) {
		struct bio *new_bio;
		new_bio = bio_split(bio, len >> SECTOR_SHIFT, GFP_NOFS, &fs_bio_set);
		BUG_ON(!new_bio);
		bio_chain(new_bio, bio);
		new_bio->bi_next = bio->bi_next;
		bio = new_bio;
	}

	switch (type) {
	case CYANFS_MAP_NOP:
		if (bio_op(bio) == REQ_OP_READ)
			zero_fill_bio(bio);
		bio_endio(bio);
		break;
	case CYANFS_MAP_SUBMIT:
		bio->bi_iter.bi_sector = b_off >> SECTOR_SHIFT;
		bio->bi_next = NULL;
		submit_bio(bio);
		break;
	case CYANFS_MAP_REQUEUE:
		return cyanfs_requeue(file, bio, __cyanfs_file_map_bio);
	default:
		BUG_ON(1);
	}

	return 0;
}

static void cyanfs_file_map_bio(struct bio *bio)
{
	struct cyanfs_file *file = (struct cyanfs_file *)bio->bi_next;
	int (*fn)(struct cyanfs_file * f, void *ctx, uint64_t f_off, uint64_t len, cyanfs_io_spliter handler);
	int err;

	if (bio_op(bio) == REQ_OP_FLUSH || bio->bi_iter.bi_size == 0) {
		bio->bi_next = NULL;
		submit_bio(bio);
		return;
	} else if (bio_op(bio) == REQ_OP_READ) {
		fn = cyanfs_read;
	} else if (bio_op(bio) == REQ_OP_WRITE) {
		fn = cyanfs_write;
	} else {
		fn = cyanfs_discard;
	}

	err = fn(file, bio, bio->bi_iter.bi_sector << SECTOR_SHIFT, bio->bi_iter.bi_size, cyanfs_file_split_bio);
	if (err)
		cyanfs_bio_endio_error(bio, err);
}

void cyanfs_file_submit_bio(struct cyanfs_file *file, struct cyanfs_backend *backend, struct bio *bio)
{
	cyanfs_map_type_t map;
	int err = 0;

	switch (bio_op(bio)) {
	case REQ_OP_READ:
	case REQ_OP_WRITE:
	case REQ_OP_DISCARD:
	case REQ_OP_FLUSH:
		break;
	default:
		cyanfs_bio_endio_status(bio, BLK_STS_NOTSUPP);
		return;
	}

	if (unlikely(!IS_ALIGNED(bio->bi_iter.bi_size, SECTOR_SIZE))) {
		cyanfs_bio_endio_status(bio, BLK_STS_IOERR);
		return;
	}

	BUG_ON(bio->bi_next);
	bio->bi_next = (struct bio *)file;
	bio_set_dev(bio, backend->dev);

	if ((bio_op(bio) == REQ_OP_FLUSH) || (bio->bi_opf & REQ_PREFLUSH)) {
		err = cyanfs_flush(file, &map);
		if (err < 0) {
			cyanfs_bio_endio_error(bio, err);
			return;
		}

		if (map == CYANFS_MAP_REQUEUE) {
			err = cyanfs_requeue(file, bio, __cyanfs_file_map_bio);
			if (err)
				cyanfs_bio_endio_error(bio, err);
			return;
		} else if (map == CYANFS_MAP_NOP) {
			if (bio_op(bio) == REQ_OP_FLUSH || bio->bi_iter.bi_size == 0) {
				bio_endio(bio);
				return;
			}
		}
	}

	cyanfs_file_map_bio(bio);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
typedef unsigned int blk_opf_t;
#endif

static int cyanfs_backend_io_vect(struct cyanfs_backend *backend, blk_opf_t opf, struct bio_vec *vec, int vecs,
				  uint64_t b_off, uint64_t size)
{
	int err;
	struct bio bio;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 18, 0)
	bio_init(&bio, vec, vecs);
	bio_set_dev(&bio, backend->dev);
	bio.bi_opf = opf;
#else
	bio_init(&bio, backend->dev, vec, vecs, opf);
#endif
	bio.bi_vcnt = vecs;
	bio.bi_iter.bi_bvec_done = 0;
	bio.bi_iter.bi_size = size;
	bio.bi_iter.bi_sector = b_off >> SECTOR_SHIFT;
	err = submit_bio_wait(&bio);
	bio_uninit(&bio);

	return err;
}

static int cyanfs_backend_io_buf(struct cyanfs_backend *backend, blk_opf_t opf, void *buf, uint64_t b_off,
				 uint64_t size)
{
	struct bio_vec vec;
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
	vec.bv_page = virt_to_page(buf);
	vec.bv_len = size;
	vec.bv_offset = offset_in_page(buf);
#else
	bvec_set_page(&vec, virt_to_page(buf), size, offset_in_page(buf));
#endif
	return cyanfs_backend_io_vect(backend, opf, &vec, 1, b_off, size);
}

static int cyanfs_backend_copy(struct cyanfs_backend *backend, uint64_t b_off_src, uint64_t b_off_dst, uint64_t len)
{
	int err;
	uint64_t l;

	while (len) {
		l = min_t(uint64_t, len, CYANFS_BIO_MAX_SIZE);

		err = cyanfs_backend_io_vect(backend, REQ_OP_READ, backend->bvecs, CYANFS_BIO_MAX_VECS, b_off_src, l);
		if (err)
			return err;
		err = cyanfs_backend_io_vect(backend, REQ_OP_WRITE, backend->bvecs, CYANFS_BIO_MAX_VECS, b_off_dst, l);
		if (err)
			return err;

		b_off_src += l;
		b_off_dst += l;
		len -= l;
	}

	return 0;
}

static void cyanfs_backend_loop(struct cyanfs_backend *backend)
{
	struct cyanfs_task *t;

	while ((t = cyanfs_super_get_task(backend->super)) != NULL) {
		int err = 0;
		switch (t->type) {
		case CYANFS_TASK_NOP:
			break;
		case CYANFS_TASK_READ:
			err = cyanfs_backend_io_buf(backend, REQ_OP_READ, t->read.buf, t->read.b_off, t->read.len);
			break;
		case CYANFS_TASK_WRITE:
			err = cyanfs_backend_io_buf(backend, REQ_OP_WRITE, t->write.buf, t->write.b_off, t->write.len);
			break;
		case CYANFS_TASK_COPY:
			err = cyanfs_backend_copy(backend, t->copy.b_src_off, t->copy.b_dst_off, t->copy.len);
			break;
		case CYANFS_TASK_DISCARD:
			CYANFS_BACKEND_DEBUG(backend, "discard b_off: %llu len: %llu", t->discard.b_off,
					     t->discard.len);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
			err = blkdev_issue_discard(backend->dev, t->discard.b_off >> SECTOR_SHIFT,
						   t->discard.len >> SECTOR_SHIFT, GFP_NOIO, 0);
#else
			err = blkdev_issue_discard(backend->dev, t->discard.b_off >> SECTOR_SHIFT,
						   t->discard.len >> SECTOR_SHIFT, GFP_NOIO);
#endif
			break;
		case CYANFS_TASK_FLUSH:
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 8, 0)
			err = blkdev_issue_flush(backend->dev, GFP_NOIO, NULL);
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 12, 0)
			err = blkdev_issue_flush(backend->dev, GFP_NOIO);
#else
			err = blkdev_issue_flush(backend->dev);
#endif
			break;
		case CYANFS_TASK_REQUEUE:
			t->requeue.fn(t->requeue.ctx);
			break;
		default:
			BUG_ON(1);
		}
		t->done(t, err);
	}
}

static void cyanfs_backend_work(struct work_struct *work)
{
	struct cyanfs_backend *backend = container_of(work, struct cyanfs_backend, work);
	struct blk_plug plug;
	blk_start_plug(&plug);
	cyanfs_backend_loop(backend);
	blk_finish_plug(&plug);
}

static void cyanfs_backend_flush_work(struct work_struct *work)
{
	struct cyanfs_backend *backend = container_of(work, struct cyanfs_backend, flush_work.work);
	cyanfs_super_flush(backend->super, false);
	queue_delayed_work(cyanfs_workqueue, &backend->flush_work, CYANFS_FLUSH_INTERVAL);
}

static void cyanfs_new_task(void *ctx)
{
	struct cyanfs_backend *backend = ctx;
	queue_work(cyanfs_workqueue, &backend->work);
}

struct cyanfs_backend *cyanfs_backend_open(dev_t dev)
{
	int error, i;
	struct cyanfs_backend *backend;

	backend = kzalloc(sizeof(struct cyanfs_backend), GFP_KERNEL);
	if (!backend) {
		error = -ENOMEM;
		goto out;
	}
	kref_init(&backend->ref);
	backend->dev_id = dev;

	for (i = 0; i < CYANFS_BIO_MAX_VECS; i++) {
		backend->bvecs[i].bv_page = alloc_page(GFP_KERNEL);
		if (!backend->bvecs[i].bv_page) {
			error = -ENOMEM;
			goto backend_out;
		}
		backend->bvecs[i].bv_len = PAGE_SIZE;
		backend->bvecs[i].bv_offset = 0;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	backend->dev_file = bdev_file_open_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, backend, NULL);
	if (IS_ERR(backend->dev_file)) {
		error = PTR_ERR(backend->dev_file);
		goto backend_out;
	}
	backend->dev = file_bdev(backend->dev_file);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	backend->dev_handle = bdev_open_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, backend, NULL);
	if (IS_ERR(backend->dev_handle)) {
		error = PTR_ERR(backend->dev_handle);
		goto backend_out;
	}
	backend->dev = backend->dev_handle->bdev;
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
	backend->dev = blkdev_get_by_dev(dev, BLK_OPEN_READ | BLK_OPEN_WRITE | BLK_OPEN_EXCL, backend, NULL);
#else
	backend->dev = blkdev_get_by_dev(dev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, backend);
#endif
	if (IS_ERR(backend->dev)) {
		error = PTR_ERR(backend->dev);
		goto backend_out;
	}
#endif

	BUILD_BUG_ON(SECTOR_SIZE != 512);
	if (queue_logical_block_size(bdev_get_queue(backend->dev)) != SECTOR_SIZE ||
	    bdev_physical_block_size(backend->dev) > CYANFS_FILE_ALIGN_SIZE) {
		error = -EMEDIUMTYPE;
		goto dev_out;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
	CYANFS_BACKEND_DEBUG(backend, "write cache: %d", blk_queue_write_cache(bdev_get_queue(backend->dev)));
#else
	CYANFS_BACKEND_DEBUG(backend, "write cache: %d",
			     test_bit(QUEUE_FLAG_WC, &bdev_get_queue(backend->dev)->queue_flags));
#endif
	CYANFS_BACKEND_DEBUG(backend, "discard: %u", bdev_get_queue(backend->dev)->limits.max_discard_sectors);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	backend->super = cyanfs_super_open(bdev_nr_bytes(backend->dev),
#else
	backend->super = cyanfs_super_open(i_size_read(backend->dev->bd_inode),
#endif
					   bdev_get_queue(backend->dev)->limits.max_discard_sectors, true);
	if (!backend->super) {
		error = -ENOMEM;
		goto dev_out;
	}

	cyanfs_backend_loop(backend);

	if (!cyanfs_super_is_ready(backend->super)) {
		CYANFS_BACKEND_INFO(backend, "journal corruption");
		error = -EIO;
		goto super_out;
	}

	INIT_WORK(&backend->work, cyanfs_backend_work);
	cyanfs_super_set_new_task_callback(backend->super, backend, cyanfs_new_task);
	INIT_DELAYED_WORK(&backend->flush_work, cyanfs_backend_flush_work);
	queue_delayed_work(cyanfs_workqueue, &backend->flush_work, CYANFS_FLUSH_INTERVAL);

	__module_get(THIS_MODULE);
	CYANFS_BACKEND_DEBUG(backend, "open");
	return backend;

super_out:
	cyanfs_super_close(backend->super);
dev_out:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	bdev_fput(backend->dev_file);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	bdev_release(backend->dev_handle);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
	blkdev_put(backend->dev, backend);
#else
	blkdev_put(backend->dev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
#endif
backend_out:
	for (i = 0; i < CYANFS_BIO_MAX_VECS; ++i) {
		if (backend->bvecs[i].bv_page)
			put_page(backend->bvecs[i].bv_page);
	}
	kfree(backend);
out:
	return ERR_PTR(error);
}

static void cyanfs_backend_close(struct kref *ref)
{
	int i;
	struct cyanfs_backend *backend = container_of(ref, struct cyanfs_backend, ref);

	CYANFS_BACKEND_DEBUG(backend, "closing");
	cancel_delayed_work_sync(&backend->flush_work);
	cyanfs_super_flush(backend->super, true);
	queue_work(cyanfs_workqueue, &backend->work);
	flush_work(&backend->work);
	cyanfs_super_close(backend->super);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	bdev_fput(backend->dev_file);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	bdev_release(backend->dev_handle);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0)
	blkdev_put(backend->dev, backend);
#else
	blkdev_put(backend->dev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
#endif
	CYANFS_BACKEND_DEBUG(backend, "closed");
	for (i = 0; i < CYANFS_BIO_MAX_VECS; ++i) {
		put_page(backend->bvecs[i].bv_page);
	}
	kfree(backend);
	module_put(THIS_MODULE);
}

void cyanfs_backend_get(struct cyanfs_backend *backend)
{
	kref_get(&backend->ref);
}

void cyanfs_backend_put(struct cyanfs_backend *backend)
{
	kref_put(&backend->ref, cyanfs_backend_close);
}

static int __init cyanfs_init(void)
{
	int error;

	BUILD_BUG_ON(sizeof(GIT_COMMIT_STRING) <= 1);
	CYANFS_INFO("driver commit: " GIT_COMMIT_STRING);
	CYANFS_DEBUG("debug on");

	cyanfs_major = register_blkdev(0, "cbd");
	if (cyanfs_major <= 0) {
		error = -EBUSY;
		goto out;
	}

	cyanfs_workqueue = alloc_workqueue("cyanfs", WQ_UNBOUND | WQ_MEM_RECLAIM, WQ_MAX_ACTIVE);
	if (!cyanfs_workqueue) {
		error = -ENOMEM;
		goto blk_out;
	}

	error = cyanfs_ctl_register();
	if (error < 0)
		goto workqueue_out;

#ifdef CYANFS_DEBUG_ENABLED
	error = cyanfs_configfs_register();
#endif
	if (error < 0)
		goto misc_out;

	return 0;

misc_out:
	cyanfs_ctl_unregister();
workqueue_out:
	destroy_workqueue(cyanfs_workqueue);
blk_out:
	unregister_blkdev(cyanfs_major, "cbd");
out:
	return error;
}

static void __exit cyanfs_exit(void)
{
#ifdef CYANFS_DEBUG_ENABLED
	cyanfs_configfs_unregister();
	WARN_ON(cyanfs_atomic_read(&debug_malloc_counter));
#endif
	cyanfs_ctl_unregister();
	destroy_workqueue(cyanfs_workqueue);
	unregister_blkdev(cyanfs_major, "cbd");
}

module_init(cyanfs_init);
module_exit(cyanfs_exit);
