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

struct cyanfs_task_journal_compact {
	struct cyanfs_task base;
	struct cyanfs_task_journal_writer writer;
	struct cyanfs_task_journal_loader loader;
	struct cyanfs_super compact;
	struct cyanfs_super_header header;
};

static void cyanfs_journal_compact_free(struct cyanfs_task_journal_compact *t)
{
	struct cyanfs_file *f, *next_f;
	struct cyanfs_extent_node *n, *next_n;

	CYANFS_DEBUG("compact end");

	while (!cyanfs_list_empty(&t->writer.head)) {
		struct cyanfs_journal_entry *j =
			cyanfs_list_first_entry(&t->writer.head, struct cyanfs_journal_entry, list);
		cyanfs_list_del(&j->list);
		cyanfs_journal_free(j);
	}
	CYANFS_RB_FOREACH_SAFE(f, cyanfs_files_id_rb, &t->compact.files_by_id, next_f)
	{
		CYANFS_RB_FOREACH_SAFE(n, cyanfs_file_extents_rb, &f->extents, next_n)
		{
			CYANFS_RB_REMOVE(cyanfs_file_extents_rb, &f->extents, n);
			cyanfs_free(n);
		}
		CYANFS_RB_REMOVE(cyanfs_files_id_rb, &t->compact.files_by_id, f);
		CYANFS_RB_REMOVE(cyanfs_files_name_rb, &t->compact.files_by_name, f);
		cyanfs_free(f);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &t->compact.journal_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &t->compact.journal_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &t->compact.free_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &t->compact.free_extents, n);
		cyanfs_free(n);
	}
	if (t->compact.journal_cursor.page)
		cyanfs_free(t->compact.journal_cursor.page);
	cyanfs_free(t);
}

static void cyanfs_journal_compact_error(struct cyanfs_task_journal_compact *t)
{
	struct cyanfs_super *s = t->base.super;
	cyanfs_journal_compact_free(t);
	cyanfs_super_mark_error(s);
}

static void cyanfs_journal_compact_finish(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_super *s = base->super;
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(base, struct cyanfs_task_journal_compact, base);
	struct cyanfs_extent_node *compact_n;

	if (err) {
		cyanfs_journal_compact_error(t);
		return;
	}

	cyanfs_write_lock(&s->files_lock);
	CYANFS_RB_FOREACH(compact_n, cyanfs_backend_extents_rb, &t->compact.journal_extents)
	{
		struct cyanfs_extent_node *super_n;
		super_n = CYANFS_RB_FIND(cyanfs_backend_extents_rb, &s->journal_extents, compact_n);
		if (!super_n) {
			cyanfs_write_unlock(&s->files_lock);
			CYANFS_DEBUG("journal extent is not found.");
			cyanfs_journal_compact_error(t);
			return;
		}
		__cyanfs_super_discard_extent(s, super_n, &s->journal_extents);
	}
	s->flags &= ~CYANFS_SUPER_FLAG_COMPACT_WORKING;
	cyanfs_write_unlock(&s->files_lock);
	cyanfs_journal_compact_free(t);
}

static void cyanfs_journal_compact_flush_super(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_super *s = base->super;
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(base, struct cyanfs_task_journal_compact, base);
	if (err) {
		cyanfs_journal_compact_error(t);
		return;
	}
	cyanfs_task_init(s, &t->base, CYANFS_TASK_FLUSH, cyanfs_journal_compact_finish);
	cyanfs_super_task_add_tail(&t->base);
}

static void cyanfs_journal_compact_write_super(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_super *s = base->super;
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(base, struct cyanfs_task_journal_compact, base);
	uint8_t *p = t->compact.journal_cursor.page;
	if (err) {
		cyanfs_journal_compact_error(t);
		return;
	}
	cyanfs_super_encode_header(p, &t->header);
	t->header.crc32 =
		cyanfs_crc32(~0, p + sizeof(t->header.crc32), CYANFS_SUPER_HEADER_SIZE - sizeof(t->header.crc32));
	cyanfs_super_encode_header(p, &t->header);
	cyanfs_task_init(s, &t->base, CYANFS_TASK_WRITE, cyanfs_journal_compact_flush_super);
	t->base.write.buf = p;
	t->base.write.b_off = (t->header.version & 1) * CYANFS_SUPER_HEADER_SIZE;
	t->base.write.len = CYANFS_SUPER_HEADER_SIZE;
	cyanfs_super_task_add_tail(&t->base);
}

