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
#include "internal.h"

static void cyanfs_super_discard_extents_done(struct cyanfs_task *t, cyanfs_status err);

static void cyanfs_super_discard_extents_begin(struct cyanfs_task *t, cyanfs_status err)
{
	struct cyanfs_super *s = t->super;
	struct cyanfs_extent_node *n, *next;

	n = CYANFS_RB_MIN(cyanfs_backend_extents_rb, &s->wait_discard_extents);
	if (!n) {
		cyanfs_write_lock(&s->files_lock);
		CYANFS_DEBUG_BUG_ON(!(s->flags & CYANFS_SUPER_FLAG_DISCARD_WORKING));
		s->flags &= ~CYANFS_SUPER_FLAG_DISCARD_WORKING;
		cyanfs_write_unlock(&s->files_lock);
		return;
	}

	t->discard.b_off = cyanfs_map_extent_offset(n->v.backend, 0);
	t->discard.len = 0;
	for (;;) {
		next = CYANFS_RB_NEXT(cyanfs_backend_extents_rb, &s->wait_discard_extents, n);
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->wait_discard_extents, n);
		CYANFS_RB_INSERT(cyanfs_backend_extents_rb, &s->discard_extents, n);
		t->discard.len += CYANFS_EXTENT_SIZE;

		if (!next || next->v.backend != n->v.backend + 1)
			break;
		n = next;
	}
	cyanfs_task_init(s, t, CYANFS_TASK_DISCARD, cyanfs_super_discard_extents_done);
	cyanfs_super_task_add_tail(t);
}

static void cyanfs_super_discard_extents_done(struct cyanfs_task *t, cyanfs_status err)
{
	struct cyanfs_super *s = t->super;
	struct cyanfs_extent_node *n, *next;

	cyanfs_write_lock(&s->files_lock);
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->discard_extents, next)
	{
		__cyanfs_extent_super_to_super(s, n, &s->discard_extents, &s->free_extents);
	}
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_discard_extents_begin(t, 0);
}

void __cyanfs_super_discard_extent(struct cyanfs_super *s, struct cyanfs_extent_node *n,
				   struct cyanfs_backend_extents_rb *root)
{
	if (s->flags & CYANFS_SUPER_FLAG_DISCARD_ENABLED) {
		__cyanfs_extent_super_to_super(s, n, root, &s->wait_discard_extents);
		if (!(s->flags & CYANFS_SUPER_FLAG_DISCARD_WORKING)) {
			s->flags |= CYANFS_SUPER_FLAG_DISCARD_WORKING;
			cyanfs_task_init(s, &s->discard_task, CYANFS_TASK_NOP, cyanfs_super_discard_extents_begin);
			cyanfs_super_task_add_tail(&s->discard_task);
		}
	} else {
		__cyanfs_extent_super_to_super(s, n, root, &s->free_extents);
	}
}

struct cyanfs_task_super_flusher {
	int flush_before_write;
	struct cyanfs_task before;
	struct cyanfs_task after;
	struct cyanfs_task free;
	struct cyanfs_task_journal_writer writer;
};

static void cyanfs_super_flusher_free(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_task_super_flusher *t = cyanfs_container_of(base, struct cyanfs_task_super_flusher, free);
	cyanfs_free(t);
}

static void cyanfs_super_flusher_after_flush(struct cyanfs_task *t, cyanfs_status err)
{
	struct cyanfs_super *s = t->super;
	struct cyanfs_extent_node *n, *next;

	if (!CYANFS_RB_EMPTY(&s->flush_extents)) {
		cyanfs_write_lock(&s->files_lock);
		if (cyanfs_atomic_read(&s->writers[!s->writer_current_id])) {
			s->flags |= CYANFS_SUPER_FLAG_FLUSH;
		} else {
			CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->flush_extents, next)
			{
				__cyanfs_super_discard_extent(s, n, &s->flush_extents);
			}
		}
		cyanfs_write_unlock(&s->files_lock);
	}
	if (err) {
		cyanfs_super_mark_error(s);
	} else {
		s->journal_cursor.extent_id_ondisk = s->journal_cursor.extent_id_flush;
	}
}

static void cyanfs_super_flusher_before_flush(struct cyanfs_task *t, cyanfs_status err)
{
	struct cyanfs_super *s = t->super;

	cyanfs_write_lock(&s->files_lock);
	if (CYANFS_RB_EMPTY(&s->flush_extents)) {
		s->flush_extents = s->unused_extents;
		CYANFS_RB_INIT(&s->unused_extents);
		s->writer_current_id = !s->writer_current_id;
	}
	cyanfs_write_unlock(&s->files_lock);

	s->journal_cursor.extent_id_flush = s->journal_cursor.extent_id;

	cyanfs_task_init(s, t, CYANFS_TASK_FLUSH, cyanfs_super_flusher_after_flush);
	cyanfs_super_task_add_head(t);
}

