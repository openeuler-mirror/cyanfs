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
#ifndef __CYANFS_JOURNAL_HEADER__
#define __CYANFS_JOURNAL_HEADER__

#include "core.h"

static inline void cyanfs_super_encode_header(uint8_t *p, struct cyanfs_super_header *h)
{
	cyanfs_put32(&p, h->crc32);
	cyanfs_put32(&p, h->magic);
	cyanfs_put_generic(&p, &h->uuid, sizeof(cyanfs_uuid_t));
	cyanfs_put64(&p, h->version);
	cyanfs_put64(&p, h->journal_id);
	cyanfs_put64(&p, h->journal_seq);
}

static inline void cyanfs_super_decode_header(uint8_t *p, struct cyanfs_super_header *h)
{
	h->crc32 = cyanfs_get32(&p);
	h->magic = cyanfs_get32(&p);
	cyanfs_get_generic(&p, &h->uuid, sizeof(cyanfs_uuid_t));
	h->version = cyanfs_get64(&p);
	h->journal_id = cyanfs_get64(&p);
	h->journal_seq = cyanfs_get64(&p);
}

struct cyanfs_journal_cursor {
	uint8_t *page;
	uint64_t extent_off;
	cyanfs_extent_id extent_id;
	cyanfs_extent_id extent_id_flush;
	cyanfs_extent_id extent_id_ondisk;
	cyanfs_journal_seq_t seq;
};

#define CYANFS_JOURNAL_BLOCK_SHIFT 9ULL
#define CYANFS_JOURNAL_BLOCK_SIZE (1ULL << CYANFS_JOURNAL_BLOCK_SHIFT)
#define CYANFS_JOURNAL_BLOCK_MASK (CYANFS_JOURNAL_BLOCK_SIZE - 1ULL)
#define CYANFS_JOURNAL_MAX_BLOCK_COUNT 128ULL
#define CYANFS_JOURNAL_PAGE_SIZE (CYANFS_JOURNAL_BLOCK_SIZE * CYANFS_JOURNAL_MAX_BLOCK_COUNT)
#define CYANFS_JOURNAL_TAIL_SIZE (cyanfs_journal_entry_size(CYANFS_JOURNAL_NEXT))
#define CYANFS_JOURNAL_HEADER_SIZE 15

struct cyanfs_journal_header {
	uint32_t crc32;
	cyanfs_journal_seq_t seq;
	uint64_t size;
	uint16_t count;
};

typedef enum {
	CYANFS_JOURNAL_NEXT,
	CYANFS_JOURNAL_CREATE,
	CYANFS_JOURNAL_TRUNCATE,
	CYANFS_JOURNAL_FORK,
	CYANFS_JOURNAL_DELETE,
	CYANFS_JOURNAL_BIND,
	CYANFS_JOURNAL_UNBIND,
	CYANFS_JOURNAL_RENAME,
} cyanfs_journal_entry_type;

struct cyanfs_journal_entry {
	struct cyanfs_list_head list;
	cyanfs_journal_entry_type type;
	int flush;

	union {
		struct {
			cyanfs_extent_id backend;
		} next;
		struct {
			cyanfs_file_name_t *name;
			cyanfs_file_id_t id;
		} create;
		struct {
			cyanfs_file_id_t id;
			uint64_t size;
		} truncate;
		struct {
			cyanfs_file_name_t *name;
			cyanfs_file_id_t id;
			cyanfs_file_id_t pid;
		} fork;
		struct {
			cyanfs_file_id_t id;
		} delete;
		struct {
			cyanfs_file_id_t id;
			cyanfs_extent_id backend, file;
		} bind;
		struct {
			cyanfs_file_id_t id;
			cyanfs_extent_id file;
		} unbind;
		struct {
			cyanfs_file_name_t *name;
			cyanfs_file_id_t id;
		} rename;
	};
};

