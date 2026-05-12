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

cyanfs_status cyanfs_read(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len, cyanfs_io_spliter handler)
{
	struct cyanfs_super *s = f->super;

	if (!len || f_off + len > f->meta.size)
		return -CYANFS_ERR_INVAL;

	while (len) {
		cyanfs_status err;
		struct cyanfs_extent_node *n;
		struct cyanfs_extent extent = { 0 };
		uint64_t l = cyanfs_map_extent_space(f_off, len);

		cyanfs_read_lock(&s->files_lock);
		err = __cyanfs_super_ensure_status(s);
		if (err) {
			cyanfs_read_unlock(&s->files_lock);
			return err;
		}
		n = __cyanfs_file_find_extent_follow(f, f_off);
		if (n)
			extent = n->v;
		cyanfs_read_unlock(&s->files_lock);

		if (extent.error) {
			err = -CYANFS_ERR_IO;
		} else if (!extent.map) {
			err = handler(CYANFS_MAP_NOP, 0, l, ctx);
		} else if (extent.pending) {
			err = handler(CYANFS_MAP_REQUEUE, 0, l, ctx);
		} else {
			err = handler(CYANFS_MAP_SUBMIT, cyanfs_map_extent_offset(extent.backend, f_off), l, ctx);
		}

		if (err)
			return err;

		f_off += l;
		len -= l;
	}
	return 0;
}

struct cyanfs_task_copy_on_write {
	struct cyanfs_task base;
	struct cyanfs_journal_entry *journal;
	struct cyanfs_extent_node *extent_node;
};

static void cyanfs_task_copy_on_write_done(struct cyanfs_task *t, cyanfs_status err)
{
	struct cyanfs_task_copy_on_write *task = cyanfs_container_of(t, struct cyanfs_task_copy_on_write, base);
	struct cyanfs_super *s = task->base.super;

	cyanfs_write_lock(&s->files_lock);
	task->extent_node->v.pending = 0;
	if (err) {
		task->extent_node->v.error = 1;
		cyanfs_journal_free(task->journal);
	} else {
		__cyanfs_super_journal_append(s, task->journal);
	}
	cyanfs_write_unlock(&s->files_lock);

	if (!err)
		cyanfs_super_flush_new_journal(s);

	cyanfs_free(task);
}

cyanfs_status cyanfs_write(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len, cyanfs_io_spliter handler)
{
	cyanfs_status err;
	struct cyanfs_super *s = f->super;
	int new_journal = 0;

	if (!len || f_off + len > f->meta.size || f->open_type != CYANFS_FILE_READWRITE)
		return -CYANFS_ERR_INVAL;

	while (len) {
		cyanfs_atomic_t *writer = NULL;
		int call_new_task = 0;
		struct cyanfs_journal_entry *j = NULL;
		struct cyanfs_task_copy_on_write *t = NULL;
		struct cyanfs_extent_node *n, *p;
		struct cyanfs_extent extent = { 0 };
		cyanfs_extent_id parent_b_id = cyanfs_extent_id_invalid;
		void (*unlocker)(struct cyanfs_rwlock * lock);
		uint64_t l = cyanfs_map_extent_space(f_off, len);

		cyanfs_read_lock(&s->files_lock);
		unlocker = cyanfs_read_unlock;

		err = __cyanfs_super_ensure_status(s);
		if (err)
			goto out;

		n = __cyanfs_file_find_extent(f, f_off);
		if (n)
			goto mapped;
		p = __cyanfs_file_find_extent_follow(f->parent, f_off);
		if (p)
			parent_b_id = p->v.backend;

		unlocker(&s->files_lock);

		j = cyanfs_journal_alloc(CYANFS_JOURNAL_BIND);
		if (!j) {
			err = -CYANFS_ERR_NOMEM;
			goto out;
		}

		if (p) {
			t = cyanfs_malloc(sizeof(struct cyanfs_task_copy_on_write));
			if (!t) {
				err = -CYANFS_ERR_NOMEM;
				goto out;
			}
		}

		cyanfs_write_lock(&s->files_lock);
		unlocker = cyanfs_write_unlock;

		n = __cyanfs_file_find_extent(f, f_off);
		if (n) // 竞争条件，另一个write操作已经分配了此区域
			goto mapped;

		n = __cyanfs_file_alloc_extent(f, f_off);
		if (!n) {
			err = -CYANFS_ERR_NOSPACE;
			goto unlock_out;
		}

		j->bind.id = f->meta.id;
		j->bind.file = n->v.file;
		j->bind.backend = n->v.backend;
		if (p) {
			n->v.pending = 1;

			j->flush = 1;

			t->journal = j;
			t->extent_node = n;
			cyanfs_task_init(s, &t->base, CYANFS_TASK_COPY, cyanfs_task_copy_on_write_done);
			t->base.copy.len = CYANFS_EXTENT_SIZE;
			t->base.copy.b_src_off = cyanfs_map_extent_offset(parent_b_id, 0);
			t->base.copy.b_dst_off = cyanfs_map_extent_offset(n->v.backend, 0);

			call_new_task |= cyanfs_super_task_add_tail(&t->base);
			t = NULL;
			j = NULL;
		} else {
			new_journal = 1;
			__cyanfs_super_journal_append(s, j);
			j = NULL;
		}

	mapped:
		err = 0;
		CYANFS_DEBUG_BUG_ON(!n->v.map);
		writer = &s->writers[s->writer_current_id];
		cyanfs_atomic_inc(writer);
		extent = n->v;

	unlock_out:
		unlocker(&s->files_lock);

	out:
		if (j)
			cyanfs_journal_free(j);
		if (t)
			cyanfs_free(t);

		if (call_new_task)
			cyanfs_super_new_task(s);

		if (extent.error) {
			err = -CYANFS_ERR_IO;
		} else if (extent.map) {
			if (extent.pending) {
				err = handler(CYANFS_MAP_REQUEUE, 0, l, ctx);
			} else {
				err = handler(CYANFS_MAP_SUBMIT, cyanfs_map_extent_offset(extent.backend, f_off), l,
					      ctx);
			}
		}

		if (writer)
			cyanfs_atomic_dec(writer);

		if (err)
			break;

		f_off += l;
		len -= l;
	}

	if (new_journal)
		cyanfs_super_flush_new_journal(s);

	return err;
}

