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
#ifndef __CYANFS_INTERNAL_HEADER__
#define __CYANFS_INTERNAL_HEADER__

#include "core.h"
#include "journal.h"

#define CYANFS_EXTENT_CONTINUOUS 16
#define CYANFS_EXTENT_MAX_INFLIGHT 256

static inline uint64_t cyanfs_extent_begin(cyanfs_extent_id id)
{
	return ((uint64_t)id) << CYANFS_EXTENT_SHIFT;
}

static inline uint64_t cyanfs_extent_end(cyanfs_extent_id id)
{
	return cyanfs_extent_begin(id) + CYANFS_EXTENT_SIZE;
}

static inline cyanfs_extent_id cyanfs_extent_from(uint64_t off)
{
	cyanfs_extent_id id;
	id = off >> CYANFS_EXTENT_SHIFT;
	return id;
}

static inline uint64_t cyanfs_extent_align(uint64_t size)
{
	return (size + CYANFS_EXTENT_MASK) & (~CYANFS_EXTENT_MASK);
}

struct cyanfs_extent {
	cyanfs_extent_id backend : 30;
	int map : 1; // 是否对应底层磁盘已分配的extent
	int pending : 1; // 该extent正在写入，需挂起新的IO
	cyanfs_extent_id file : 30;
	int error : 1; // 该extent存在IO问题
};

struct cyanfs_extent_node {
	struct cyanfs_extent v;
	union {
		CYANFS_RB_ENTRY(cyanfs_extent_node) file_node;
		CYANFS_RB_ENTRY(cyanfs_extent_node) backend_node;
	};
};

CYANFS_RB_HEAD(cyanfs_file_extents_rb, cyanfs_extent_node);

static inline int __cyanfs_extent_compare_file(struct cyanfs_extent_node *a, struct cyanfs_extent_node *b)
{
	if (a->v.file > b->v.file)
		return 1;
	else if (a->v.file < b->v.file)
		return -1;
	return 0;
}

CYANFS_RB_GENERATE_INTERNAL(cyanfs_file_extents_rb, cyanfs_extent_node, file_node, __cyanfs_extent_compare_file,
			    static inline)

CYANFS_RB_HEAD(cyanfs_backend_extents_rb, cyanfs_extent_node);

static inline int __cyanfs_extent_compare_backend(struct cyanfs_extent_node *a, struct cyanfs_extent_node *b)
{
	if (a->v.backend > b->v.backend)
		return 1;
	else if (a->v.backend < b->v.backend)
		return -1;
	return 0;
}

CYANFS_RB_GENERATE_INTERNAL(cyanfs_backend_extents_rb, cyanfs_extent_node, backend_node,
			    __cyanfs_extent_compare_backend, static inline)

struct cyanfs_file {
	struct cyanfs_super *super;
	struct cyanfs_file *parent;
	CYANFS_RB_ENTRY(cyanfs_file) id_node;
	CYANFS_RB_ENTRY(cyanfs_file) name_node;

	struct cyanfs_file_meta meta;
	struct cyanfs_file_extents_rb extents;

	enum {
		CYANFS_FILE_CLOSED,
		CYANFS_FILE_READONLY,
		CYANFS_FILE_READWRITE,
	} open_type;
	int reader;
};

CYANFS_RB_HEAD(cyanfs_files_id_rb, cyanfs_file);

static inline int __cyanfs_file_id_compare(struct cyanfs_file *a, struct cyanfs_file *b)
{
	if (a->meta.id > b->meta.id)
		return 1;
	else if (a->meta.id < b->meta.id)
		return -1;
	return 0;
}

CYANFS_RB_GENERATE_INTERNAL(cyanfs_files_id_rb, cyanfs_file, id_node, __cyanfs_file_id_compare, static inline)

CYANFS_RB_HEAD(cyanfs_files_name_rb, cyanfs_file);

static inline cyanfs_status __cyanfs_file_name_normalize(cyanfs_file_name_t *name)
{
	uint32_t i;

	if (!name->data[0] || name->zero)
		return -CYANFS_ERR_INVAL;

	for (i = 1; i < sizeof(name->data) && name->data[i]; ++i)
		;
	for (; i < sizeof(name->data); ++i)
		name->data[i] = 0;
	name->zero = 0;

	return 0;
}