static inline struct cyanfs_journal_entry *cyanfs_journal_alloc(cyanfs_journal_entry_type type)
{
	struct cyanfs_journal_entry *j;
	int use_name = type == CYANFS_JOURNAL_CREATE || type == CYANFS_JOURNAL_FORK || type == CYANFS_JOURNAL_RENAME;

	j = cyanfs_malloc(sizeof(struct cyanfs_journal_entry) + (use_name ? sizeof(cyanfs_file_name_t) : 0));
	if (!j)
		return NULL;
	j->type = type;
	j->flush = 0;
	switch (type) {
	case CYANFS_JOURNAL_CREATE:
		j->create.name = (cyanfs_file_name_t *)(j + 1);
		break;
	case CYANFS_JOURNAL_FORK:
		j->fork.name = (cyanfs_file_name_t *)(j + 1);
		break;
	case CYANFS_JOURNAL_RENAME:
		j->rename.name = (cyanfs_file_name_t *)(j + 1);
		break;
	default:
		break;
	}
	return j;
}

static inline void cyanfs_journal_free(struct cyanfs_journal_entry *j)
{
	cyanfs_free(j);
}

static inline void cyanfs_journal_encode_header(uint8_t *p, struct cyanfs_journal_header *h)
{
	cyanfs_put32(&p, h->crc32);
	cyanfs_put64(&p, h->seq);
	cyanfs_put16(&p, h->count);
	cyanfs_put8(&p, (h->size) >> CYANFS_JOURNAL_BLOCK_SHIFT);
}

static inline cyanfs_status cyanfs_journal_decode_header_checked(uint8_t *p, uint64_t available,
							 struct cyanfs_journal_header *h)
{
	if (available < CYANFS_JOURNAL_HEADER_SIZE)
		return -CYANFS_ERR_INVAL;

	h->crc32 = cyanfs_get32(&p);
	h->seq = cyanfs_get64(&p);
	h->count = cyanfs_get16(&p);
	h->size = ((uint64_t)cyanfs_get8(&p)) << CYANFS_JOURNAL_BLOCK_SHIFT;
	return 0;
}

static inline uint64_t cyanfs_journal_entry_size(cyanfs_journal_entry_type type)
{
	struct cyanfs_journal_entry j;
	switch (type) {
	case CYANFS_JOURNAL_NEXT:
		return 1 + sizeof(j.next.backend);
	case CYANFS_JOURNAL_CREATE:
		return 1 + sizeof(j.create.id) + sizeof(*j.create.name);
	case CYANFS_JOURNAL_TRUNCATE:
		return 1 + sizeof(j.truncate.id) + sizeof(j.truncate.size);
	case CYANFS_JOURNAL_FORK:
		return 1 + sizeof(j.fork.id) + sizeof(j.fork.pid) + sizeof(*j.fork.name);
	case CYANFS_JOURNAL_DELETE:
		return 1 + sizeof(j.delete.id);
	case CYANFS_JOURNAL_BIND:
		return 1 + sizeof(j.bind.id) + sizeof(j.bind.file) + sizeof(j.bind.backend);
	case CYANFS_JOURNAL_UNBIND:
		return 1 + sizeof(j.unbind.id) + sizeof(j.unbind.file);
	case CYANFS_JOURNAL_RENAME:
		return 1 + sizeof(j.rename.id) + sizeof(*j.rename.name);
	default:
		return 0;
	}
}

static inline void cyanfs_journal_encode_entry(uint8_t **p, struct cyanfs_journal_entry *j)
{
	cyanfs_put8(p, j->type);
	switch (j->type) {
	case CYANFS_JOURNAL_NEXT:
		cyanfs_put32(p, j->next.backend);
		break;
	case CYANFS_JOURNAL_CREATE:
		cyanfs_put64(p, j->create.id);
		cyanfs_put_generic(p, j->create.name, sizeof(cyanfs_file_name_t));
		break;
	case CYANFS_JOURNAL_TRUNCATE:
		cyanfs_put64(p, j->truncate.id);
		cyanfs_put64(p, j->truncate.size);
		break;
	case CYANFS_JOURNAL_FORK:
		cyanfs_put64(p, j->fork.id);
		cyanfs_put64(p, j->fork.pid);
		cyanfs_put_generic(p, j->fork.name, sizeof(cyanfs_file_name_t));
		break;
	case CYANFS_JOURNAL_DELETE:
		cyanfs_put64(p, j->delete.id);
		break;
	case CYANFS_JOURNAL_BIND:
		cyanfs_put64(p, j->bind.id);
		cyanfs_put32(p, j->bind.file);
		cyanfs_put32(p, j->bind.backend);
		break;
	case CYANFS_JOURNAL_UNBIND:
		cyanfs_put64(p, j->unbind.id);
		cyanfs_put32(p, j->unbind.file);
		break;
	case CYANFS_JOURNAL_RENAME:
		cyanfs_put64(p, j->rename.id);
		cyanfs_put_generic(p, j->rename.name, sizeof(cyanfs_file_name_t));
		break;
	}
}

