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
#ifndef __CYANFS_KERNEL_HEADER__
#define __CYANFS_KERNEL_HEADER__

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/configfs.h>
#include <linux/workqueue.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>

#include "../core/core.h"
#include "include/cyanfs.h"

#ifndef OPENEULER_MAJOR
#define OPENEULER_MAJOR 0
#endif

extern int cyanfs_major;

struct cyanfs_backend;

struct cyanfs_block_device {
	int id;
	struct kref ref;
	struct cyanfs_backend *backend;
	cyanfs_file_id_t file_id;
	struct cyanfs_file *file;
	struct gendisk *disk;
	struct work_struct work;

	// 外部初始化
	CYANFS_RB_ENTRY(cyanfs_block_device) node;
#ifdef CYANFS_DEBUG_ENABLED
	struct config_group group;
#endif
};

extern struct cyanfs_block_device *cyanfs_block_device_alloc(struct cyanfs_backend *backend, cyanfs_file_id_t id,
							     int write);
extern void cyanfs_block_device_get(struct cyanfs_block_device *cbd);
extern void cyanfs_block_device_put(struct cyanfs_block_device *cbd);

#ifndef BIO_MAX_VECS
#define BIO_MAX_VECS BIO_MAX_PAGES
#endif

#define CYANFS_BIO_MAX_VECS BIO_MAX_VECS
#define CYANFS_BIO_MAX_SIZE (CYANFS_BIO_MAX_VECS << PAGE_SHIFT)

struct cyanfs_backend {
	struct kref ref;
	struct cyanfs_super *super;
	dev_t dev_id;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 9, 0)
	struct file *dev_file;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct bdev_handle *dev_handle;
#endif
	struct block_device *dev;
	struct work_struct work;
	spinlock_t sync_waiters_lock;
	struct list_head sync_waiters;
	struct bio_vec bvecs[CYANFS_BIO_MAX_VECS];
	struct delayed_work flush_work;

	// 外部初始化
	CYANFS_RB_ENTRY(cyanfs_backend) node;
#ifdef CYANFS_DEBUG_ENABLED
	struct config_group group;
	struct config_group devices_ro_group;
	struct config_group devices_rw_group;
#endif
};

extern struct cyanfs_backend *cyanfs_backend_open(dev_t dev);
extern void cyanfs_backend_get(struct cyanfs_backend *backend);
extern void cyanfs_backend_put(struct cyanfs_backend *backend);
extern int cyanfs_backend_wait_tasks(struct cyanfs_backend *backend);
extern int cyanfs_backend_sync(struct cyanfs_backend *backend);

extern void cyanfs_file_submit_bio(struct cyanfs_file *file, struct cyanfs_backend *backend, struct bio *bio);

#ifdef CYANFS_DEBUG_ENABLED
extern int cyanfs_configfs_register(void);
extern void cyanfs_configfs_unregister(void);
#endif

extern int cyanfs_ctl_register(void);
extern void cyanfs_ctl_unregister(void);

#endif