static void cyanfs_journal_compact_write_journal_finish(struct cyanfs_task_journal_writer *writer)
{
	struct cyanfs_super *s = writer->base.super;
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(writer, struct cyanfs_task_journal_compact, writer);
	cyanfs_task_init(s, &t->base, CYANFS_TASK_FLUSH, cyanfs_journal_compact_write_super);
	cyanfs_super_task_add_tail(&t->base);
}

static void cyanfs_journal_compact_write_journal_error(struct cyanfs_task_journal_writer *writer)
{
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(writer, struct cyanfs_task_journal_compact, writer);
	cyanfs_journal_compact_error(t);
}

static cyanfs_status cyanfs_journal_compact_loader_parser(struct cyanfs_task_journal_loader *loader,
							  struct cyanfs_journal_entry *j)
{
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(loader, struct cyanfs_task_journal_compact, loader);
	return __cyanfs_super_journal_replay(&t->compact, j);
}

static int cyanfs_journal_compact_count_entry(cyanfs_journal_seq_t *count)
{
	if (*count == ~(cyanfs_journal_seq_t)0)
		return 1;
	++*count;
	return 0;
}

static void cyanfs_journal_compact_loader_error(struct cyanfs_task_journal_loader *loader)
{
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(loader, struct cyanfs_task_journal_compact, loader);
	cyanfs_journal_compact_error(t);
}

static void cyanfs_journal_compact_loader_finish(struct cyanfs_task_journal_loader *loader)
{
	struct cyanfs_task_journal_compact *t = cyanfs_container_of(loader, struct cyanfs_task_journal_compact, loader);
	struct cyanfs_super *s = loader->base.super;
	struct cyanfs_file *f;
	struct cyanfs_extent_node *n;
	struct cyanfs_journal_entry *j;
	cyanfs_journal_seq_t count = 0;

	cyanfs_journal_writer_init(&t->writer);
	CYANFS_RB_FOREACH(f, cyanfs_files_id_rb, &t->compact.files_by_id)
	{
		int truncate;
		if (f->parent) {
			j = cyanfs_journal_alloc(CYANFS_JOURNAL_FORK);
			if (!j)
				goto err;
			*j->fork.name = f->meta.name;
			j->fork.id = f->meta.id;
			j->fork.pid = f->meta.parent_id;
			cyanfs_list_add_tail(&j->list, &t->writer.head);
			if (cyanfs_journal_compact_count_entry(&count))
				goto err;
			truncate = f->parent->meta.size != f->meta.size;
		} else {
			j = cyanfs_journal_alloc(CYANFS_JOURNAL_CREATE);
			if (!j)
				goto err;
			*j->create.name = f->meta.name;
			j->create.id = f->meta.id;
			cyanfs_list_add_tail(&j->list, &t->writer.head);
			if (cyanfs_journal_compact_count_entry(&count))
				goto err;
			truncate = f->meta.size > 0;
		}
		if (truncate) {
			j = cyanfs_journal_alloc(CYANFS_JOURNAL_TRUNCATE);
			if (!j)
				goto err;
			j->truncate.id = f->meta.id;
			j->truncate.size = f->meta.size;
			cyanfs_list_add_tail(&j->list, &t->writer.head);
			if (cyanfs_journal_compact_count_entry(&count))
				goto err;
		}
		CYANFS_RB_FOREACH(n, cyanfs_file_extents_rb, &f->extents)
		{
			j = cyanfs_journal_alloc(CYANFS_JOURNAL_BIND);
			if (!j)
				goto err;
			j->bind.id = f->meta.id;
			j->bind.file = n->v.file;
			j->bind.backend = n->v.backend;
			cyanfs_list_add_tail(&j->list, &t->writer.head);
			if (cyanfs_journal_compact_count_entry(&count))
				goto err;
		}
	}

	t->header = loader->header;
	if (count > t->compact.journal_cursor.seq) {
		CYANFS_DEBUG("compact journal entry count exceeds sequence.");
		goto err;
	}
	if (t->header.version == ~(uint64_t)0) {
		CYANFS_DEBUG("compact super header version is exhausted.");
		goto err;
	}
	t->header.journal_seq = t->compact.journal_cursor.seq - count;
	++t->header.version;

	if (count) {
		cyanfs_write_lock(&s->files_lock);
		n = __cyanfs_super_find_nearly_extent(s, 0);
		if (!n) {
			cyanfs_write_unlock(&s->files_lock);
			cyanfs_journal_compact_loader_error(loader);
			return;
		}
		__cyanfs_extent_super_to_super(s, n, &s->free_extents, &s->journal_extents);
		t->header.journal_id = n->v.backend;
		cyanfs_write_unlock(&s->files_lock);

		t->compact.journal_cursor.extent_id = t->header.journal_id;
		t->compact.journal_cursor.extent_off = 0;
		t->compact.journal_cursor.seq = t->header.journal_seq;

		t->writer.next_id = t->loader.end_id;
		t->writer.cursor = &t->compact.journal_cursor;
		t->writer.operations.error = cyanfs_journal_compact_write_journal_error;
		t->writer.operations.finish = cyanfs_journal_compact_write_journal_finish;
		cyanfs_journal_writer_start(s, &t->writer);
	} else {
		t->header.journal_id = t->loader.end_id;
		cyanfs_task_init(s, &t->base, CYANFS_TASK_NOP, cyanfs_journal_compact_write_super);
		cyanfs_super_task_add_tail(&t->base);
	}

	return;

err:
	cyanfs_journal_compact_error(t);
}