// 应确保p指向的空间生命周期长于解析的journal entry
static inline cyanfs_status cyanfs_journal_decode_entry_checked(uint8_t **p, uint8_t *end,
							struct cyanfs_journal_entry *j)
{
	uint8_t *cursor = *p;
	cyanfs_journal_entry_type type;
	uint64_t size;

	if (cursor >= end)
		return -CYANFS_ERR_INVAL;

	type = cyanfs_get8(&cursor);
	size = cyanfs_journal_entry_size(type);
	if (!size || size > (uint64_t)(end - *p))
		return -CYANFS_ERR_INVAL;

	j->type = type;
	switch (j->type) {
	case CYANFS_JOURNAL_NEXT:
		j->next.backend = cyanfs_get32(&cursor);
		break;
	case CYANFS_JOURNAL_CREATE:
		j->create.id = cyanfs_get64(&cursor);
		j->create.name = (void *)cursor;
		cursor += sizeof(cyanfs_file_name_t);
		break;
	case CYANFS_JOURNAL_TRUNCATE:
		j->truncate.id = cyanfs_get64(&cursor);
		j->truncate.size = cyanfs_get64(&cursor);
		break;
	case CYANFS_JOURNAL_FORK:
		j->fork.id = cyanfs_get64(&cursor);
		j->fork.pid = cyanfs_get64(&cursor);
		j->fork.name = (void *)cursor;
		cursor += sizeof(cyanfs_file_name_t);
		break;
	case CYANFS_JOURNAL_DELETE:
		j->delete.id = cyanfs_get64(&cursor);
		break;
	case CYANFS_JOURNAL_BIND:
		j->bind.id = cyanfs_get64(&cursor);
		j->bind.file = cyanfs_get32(&cursor);
		j->bind.backend = cyanfs_get32(&cursor);
		break;
	case CYANFS_JOURNAL_UNBIND:
		j->unbind.id = cyanfs_get64(&cursor);
		j->unbind.file = cyanfs_get32(&cursor);
		break;
	case CYANFS_JOURNAL_RENAME:
		j->rename.id = cyanfs_get64(&cursor);
		j->rename.name = (void *)cursor;
		cursor += sizeof(cyanfs_file_name_t);
		break;
	}

	*p = cursor;
	return 0;
}

