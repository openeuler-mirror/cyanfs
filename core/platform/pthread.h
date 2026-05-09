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
#ifndef __CYANFS_PTHREAD_HEADER__
#define __CYANFS_PTHREAD_HEADER__

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <endian.h>
#include <inttypes.h>
#include <stdatomic.h>

#define CYANFS_CALL_PRINTK(level, fmt, args...)                                                                        \
	do {                                                                                                           \
		fprintf(stderr, fmt "\n", ##args);                                                                     \
	} while (0)

#define CYANFS_BUG_ON(cond)                                                                                            \
	do {                                                                                                           \
		if (cond)                                                                                              \
			*((uint8_t *)0) = 0;                                                                           \
	} while (0)

#define cyanfs_malloc(size) malloc((size))
#define cyanfs_free(p) free((p))
#define cyanfs_memcpy(dst, src, size) memcpy((dst), (src), (size))
#define cyanfs_memcmp(a, b, size) memcmp((a), (b), (size))

#define cyanfs_htole64(a) htole64(a)
#define cyanfs_htole32(a) htole32(a)
#define cyanfs_htole16(a) htole16(a)
#define cyanfs_le64toh(a) le64toh(a)
#define cyanfs_le32toh(a) le32toh(a)
#define cyanfs_le16toh(a) le16toh(a)

struct cyanfs_rwlock {
	pthread_rwlock_t l;
};

static inline void cyanfs_init_rwlock(struct cyanfs_rwlock *lock)
{
	pthread_rwlock_init(&lock->l, 0);
}

static inline void cyanfs_read_lock(struct cyanfs_rwlock *lock)
{
	pthread_rwlock_rdlock(&lock->l);
}

static inline void cyanfs_read_unlock(struct cyanfs_rwlock *lock)
{
	pthread_rwlock_unlock(&lock->l);
}

static inline void cyanfs_write_lock(struct cyanfs_rwlock *lock)
{
	pthread_rwlock_wrlock(&lock->l);
}

static inline void cyanfs_write_unlock(struct cyanfs_rwlock *lock)
{
	pthread_rwlock_unlock(&lock->l);
}

struct cyanfs_lock {
	pthread_mutex_t l;
};

static inline void cyanfs_init_lock(struct cyanfs_lock *lock)
{
	pthread_mutex_init(&lock->l, 0);
}

static inline void cyanfs_lock(struct cyanfs_lock *lock)
{
	pthread_mutex_lock(&lock->l);
}

static inline void cyanfs_unlock(struct cyanfs_lock *lock)
{
	pthread_mutex_unlock(&lock->l);
}

typedef struct {
	atomic_int v;
} cyanfs_atomic_t;

static inline void cyanfs_atomic_init(cyanfs_atomic_t *v, int i)
{
	atomic_store(&v->v, i);
}

static inline int cyanfs_atomic_inc(cyanfs_atomic_t *v)
{
	return atomic_fetch_add(&v->v, 1) + 1;
}

static inline int cyanfs_atomic_dec(cyanfs_atomic_t *v)
{
	return atomic_fetch_add(&v->v, -1) - 1;
}

static inline int cyanfs_atomic_read(cyanfs_atomic_t *v)
{
	return atomic_load(&v->v);
}

typedef int cyanfs_status;

#define CYANFS_ERR_NOMEM ENOMEM
#define CYANFS_ERR_INVAL EINVAL
#define CYANFS_ERR_BUSY EBUSY
#define CYANFS_ERR_NOENT ENOENT
#define CYANFS_ERR_EXIST EEXIST
#define CYANFS_ERR_NOSPACE ENOSPC
#define CYANFS_ERR_IO EIO

#endif