static inline int __cyanfs_file_name_compare(struct cyanfs_file *a, struct cyanfs_file *b)
{
	return cyanfs_memcmp(&a->meta.name, &b->meta.name, sizeof(cyanfs_file_name_t));
}

CYANFS_RB_GENERATE_INTERNAL(cyanfs_files_name_rb, cyanfs_file, name_node, __cyanfs_file_name_compare, static inline)

static inline uint64_t cyanfs_map_extent_space(uint64_t f_off, uint64_t len)
{
	uint64_t extent_space = cyanfs_extent_end(cyanfs_extent_from(f_off)) - f_off;
	if (extent_space > len)
		extent_space = len;
	return extent_space;
}

static inline uint64_t cyanfs_map_extent_offset(cyanfs_extent_id b_id, uint64_t f_off)
{
	return CYANFS_SUPER_BLOCK_SIZE + cyanfs_extent_begin(b_id) + (f_off & CYANFS_EXTENT_MASK);
}

static inline cyanfs_extent_id cyanfs_map_extent_from(uint64_t b_off)
{
	return cyanfs_extent_from(b_off - CYANFS_SUPER_BLOCK_SIZE);
}

#define CYANFS_SUPER_MAX_FILES (4096)
#define CYANFS_SUPER_MAX_INFLIGHT_JOURNAL (10000)

#define CYANFS_SUPER_FLAG_READY (1 << 0) // 完成Journal的Replay，进入就绪状态
#define CYANFS_SUPER_FLAG_FLUSH (1 << 1)
#define CYANFS_SUPER_FLAG_ERROR (1 << 2) // 磁盘IO错误
#define CYANFS_SUPER_FLAG_DISCARD_ENABLED (1 << 3) // 是否对Backend发起Discard
#define CYANFS_SUPER_FLAG_DISCARD_WORKING (1 << 4)
#define CYANFS_SUPER_FLAG_COMPACT_ENABLED (1 << 5)
#define CYANFS_SUPER_FLAG_COMPACT_WORKING (1 << 6)

struct cyanfs_super {
	void *ctx;

	struct cyanfs_rwlock files_lock;
	struct cyanfs_files_id_rb files_by_id;
	struct cyanfs_files_name_rb files_by_name;
	struct cyanfs_super_meta meta;
	struct cyanfs_backend_extents_rb free_extents;
	struct cyanfs_backend_extents_rb discard_extents;
	struct cyanfs_backend_extents_rb wait_discard_extents;
	struct cyanfs_backend_extents_rb flush_extents;
	struct cyanfs_backend_extents_rb unused_extents;
	struct cyanfs_backend_extents_rb journal_extents;
	struct cyanfs_list_head journal_head;
	uint32_t journal_count;
	cyanfs_file_id_t max_file_id; // 全局单调递增
	uint32_t flags;
	cyanfs_atomic_t writers[2];
	int writer_current_id;

	struct cyanfs_task discard_task;
	struct cyanfs_journal_cursor journal_cursor;

	struct cyanfs_lock task_lock;
	struct cyanfs_list_head task_head;
	cyanfs_ctx_fn new_task;
};

static inline cyanfs_status __cyanfs_super_ensure_status(struct cyanfs_super *s)
{
	if (!(s->flags & CYANFS_SUPER_FLAG_READY))
		return -CYANFS_ERR_BUSY;
	if (s->flags & CYANFS_SUPER_FLAG_ERROR)
		return -CYANFS_ERR_IO;
	return 0;
}

static inline void __cyanfs_super_mark_error(struct cyanfs_super *s)
{
	s->flags |= CYANFS_SUPER_FLAG_ERROR;
}

static inline void cyanfs_super_mark_error(struct cyanfs_super *s)
{
	cyanfs_write_lock(&s->files_lock);
	__cyanfs_super_mark_error(s);
	cyanfs_write_unlock(&s->files_lock);
}

static inline void cyanfs_task_init(struct cyanfs_super *s, struct cyanfs_task *task, cyanfs_task_type_t type,
				    cyanfs_task_done_fn fn)
{
	task->super = s;
	task->done = fn;
	task->type = type;
};

static inline void cyanfs_super_new_task(struct cyanfs_super *s)
{
	if (s->new_task)
		s->new_task(s->ctx);
}