#ifdef CYANFS_DEBUG_ENABLED
#define cyanfs_journal_dump_entry(e)                                                                                   \
	do {                                                                                                           \
		if (!debug_dump_journal)                                                                               \
			break;                                                                                         \
		switch ((e)->type) {                                                                                   \
		case CYANFS_JOURNAL_NEXT:                                                                              \
			CYANFS_DEBUG("next: %" PRIu32, (e)->next.backend);                                             \
			break;                                                                                         \
		case CYANFS_JOURNAL_CREATE:                                                                            \
			CYANFS_DEBUG("create: file %" PRIu64 " name %.*" CYANFS_PRINT_STRING_TYPE, (e)->create.id,     \
				     (int)sizeof(cyanfs_file_name_t), (char *)(e)->create.name->data);                 \
			break;                                                                                         \
		case CYANFS_JOURNAL_TRUNCATE:                                                                          \
			CYANFS_DEBUG("truncate: file %" PRIu64 " size %" PRIu64, (e)->truncate.id,                     \
				     (e)->truncate.size);                                                              \
			break;                                                                                         \
		case CYANFS_JOURNAL_FORK:                                                                              \
			CYANFS_DEBUG("fork: %" PRIu64 " to file %" PRIu64 " name %.*" CYANFS_PRINT_STRING_TYPE "",     \
				     (e)->fork.pid, (e)->fork.id, (int)sizeof(cyanfs_file_name_t),                     \
				     (char *)(e)->fork.name->data);                                                    \
			break;                                                                                         \
		case CYANFS_JOURNAL_DELETE:                                                                            \
			CYANFS_DEBUG("delete: file %" PRIu64, (e)->delete.id);                                         \
			break;                                                                                         \
		case CYANFS_JOURNAL_BIND:                                                                              \
			CYANFS_DEBUG("bind: file %" PRIu64 " fid: %" PRIu32 " bid: %" PRIu32, (e)->bind.id,            \
				     (e)->bind.file, (e)->bind.backend);                                               \
			break;                                                                                         \
		case CYANFS_JOURNAL_UNBIND:                                                                            \
			CYANFS_DEBUG("unbind: file %" PRIu64 " fid: %" PRIu32, (e)->unbind.id, (e)->unbind.file);      \
			break;                                                                                         \
		case CYANFS_JOURNAL_RENAME:                                                                            \
			CYANFS_DEBUG("rename: file %" PRIu64 " name %.*" CYANFS_PRINT_STRING_TYPE, (e)->rename.id,     \
				     (int)sizeof(cyanfs_file_name_t), (char *)(e)->rename.name->data);                 \
			break;                                                                                         \
		}                                                                                                      \
	} while (0)
#else
#define cyanfs_journal_dump_entry(e)                                                                                   \
	do {                                                                                                           \
	} while (0)
#endif

#define CYANFS_EXTENT_SET_LIIMT 200
struct cyanfs_extent_set {
	struct cyanfs_list_head node;
	int count;
	cyanfs_extent_id id[CYANFS_EXTENT_SET_LIIMT];
};

struct cyanfs_task_journal_loader {
	struct cyanfs_task base;
	struct cyanfs_journal_cursor *cursor;
	struct cyanfs_super_header header;
	cyanfs_extent_id end_id;

	struct cyanfs_extent_set static_journals;
	struct cyanfs_list_head journals;

	struct {
		cyanfs_status (*parser)(struct cyanfs_task_journal_loader *t, struct cyanfs_journal_entry *j);
		void (*finish)(struct cyanfs_task_journal_loader *t);
		void (*error)(struct cyanfs_task_journal_loader *t);
	} operations;
};

static inline void cyanfs_journal_loader_init(struct cyanfs_task_journal_loader *t)
{
	t->cursor = 0;
	t->end_id = cyanfs_extent_id_invalid;
	t->operations.parser = 0;
	t->operations.finish = 0;
	t->operations.error = 0;
	t->static_journals.count = 0;
	CYANFS_INIT_LIST_HEAD(&t->journals);
	cyanfs_list_add_tail(&t->static_journals.node, &t->journals);
}

int cyanfs_journal_loader_start(struct cyanfs_super *s, struct cyanfs_task_journal_loader *t);

struct cyanfs_task_journal_writer {
	struct cyanfs_task base;
	struct cyanfs_list_head head;
	struct cyanfs_journal_cursor *cursor;
	cyanfs_extent_id next_id;
	struct {
		void (*flush)(struct cyanfs_task_journal_writer *t);
		void (*finish)(struct cyanfs_task_journal_writer *t);
		void (*error)(struct cyanfs_task_journal_writer *t);
	} operations;
};

static inline void cyanfs_journal_writer_init(struct cyanfs_task_journal_writer *t)
{
	t->cursor = 0;
	CYANFS_INIT_LIST_HEAD(&t->head);
	t->next_id = cyanfs_extent_id_invalid;
	t->operations.error = 0;
	t->operations.finish = 0;
	t->operations.flush = 0;
}

int cyanfs_journal_writer_start(struct cyanfs_super *s, struct cyanfs_task_journal_writer *t);

#endif