void cyanfs_super_compact(struct cyanfs_super *s)
{
	struct cyanfs_task_journal_compact *t;
	struct cyanfs_extent_node *n;
	cyanfs_extent_id end_id;
	int i;

	if (!(s->flags & CYANFS_SUPER_FLAG_COMPACT_ENABLED))
		return;

	cyanfs_write_lock(&s->files_lock);
	if (s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING) {
		cyanfs_write_unlock(&s->files_lock);
		return;
	}
	s->flags |= CYANFS_SUPER_FLAG_COMPACT_WORKING;
	end_id = s->journal_cursor.extent_id_ondisk;
	cyanfs_write_unlock(&s->files_lock);

	t = cyanfs_malloc(sizeof(struct cyanfs_task_journal_compact));
	if (!t)
		goto err;
	t->base.super = s;

	t->compact.journal_cursor.page = cyanfs_malloc(CYANFS_JOURNAL_PAGE_SIZE);
	if (!t->compact.journal_cursor.page) {
		cyanfs_free(t);
		goto err;
	}

	CYANFS_DEBUG("compact start");

	cyanfs_journal_writer_init(&t->writer);
	CYANFS_RB_INIT(&t->compact.files_by_id);
	CYANFS_RB_INIT(&t->compact.files_by_name);
	CYANFS_RB_INIT(&t->compact.free_extents);
	CYANFS_RB_INIT(&t->compact.journal_extents);
	t->compact.max_file_id = 0;
	t->compact.flags = 0;
	for (i = 0; i < s->meta.total_extents; i++) {
		n = cyanfs_malloc(sizeof(struct cyanfs_extent_node));
		if (!n)
			goto task_out;
		n->v.map = n->v.pending = n->v.error = 0;
		n->v.backend = i;
		CYANFS_RB_INSERT(cyanfs_backend_extents_rb, &t->compact.free_extents, n);
	}

	cyanfs_journal_loader_init(&t->loader);
	t->loader.end_id = end_id;
	t->loader.cursor = &t->compact.journal_cursor;
	t->loader.operations.error = cyanfs_journal_compact_loader_error;
	t->loader.operations.finish = cyanfs_journal_compact_loader_finish;
	t->loader.operations.parser = cyanfs_journal_compact_loader_parser;
	if (cyanfs_journal_loader_start(s, &t->loader))
		cyanfs_super_new_task(s);

	return;

task_out:
	cyanfs_journal_compact_free(t);
err:
	cyanfs_super_mark_error(s);
}