static inline int cyanfs_super_task_add_head(struct cyanfs_task *task)
{
	int empty;
	struct cyanfs_super *s = task->super;
	cyanfs_lock(&s->task_lock);
	empty = cyanfs_list_empty(&s->task_head);
	cyanfs_list_add(&task->list, &s->task_head);
	cyanfs_unlock(&s->task_lock);
	return empty;
}

static inline int cyanfs_super_task_add_tail(struct cyanfs_task *task)
{
	int empty;
	struct cyanfs_super *s = task->super;
	cyanfs_lock(&s->task_lock);
	empty = cyanfs_list_empty(&s->task_head);
	cyanfs_list_add_tail(&task->list, &s->task_head);
	cyanfs_unlock(&s->task_lock);
	return empty;
}

void __cyanfs_super_journal_append(struct cyanfs_super *s, struct cyanfs_journal_entry *j);
cyanfs_status __cyanfs_super_journal_replay(struct cyanfs_super *s, struct cyanfs_journal_entry *j);

cyanfs_status cyanfs_super_flusher_start(struct cyanfs_super *s, uint32_t min_journal_count, int ondisk);
static inline void cyanfs_super_flush_new_journal(struct cyanfs_super *s)
{
	cyanfs_super_flusher_start(s, CYANFS_SUPER_MAX_INFLIGHT_JOURNAL, 0);
}

void __cyanfs_super_discard_extent(struct cyanfs_super *s, struct cyanfs_extent_node *n,
				   struct cyanfs_backend_extents_rb *root);

static inline void __cyanfs_super_stat_add(struct cyanfs_super *s, struct cyanfs_backend_extents_rb *r, int v)
{
	if (r == &s->free_extents)
		s->meta.free_extents += v;
	else if (r == &s->journal_extents)
		s->meta.journal_extents += v;
}

static inline void __cyanfs_extent_super_to_super(struct cyanfs_super *s, struct cyanfs_extent_node *n,
						  struct cyanfs_backend_extents_rb *from,
						  struct cyanfs_backend_extents_rb *to)
{
	__cyanfs_super_stat_add(s, from, -1);
	CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, from, n);
	CYANFS_RB_INSERT(cyanfs_backend_extents_rb, to, n);
	__cyanfs_super_stat_add(s, to, 1);
}

static inline void __cyanfs_extent_file_to_super(struct cyanfs_super *s, struct cyanfs_file *f,
						 struct cyanfs_extent_node *n, struct cyanfs_file_extents_rb *from,
						 struct cyanfs_backend_extents_rb *to)
{
	--f->meta.extents;
	--s->meta.data_extents;
	CYANFS_RB_REMOVE(cyanfs_file_extents_rb, from, n);
	CYANFS_RB_INSERT(cyanfs_backend_extents_rb, to, n);
	__cyanfs_super_stat_add(s, to, 1);
}

static inline void __cyanfs_extent_super_to_file(struct cyanfs_super *s, struct cyanfs_file *f,
						 struct cyanfs_extent_node *n, struct cyanfs_backend_extents_rb *from,
						 struct cyanfs_file_extents_rb *to)
{
	__cyanfs_super_stat_add(s, from, -1);
	CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, from, n);
	CYANFS_RB_INSERT(cyanfs_file_extents_rb, to, n);
	++s->meta.data_extents;
	++f->meta.extents;
}

struct cyanfs_extent_node *__cyanfs_super_find_nearly_extent(struct cyanfs_super *s, cyanfs_extent_id id_from);
struct cyanfs_extent_node *__cyanfs_super_find_extent(struct cyanfs_super *s, cyanfs_extent_id id);
void __cyanfs_file_map_extent(struct cyanfs_file *f, struct cyanfs_extent_node *n, cyanfs_extent_id f_id);
void __cyanfs_file_unmap_extent(struct cyanfs_file *f, struct cyanfs_extent_node *n);
struct cyanfs_extent_node *__cyanfs_file_find_extent(struct cyanfs_file *f, uint64_t f_off);
struct cyanfs_extent_node *__cyanfs_file_find_extent_follow(struct cyanfs_file *f, uint64_t f_off);
struct cyanfs_extent_node *__cyanfs_file_alloc_extent(struct cyanfs_file *f, uint64_t f_off);
cyanfs_status __cyanfs_file_bind_extent(struct cyanfs_file *f, cyanfs_extent_id f_id, cyanfs_extent_id b_id);
cyanfs_status __cyanfs_file_unbind_extent(struct cyanfs_file *f, cyanfs_extent_id f_id);

#endif
