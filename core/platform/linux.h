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
#ifndef __CYANFS_LINUX_HEADER__
#define __CYANFS_LINUX_HEADER__

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/crc32.h>

// C format macro constants
// Defiend in header <inttypes.h>
#define PRIu32 "u"
#define PRIu64 "llu"

#define __CYANFS_CALL_PRINTK(level, fmt, args...)                                                                      \
	do {                                                                                                           \
		printk(level fmt "\n", ##args);                                                                        \
	} while (0)
#define CYANFS_CALL_PRINTK(level, fmt, args...) __CYANFS_CALL_PRINTK(KERN_##level, fmt, ##args)

#define CYANFS_BUG_ON(cond) BUG_ON(cond)
#define CYANFS_BUILD_BUG_ON(cond) BUILD_BUG_ON(cond)

#define cyanfs_malloc(size) kmalloc((size), GFP_NOIO)
#define cyanfs_free(p) kfree((p))
#define cyanfs_memcpy(dst, src, size) memcpy((dst), (src), (size))
#define cyanfs_memcmp(a, b, size) memcmp((a), (b), (size))

#define cyanfs_htole64(a) cpu_to_le64(a)
#define cyanfs_htole32(a) cpu_to_le32(a)
#define cyanfs_htole16(a) cpu_to_le16(a)
#define cyanfs_le64toh(a) le64_to_cpu(a)
#define cyanfs_le32toh(a) le32_to_cpu(a)
#define cyanfs_le16toh(a) le16_to_cpu(a)

struct cyanfs_rwlock {
	rwlock_t l;
};

static inline void cyanfs_init_rwlock(struct cyanfs_rwlock *lock)
{
	rwlock_init(&lock->l);
}

static inline void cyanfs_read_lock(struct cyanfs_rwlock *lock)
{
	read_lock(&lock->l);
}

static inline void cyanfs_read_unlock(struct cyanfs_rwlock *lock)
{
	read_unlock(&lock->l);
}

static inline void cyanfs_write_lock(struct cyanfs_rwlock *lock)
{
	write_lock(&lock->l);
}

static inline void cyanfs_write_unlock(struct cyanfs_rwlock *lock)
{
	write_unlock(&lock->l);
}

struct cyanfs_lock {
	spinlock_t l;
};

static inline void cyanfs_init_lock(struct cyanfs_lock *lock)
{
	spin_lock_init(&lock->l);
}

static inline void cyanfs_lock(struct cyanfs_lock *lock)
{
	spin_lock(&lock->l);
}

static inline void cyanfs_unlock(struct cyanfs_lock *lock)
{
	spin_unlock(&lock->l);
}

typedef struct {
	atomic_t v;
} cyanfs_atomic_t;

static inline void cyanfs_atomic_init(cyanfs_atomic_t *v, int i)
{
	atomic_set(&v->v, i);
}

static inline int cyanfs_atomic_inc(cyanfs_atomic_t *v)
{
	return atomic_inc_return(&v->v);
}

static inline int cyanfs_atomic_dec(cyanfs_atomic_t *v)
{
	return atomic_dec_return(&v->v);
}

static inline int cyanfs_atomic_read(cyanfs_atomic_t *v)
{
	return atomic_read(&v->v);
}

typedef int cyanfs_status;

#define CYANFS_ERR_NOMEM ENOMEM
#define CYANFS_ERR_INVAL EINVAL
#define CYANFS_ERR_BUSY EBUSY
#define CYANFS_ERR_NOENT ENOENT
#define CYANFS_ERR_EXIST EEXIST
#define CYANFS_ERR_NOSPACE ENOSPC
#define CYANFS_ERR_IO EIO

#define cyanfs_crc32(crc, data, len) crc32_le(crc, data, len)

#endif