cyanfs_status cyanfs_discard(struct cyanfs_file *f, void *ctx, uint64_t f_off, uint64_t len, cyanfs_io_spliter handler)
{
	cyanfs_status err;
	struct cyanfs_super *s = f->super;
	int new_journal = 0;

	if (!len || f_off + len > f->meta.size || f->open_type != CYANFS_FILE_READWRITE)
		return -CYANFS_ERR_INVAL;

	while (len) {
		cyanfs_map_type_t map = CYANFS_MAP_NOP;
		struct cyanfs_extent_node *n, *p;
		struct cyanfs_journal_entry *j = NULL;
		void (*unlocker)(struct cyanfs_rwlock * lock);
		uint64_t l = cyanfs_map_extent_space(f_off, len);

		if (cyanfs_extent_begin(cyanfs_extent_from(f_off)) != f_off || l < CYANFS_EXTENT_SIZE)
			goto handle;

		cyanfs_read_lock(&s->files_lock);
		unlocker = cyanfs_read_unlock;

	upgraded:
		p = __cyanfs_file_find_extent_follow(f->parent, f_off);
		if (p)
			goto unlock_out;
		n = __cyanfs_file_find_extent(f, f_off);
		if (!n)
			goto unlock_out;
		if (n->v.error || __cyanfs_super_ensure_status(s)) {
			goto unlock_out;
		}
		if (n->v.pending) {
			map = CYANFS_MAP_REQUEUE;
			goto unlock_out;
		}
		if (unlocker == cyanfs_read_unlock) {
			unlocker(&s->files_lock);

			j = cyanfs_journal_alloc(CYANFS_JOURNAL_UNBIND);
			if (!j) {
				err = -CYANFS_ERR_NOMEM;
				goto out;
			}
			j->unbind.id = f->meta.id;
			j->unbind.file = cyanfs_extent_from(f_off);
			j->flush = 1;
			new_journal = 1;

			cyanfs_write_lock(&s->files_lock);
			unlocker = cyanfs_write_unlock;
			goto upgraded;
		} else {
			__cyanfs_file_unmap_extent(f, n);
			__cyanfs_super_journal_append(s, j);
			j = NULL;
		}

	unlock_out:
		unlocker(&s->files_lock);

	handle:
		err = handler(map, 0, l, ctx);

	out:
		if (j)
			cyanfs_journal_free(j);

		if (err)
			break;

		f_off += l;
		len -= l;
	}

	if (new_journal)
		cyanfs_super_flush_new_journal(s);

	return err;
}

cyanfs_status cyanfs_flush(struct cyanfs_file *f, cyanfs_map_type_t *type)
{
	struct cyanfs_super *s = f->super;
	cyanfs_status err;

	err = cyanfs_super_flush(s, 0);
	if (err)
		return err;

	cyanfs_lock(&s->task_lock);
	*type = cyanfs_list_empty(&s->task_head) ? CYANFS_MAP_SUBMIT : CYANFS_MAP_REQUEUE;
	cyanfs_unlock(&s->task_lock);

	return 0;
}

static void __cyanfs_task_free(struct cyanfs_task *t, cyanfs_status err)
{
	cyanfs_free(t);
}

cyanfs_status cyanfs_requeue(struct cyanfs_file *f, void *ctx, cyanfs_ctx_fn fn)
{
	struct cyanfs_task *t;
	struct cyanfs_super *s = f->super;

	t = cyanfs_malloc(sizeof(struct cyanfs_task));
	if (!t)
		return -CYANFS_ERR_NOMEM;

	cyanfs_task_init(s, t, CYANFS_TASK_REQUEUE, __cyanfs_task_free);
	t->requeue.file = f;
	t->requeue.ctx = ctx;
	t->requeue.fn = fn;

	if (cyanfs_super_task_add_tail(t))
		cyanfs_super_new_task(s);

	return 0;
}

cyanfs_status cyanfs_seek_extent(struct cyanfs_file *f, uint64_t *f_off)
{
	struct cyanfs_super *s = f->super;
	struct cyanfs_extent_node tmp, *n;
	cyanfs_status r;

	if (f->open_type != CYANFS_FILE_READONLY)
		return -CYANFS_ERR_INVAL;

	tmp.v.file = cyanfs_extent_from(*f_off);
	cyanfs_read_lock(&s->files_lock);
	n = CYANFS_RB_NFIND(cyanfs_file_extents_rb, &f->extents, &tmp);
	if (n) {
		uint64_t begin = cyanfs_extent_begin(n->v.file);
		if (begin > *f_off)
			*f_off = begin;
		r = 0;
	} else {
		r = -CYANFS_ERR_NOENT;
	}
	cyanfs_read_unlock(&s->files_lock);

	return r;
}
