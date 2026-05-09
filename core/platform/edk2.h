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
#ifndef __CYANFS_EDK2_HEADER__
#define __CYANFS_EDK2_HEADER__

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/SynchronizationLib.h>

typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINT64 uint64_t;

// C format macro constants
// Defiend in header <inttypes.h>
#define PRIu32 "u"
#define PRIu64 "llu"

#define CYANFS_PRINT_STRING_TYPE "a"

#define CYANFS_CALL_PRINTK(level, fmt, args...)                                                                        \
	do {                                                                                                           \
		AsciiPrint(fmt "\n", ##args);                                                                          \
	} while (0)

#define CYANFS_BUG_ON(cond)                                                                                            \
	do {                                                                                                           \
		if (cond) {                                                                                            \
			AsciiPrint("CYANFS_BUG_ON: %a:%d\n", __func__, __LINE__);                                      \
			*((uint8_t *)0) = 0;                                                                           \
		}                                                                                                      \
	} while (0)

#define cyanfs_malloc(size) AllocatePool((size))
#define cyanfs_free(p) FreePool((p))
#define cyanfs_memcpy(dst, src, size) CopyMem((dst), (src), (size))
#define cyanfs_memcmp(a, b, size) CompareMem((a), (b), (size))

// EDK2 只支持小端序
#define cyanfs_htole64(a) (a)
#define cyanfs_htole32(a) (a)
#define cyanfs_htole16(a) (a)
#define cyanfs_le64toh(a) (a)
#define cyanfs_le32toh(a) (a)
#define cyanfs_le16toh(a) (a)

struct cyanfs_rwlock {
	EFI_TPL tpl;
};

static inline void cyanfs_init_rwlock(struct cyanfs_rwlock *lock)
{
}

static inline void cyanfs_read_lock(struct cyanfs_rwlock *lock)
{
	lock->tpl = gBS->RaiseTPL(TPL_HIGH_LEVEL - 1);
}

static inline void cyanfs_read_unlock(struct cyanfs_rwlock *lock)
{
	gBS->RestoreTPL(lock->tpl);
}

static inline void cyanfs_write_lock(struct cyanfs_rwlock *lock)
{
	lock->tpl = gBS->RaiseTPL(TPL_HIGH_LEVEL - 1);
}

static inline void cyanfs_write_unlock(struct cyanfs_rwlock *lock)
{
	gBS->RestoreTPL(lock->tpl);
}

struct cyanfs_lock {
	EFI_TPL tpl;
};

static inline void cyanfs_init_lock(struct cyanfs_lock *lock)
{
}

static inline void cyanfs_lock(struct cyanfs_lock *lock)
{
	lock->tpl = gBS->RaiseTPL(TPL_HIGH_LEVEL - 1);
}

static inline void cyanfs_unlock(struct cyanfs_lock *lock)
{
	gBS->RestoreTPL(lock->tpl);
}

typedef struct {
	UINT32 v;
} cyanfs_atomic_t;

static inline void cyanfs_atomic_init(cyanfs_atomic_t *v, int i)
{
	volatile UINT32 *p = &v->v;
	*p = i;
}

static inline int cyanfs_atomic_inc(cyanfs_atomic_t *v)
{
	return InterlockedIncrement(&v->v);
}

static inline int cyanfs_atomic_dec(cyanfs_atomic_t *v)
{
	return InterlockedDecrement(&v->v);
}

static inline int cyanfs_atomic_read(cyanfs_atomic_t *v)
{
	volatile UINT32 *p = &v->v;
	return *p;
}

typedef EFI_STATUS cyanfs_status;

#define CYANFS_ERR_NOMEM (-EFI_OUT_OF_RESOURCES)
#define CYANFS_ERR_INVAL (-EFI_INVALID_PARAMETER)
#define CYANFS_ERR_BUSY (-EFI_NOT_READY)
#define CYANFS_ERR_NOENT (-EFI_NOT_FOUND)
#define CYANFS_ERR_EXIST (-EFI_INVALID_PARAMETER)
#define CYANFS_ERR_NOSPACE (-EFI_VOLUME_FULL)
#define CYANFS_ERR_IO (-EFI_DEVICE_ERROR)

#endif
