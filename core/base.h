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
#ifndef __CYANFS_BASE_HEADER__
#define __CYANFS_BASE_HEADER__

#ifdef __KERNEL__
#include "platform/linux.h"
#elif CYANFS_PTHREAD
#include "platform/pthread.h"
#elif CYANFS_GLIBC
#include "platform/glibc.h"
#elif CYANFS_EDK2
#include "platform/edk2.h"
#else
#error 不支持的平台
#endif

#ifndef CYANFS_BUILD_BUG_ON
#define CYANFS_BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2 * !!(condition)]))
#endif

#ifdef CYANFS_DEBUG_ENABLED

extern cyanfs_atomic_t debug_malloc_counter;

static inline void *__cyanfs_malloc(int size)
{
	void *p = cyanfs_malloc(size);
	if (p)
		cyanfs_atomic_inc(&debug_malloc_counter);
	return p;
}
static inline void __cyanfs_free(void *p)
{
	cyanfs_free(p);
	if (p)
		cyanfs_atomic_dec(&debug_malloc_counter);
}

#undef cyanfs_malloc
#define cyanfs_malloc(size) __cyanfs_malloc(size)
#undef cyanfs_free
#define cyanfs_free(p) __cyanfs_free(p)

#endif

#ifdef CYANFS_DEBUG_ENABLED
#define CYANFS_DEBUG_BUG_ON(cond) CYANFS_BUG_ON(cond)
#else
#define CYANFS_DEBUG_BUG_ON(cond)                                                                                      \
	do {                                                                                                           \
	} while (0)
#endif

#ifndef CYANFS_PRINT_STRING_TYPE
#define CYANFS_PRINT_STRING_TYPE "s"
#endif

#define CYANFS_RAW_INFO(fmt, args...) CYANFS_CALL_PRINTK(INFO, fmt, ##args)
#ifdef CYANFS_DEBUG_ENABLED
extern int debug_enable;
extern int debug_dump_journal;
#define CYANFS_RAW_DEBUG(fmt, args...)                                                                                 \
	do {                                                                                                           \
		if (debug_enable)                                                                                      \
			CYANFS_CALL_PRINTK(DEBUG, "%" CYANFS_PRINT_STRING_TYPE ":%d " fmt, __func__, __LINE__,         \
					   ##args);                                                                    \
	} while (0)

#else
#define CYANFS_RAW_DEBUG(fmt, args...)                                                                                 \
	do {                                                                                                           \
	} while (0)
#endif

#define CYANFS_PRINTK(level, fmt, args...) CYANFS_RAW_##level("(cyanfs): " fmt, ##args)
#define CYANFS_INFO(fmt, args...) CYANFS_PRINTK(INFO, fmt, ##args)
#define CYANFS_DEBUG(fmt, args...) CYANFS_PRINTK(DEBUG, fmt, ##args)

#include "misc/rb_tree.h"
#include "misc/list.h"

#ifndef cyanfs_crc32
static inline uint32_t cyanfs_crc32_simple(uint32_t crc, void *data, int len)
{
	int i;
	uint8_t *p = data;
	while (len--) {
		crc = crc ^ *p++;
		for (i = 7; i >= 0; i--) {
			crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
		}
	}
	return crc;
}
#define cyanfs_crc32(crc, data, len) cyanfs_crc32_simple(crc, data, len)
#endif

static inline void cyanfs_put_generic(uint8_t **p, void *s, int size)
{
	cyanfs_memcpy(*p, s, size);
	*p += size;
}

static inline void cyanfs_get_generic(uint8_t **p, void *s, int size)
{
	cyanfs_memcpy(s, *p, size);
	*p += size;
}

static inline void cyanfs_put64(uint8_t **p, uint64_t v)
{
	v = cyanfs_htole64(v);
	cyanfs_put_generic(p, &v, sizeof(v));
}

static inline uint64_t cyanfs_get64(uint8_t **p)
{
	uint64_t v;
	cyanfs_get_generic(p, &v, sizeof(v));
	return cyanfs_le64toh(v);
}

static inline void cyanfs_put32(uint8_t **p, uint32_t v)
{
	v = cyanfs_htole32(v);
	cyanfs_put_generic(p, &v, sizeof(v));
}

static inline uint32_t cyanfs_get32(uint8_t **p)
{
	uint32_t v;
	cyanfs_get_generic(p, &v, sizeof(v));
	return cyanfs_le32toh(v);
}

static inline void cyanfs_put16(uint8_t **p, uint16_t v)
{
	v = cyanfs_htole16(v);
	cyanfs_put_generic(p, &v, sizeof(v));
}

static inline uint16_t cyanfs_get16(uint8_t **p)
{
	uint16_t v;
	cyanfs_get_generic(p, &v, sizeof(v));
	return cyanfs_le16toh(v);
}

static inline void cyanfs_put8(uint8_t **p, uint8_t v)
{
	**p = v;
	*p += sizeof(v);
}

static inline uint8_t cyanfs_get8(uint8_t **p)
{
	uint8_t v = **p;
	*p += sizeof(v);
	return v;
}

#endif