static void cyanfs_super_flusher_write_error(struct cyanfs_task_journal_writer *writer)
{
	struct cyanfs_super *s = writer->base.super;

	cyanfs_super_mark_error(s);
	while (!cyanfs_list_empty(&writer->head)) {
		struct cyanfs_journal_entry *j =
			cyanfs_list_first_entry(&writer->head, struct cyanfs_journal_entry, list);
		cyanfs_list_del(&j->list);
		cyanfs_journal_free(j);
	}
}

static void cyanfs_super_flusher_flush_before_write(struct cyanfs_task_journal_writer *writer)
{
	struct cyanfs_task_super_flusher *t = cyanfs_container_of(writer, struct cyanfs_task_super_flusher, writer);
	struct cyanfs_super *s = writer->base.super;

	if (t->flush_before_write)
		return;
	t->flush_before_write = 1;

	cyanfs_task_init(s, &t->before, CYANFS_TASK_NOP, cyanfs_super_flusher_before_flush);
	cyanfs_super_task_add_head(&t->before);
}

static void cyanfs_super_flusher_write_finish(struct cyanfs_task_journal_writer *writer)
{
	struct cyanfs_super *s = writer->base.super;
	struct cyanfs_super_meta *m = &s->meta;
	uint32_t limit;
	int compact = 0;
	int flush = s->journal_cursor.extent_id != s->journal_cursor.extent_id_ondisk;

	cyanfs_read_lock(&s->files_lock);
	limit = m->free_extents >> 10;
	if (limit > CYANFS_EXTENT_MAX_INFLIGHT)
		limit = CYANFS_EXTENT_MAX_INFLIGHT;
	flush |= m->total_extents - m->data_extents - m->journal_extents - m->free_extents > limit;
	compact = m->journal_extents >= m->reserved_extents / 2 && !(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING);
	cyanfs_read_unlock(&s->files_lock);

	if (flush) {
		cyanfs_write_lock(&s->files_lock);
		s->flags |= CYANFS_SUPER_FLAG_FLUSH;
		cyanfs_write_unlock(&s->files_lock);
	}

	if (compact)
		cyanfs_super_compact(s);
}

cyanfs_status cyanfs_super_flusher_start(struct cyanfs_super *s, uint32_t min_journal_count, int ondisk)
{
	cyanfs_status err;
	int no_journal;
	struct cyanfs_task_super_flusher *t;
	int call_new_task = 0;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	no_journal = s->journal_count <= min_journal_count;
	ondisk |= s->flags & CYANFS_SUPER_FLAG_FLUSH;
	cyanfs_read_unlock(&s->files_lock);

	if (err || (no_journal && !ondisk))
		return err;

	t = cyanfs_malloc(sizeof(struct cyanfs_task_super_flusher));
	if (!t)
		return -CYANFS_ERR_NOMEM;
	t->flush_before_write = 0;

	cyanfs_write_lock(&s->files_lock);
	no_journal = cyanfs_list_empty(&s->journal_head);
	if (!no_journal) {
		cyanfs_journal_writer_init(&t->writer);
		cyanfs_list_splice_tail_init(&s->journal_head, &t->writer.head);
		s->journal_count = 0;
		t->writer.cursor = &s->journal_cursor;
		t->writer.operations.flush = cyanfs_super_flusher_flush_before_write;
		t->writer.operations.error = cyanfs_super_flusher_write_error;
		t->writer.operations.finish = cyanfs_super_flusher_write_finish;
		call_new_task |= cyanfs_journal_writer_start(s, &t->writer);
	}
	if (ondisk) {
		s->flags &= ~CYANFS_SUPER_FLAG_FLUSH;
		cyanfs_task_init(s, &t->after, CYANFS_TASK_NOP, cyanfs_super_flusher_before_flush);
		call_new_task |= cyanfs_super_task_add_tail(&t->after);
	}
	if (ondisk || !no_journal) {
		cyanfs_task_init(s, &t->free, CYANFS_TASK_NOP, cyanfs_super_flusher_free);
		call_new_task |= cyanfs_super_task_add_tail(&t->free);
	} else {
		cyanfs_free(t);
	}
	cyanfs_write_unlock(&s->files_lock);

	if (call_new_task)
		cyanfs_super_new_task(s);

	return 0;
}
