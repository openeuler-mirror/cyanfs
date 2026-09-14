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

#ifdef CYANFS_DEBUG_ENABLED
int debug_enable = 1;
cyanfs_atomic_t debug_malloc_counter = { 0 };
#endif

struct cyanfs_extent_node *__cyanfs_super_find_nearly_extent(struct cyanfs_super *s, cyanfs_extent_id id_from)
{
	struct cyanfs_extent_node cmp, *n;

	if (id_from <= CYANFS_EXTENT_INDEX_MAX) {
		cmp.v.backend = id_from;
		n = CYANFS_RB_NFIND(cyanfs_backend_extents_rb, &s->free_extents, &cmp);
		if (n)
			return n;
	}
	return CYANFS_RB_MIN(cyanfs_backend_extents_rb, &s->free_extents);
}

struct cyanfs_extent_node *__cyanfs_super_find_extent(struct cyanfs_super *s, cyanfs_extent_id id)
{
	struct cyanfs_extent_node cmp;

	if (id > CYANFS_EXTENT_INDEX_MAX)
		return NULL;
	cmp.v.backend = id;
	return CYANFS_RB_FIND(cyanfs_backend_extents_rb, &s->free_extents, &cmp);
}

void __cyanfs_file_map_extent(struct cyanfs_file *f, struct cyanfs_extent_node *n, cyanfs_extent_id f_id)
{
	struct cyanfs_super *s = f->super;
	n->v.file = f_id;
	__cyanfs_extent_super_to_file(s, f, n, &s->free_extents, &f->extents);
	n->v.map = 1;
}

void __cyanfs_file_unmap_extent(struct cyanfs_file *f, struct cyanfs_extent_node *n)
{
	struct cyanfs_super *s = f->super;
	CYANFS_BUG_ON(n->v.pending);
	n->v.map = n->v.pending = n->v.error = 0;
	if (s->flags & CYANFS_SUPER_FLAG_READY) {
		__cyanfs_extent_file_to_super(s, f, n, &f->extents, &s->unused_extents);
	} else {
		__cyanfs_extent_file_to_super(s, f, n, &f->extents, &s->free_extents);
	}
}

struct cyanfs_extent_node *__cyanfs_file_find_extent(struct cyanfs_file *f, uint64_t f_off)
{
	struct cyanfs_extent_node tmp;
	tmp.v.file = cyanfs_extent_from(f_off);
	return CYANFS_RB_FIND(cyanfs_file_extents_rb, &f->extents, &tmp);
}

struct cyanfs_extent_node *__cyanfs_file_find_extent_follow(struct cyanfs_file *f, uint64_t f_off)
{
	struct cyanfs_extent_node *n = NULL;

	while (f && cyanfs_extent_begin(cyanfs_extent_from(f_off)) < f->meta.size) {
		n = __cyanfs_file_find_extent(f, f_off);
		if (n)
			break;
		f = f->parent;
	}

	return n;
}

struct cyanfs_extent_node *__cyanfs_file_alloc_extent(struct cyanfs_file *f, uint64_t f_off)
{
	struct cyanfs_super *s = f->super;
	struct cyanfs_extent_node *n = NULL, tmp;
	cyanfs_extent_id id = cyanfs_extent_from(f_off);
	cyanfs_extent_id id_from;

	if (s->meta.free_extents <= s->meta.reserved_extents)
		goto out;

	tmp.v.file = (id < CYANFS_EXTENT_CONTINUOUS) ? 0 : (id - CYANFS_EXTENT_CONTINUOUS);
	n = CYANFS_RB_NFIND(cyanfs_file_extents_rb, &f->extents, &tmp);
	if (n && n->v.file <= id + CYANFS_EXTENT_CONTINUOUS) {
		id_from = n->v.backend + id - n->v.file;
	} else {
		uint8_t hash[16];
		*(uint64_t *)hash = f->meta.id;
		*(uint64_t *)(hash + sizeof(uint64_t)) = f_off;
		id_from = cyanfs_crc32(~0, hash, 16) % s->meta.total_extents;
	}

	n = __cyanfs_super_find_nearly_extent(s, id_from);
	if (!n)
		goto out;
	__cyanfs_file_map_extent(f, n, id);

out:
	return n;
}

cyanfs_status __cyanfs_file_bind_extent(struct cyanfs_file *f, cyanfs_extent_id f_id, cyanfs_extent_id b_id)
{
	struct cyanfs_super *s = f->super;
	struct cyanfs_extent_node *n;

	if (f_id > CYANFS_EXTENT_INDEX_MAX || cyanfs_extent_begin(f_id) >= f->meta.size)
		return -CYANFS_ERR_INVAL;
	n = __cyanfs_super_find_extent(s, b_id);
	if (!n)
		return -CYANFS_ERR_INVAL;
	__cyanfs_file_map_extent(f, n, f_id);

	return 0;
}

cyanfs_status __cyanfs_file_unbind_extent(struct cyanfs_file *f, cyanfs_extent_id f_id)
{
	struct cyanfs_extent_node *n;

	if (f_id > CYANFS_EXTENT_INDEX_MAX || cyanfs_extent_begin(f_id) >= f->meta.size)
		return -CYANFS_ERR_INVAL;
	n = __cyanfs_file_find_extent(f, cyanfs_extent_begin(f_id));
	if (!n)
		return -CYANFS_ERR_INVAL;
	__cyanfs_file_unmap_extent(f, n);

	return 0;
}

static cyanfs_status __cyanfs_file_truncate(struct cyanfs_file *f, uint64_t size)
{
	struct cyanfs_extent_node *n;

	if (size) {
		struct cyanfs_extent_node tmp;
		tmp.v.file = cyanfs_extent_from(size - 1);
		n = CYANFS_RB_NFIND(cyanfs_file_extents_rb, &f->extents, &tmp);
		while (n) {
			struct cyanfs_extent_node *next = CYANFS_RB_NEXT(cyanfs_file_extents_rb, &f->extents, n);
			if (n->v.file > tmp.v.file)
				__cyanfs_file_unmap_extent(f, n);
			n = next;
		}
	} else {
		n = CYANFS_RB_MIN(cyanfs_file_extents_rb, &f->extents);
		while (n) {
			struct cyanfs_extent_node *next = CYANFS_RB_NEXT(cyanfs_file_extents_rb, &f->extents, n);
			__cyanfs_file_unmap_extent(f, n);
			n = next;
		}
	}
	f->meta.size = size;

	return 0;
}

static struct cyanfs_file *__cyanfs_lookup_file_by_name(struct cyanfs_super *s, cyanfs_file_name_t name)
{
	struct cyanfs_file tmp;
	tmp.meta.name = name;
	return CYANFS_RB_FIND(cyanfs_files_name_rb, &s->files_by_name, &tmp);
}

static struct cyanfs_file *__cyanfs_lookup_file_by_id(struct cyanfs_super *s, cyanfs_file_id_t id)
{
	struct cyanfs_file tmp;
	tmp.meta.id = id;
	return CYANFS_RB_FIND(cyanfs_files_id_rb, &s->files_by_id, &tmp);
}

static void __cyanfs_init_file(struct cyanfs_super *s, struct cyanfs_file *file, cyanfs_file_name_t name,
			       cyanfs_file_id_t id, struct cyanfs_file *parent)
{
	file->meta.name = name;
	file->meta.id = id;
	file->meta.extents = 0;
	file->meta.size = parent ? parent->meta.size : 0;
	file->meta.parent_id = parent ? parent->meta.id : 0;
	file->meta.children = 0;
	file->super = s;
	file->parent = parent;
	file->open_type = CYANFS_FILE_CLOSED;
	file->reader = 0;
	CYANFS_RB_INIT(&file->extents);
	if (parent)
		++parent->meta.children;
	if (!file->meta.id)
		file->meta.id = ++s->max_file_id;
	else if (file->meta.id > s->max_file_id)
		s->max_file_id = file->meta.id;
	CYANFS_RB_INSERT(cyanfs_files_id_rb, &s->files_by_id, file);
	CYANFS_RB_INSERT(cyanfs_files_name_rb, &s->files_by_name, file);
	++s->meta.files;
}

static cyanfs_status __cyanfs_rename_file(struct cyanfs_file *f, cyanfs_file_name_t name)
{
	struct cyanfs_super *s = f->super;
	CYANFS_RB_REMOVE(cyanfs_files_name_rb, &s->files_by_name, f);
	f->meta.name = name;
	CYANFS_RB_INSERT(cyanfs_files_name_rb, &s->files_by_name, f);
	return 0;
}

static cyanfs_status __cyanfs_delete_file(struct cyanfs_super *s, struct cyanfs_file *file)
{
	cyanfs_status err;
	err = __cyanfs_file_truncate(file, 0);
	if (err < 0)
		return err;
	CYANFS_RB_REMOVE(cyanfs_files_id_rb, &s->files_by_id, file);
	CYANFS_RB_REMOVE(cyanfs_files_name_rb, &s->files_by_name, file);
	--s->meta.files;
	if (file->parent)
		--file->parent->meta.children;
	cyanfs_free(file); // free函数不会睡眠
	return 0;
}

void __cyanfs_super_journal_append(struct cyanfs_super *s, struct cyanfs_journal_entry *j)
{
	cyanfs_list_add_tail(&j->list, &s->journal_head);
	++s->journal_count;
}

cyanfs_status __cyanfs_super_journal_replay(struct cyanfs_super *s, struct cyanfs_journal_entry *j)
{
	cyanfs_file_name_t name;
	cyanfs_status err;
	struct cyanfs_file *f, *p;
	struct cyanfs_extent_node *n;

	switch (j->type) {
	case CYANFS_JOURNAL_CREATE:
		name = *j->create.name;
		err = __cyanfs_file_name_normalize(&name);
		if (err || __cyanfs_lookup_file_by_name(s, name))
			return -CYANFS_ERR_INVAL;
		f = cyanfs_malloc(sizeof(struct cyanfs_file));
		if (!f)
			return -CYANFS_ERR_NOMEM;
		__cyanfs_init_file(s, f, name, j->create.id, NULL);
		return 0;
	case CYANFS_JOURNAL_TRUNCATE:
		err = __cyanfs_file_size_validate(j->truncate.size);
		if (err)
			return err;
		f = __cyanfs_lookup_file_by_id(s, j->truncate.id);
		if (!f)
			return -CYANFS_ERR_INVAL;
		return __cyanfs_file_truncate(f, j->truncate.size);
	case CYANFS_JOURNAL_FORK:
		name = *j->fork.name;
		err = __cyanfs_file_name_normalize(&name);
		if (err || __cyanfs_lookup_file_by_name(s, name))
			return -CYANFS_ERR_INVAL;
		p = __cyanfs_lookup_file_by_id(s, j->fork.pid);
		if (!p)
			return -CYANFS_ERR_INVAL;
		f = cyanfs_malloc(sizeof(struct cyanfs_file));
		if (!f)
			return -CYANFS_ERR_NOMEM;
		__cyanfs_init_file(s, f, name, j->fork.id, p);
		return 0;
	case CYANFS_JOURNAL_DELETE:
		f = __cyanfs_lookup_file_by_id(s, j->delete.id);
		if (!f)
			return -CYANFS_ERR_INVAL;
		return __cyanfs_delete_file(s, f);
	case CYANFS_JOURNAL_BIND:
		f = __cyanfs_lookup_file_by_id(s, j->bind.id);
		if (!f)
			return -CYANFS_ERR_INVAL;
		return __cyanfs_file_bind_extent(f, j->bind.file, j->bind.backend);
	case CYANFS_JOURNAL_UNBIND:
		f = __cyanfs_lookup_file_by_id(s, j->unbind.id);
		if (!f)
			return -CYANFS_ERR_INVAL;
		return __cyanfs_file_unbind_extent(f, j->unbind.file);
	case CYANFS_JOURNAL_RENAME:
		name = *j->rename.name;
		err = __cyanfs_file_name_normalize(&name);
		if (err)
			return err;
		f = __cyanfs_lookup_file_by_id(s, j->rename.id);
		if (!f)
			return -CYANFS_ERR_INVAL;
		p = __cyanfs_lookup_file_by_name(s, name);
		if (p && p != f)
			return -CYANFS_ERR_INVAL;
		return __cyanfs_rename_file(f, name);
	case CYANFS_JOURNAL_NEXT:
		n = __cyanfs_super_find_extent(s, j->next.backend);
		if (!n)
			return -CYANFS_ERR_INVAL;
		__cyanfs_extent_super_to_super(s, n, &s->free_extents, &s->journal_extents);
		return 0;
	default:
		return -CYANFS_ERR_INVAL;
	}
}

void cyanfs_super_set_new_task_callback(struct cyanfs_super *s, void *ctx, cyanfs_ctx_fn fn)
{
	s->ctx = ctx;
	s->new_task = fn;
}

int cyanfs_super_is_ready(struct cyanfs_super *s)
{
	return s->flags & CYANFS_SUPER_FLAG_READY;
}

cyanfs_status cyanfs_super_status(struct cyanfs_super *s)
{
	cyanfs_status err;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	cyanfs_read_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_file_status(struct cyanfs_file *f)
{
	return cyanfs_super_status(f->super);
}

struct cyanfs_task *cyanfs_super_get_task(struct cyanfs_super *s)
{
	struct cyanfs_task *t = NULL;

	cyanfs_lock(&s->task_lock);
	if (!cyanfs_list_empty(&s->task_head)) {
		t = cyanfs_list_first_entry(&s->task_head, struct cyanfs_task, list);
		cyanfs_list_del(&t->list);
	}
	cyanfs_unlock(&s->task_lock);

	return t;
}

cyanfs_status cyanfs_super_flush(struct cyanfs_super *s, int ondisk)
{
	return cyanfs_super_flusher_start(s, 0, ondisk);
}

static cyanfs_status cyanfs_super_open_parser(struct cyanfs_task_journal_loader *t, struct cyanfs_journal_entry *j)
{
	struct cyanfs_super *s = t->base.super;
	return __cyanfs_super_journal_replay(s, j);
}

static void cyanfs_super_open_finish(struct cyanfs_task_journal_loader *t)
{
	struct cyanfs_super *s = t->base.super;

	s->flags |= CYANFS_SUPER_FLAG_READY;
	s->journal_cursor.extent_id = s->journal_cursor.extent_id_ondisk = t->cursor->extent_id;
	s->meta.uuid = t->header.uuid;

	cyanfs_free(t);
}

static void cyanfs_super_open_error(struct cyanfs_task_journal_loader *t)
{
	cyanfs_super_mark_error(t->base.super);
	cyanfs_free(t);
}

struct cyanfs_super *cyanfs_super_open(uint64_t size, int discard, int compact)
{
	cyanfs_extent_id i;
	struct cyanfs_extent_node *n, *tmp;
	struct cyanfs_super *s;
	struct cyanfs_task_journal_loader *t;

	if (size < CYANFS_SUPER_BLOCK_SIZE + CYANFS_EXTENT_SIZE ||
	    size - CYANFS_SUPER_BLOCK_SIZE > CYANFS_FILE_MAX_SIZE)
		goto out;

	s = cyanfs_malloc(sizeof(struct cyanfs_super));
	if (!s)
		goto out;

	cyanfs_init_rwlock(&s->files_lock);
	CYANFS_RB_INIT(&s->files_by_id);
	CYANFS_RB_INIT(&s->files_by_name);
	CYANFS_RB_INIT(&s->free_extents);
	CYANFS_RB_INIT(&s->discard_extents);
	CYANFS_RB_INIT(&s->wait_discard_extents);
	CYANFS_RB_INIT(&s->flush_extents);
	CYANFS_RB_INIT(&s->unused_extents);
	CYANFS_RB_INIT(&s->journal_extents);
	CYANFS_INIT_LIST_HEAD(&s->journal_head);
	s->journal_count = 0;
	cyanfs_init_lock(&s->task_lock);
	CYANFS_INIT_LIST_HEAD(&s->task_head);
	s->max_file_id = 0;
	s->new_task = NULL;
	s->ctx = NULL;
	s->flags = 0;
	cyanfs_atomic_init(&s->writers[0], 0);
	cyanfs_atomic_init(&s->writers[1], 0);
	s->writer_current_id = 0;

	CYANFS_BUILD_BUG_ON(sizeof(struct cyanfs_extent) != 8);

	if (discard)
		s->flags |= CYANFS_SUPER_FLAG_DISCARD_ENABLED;
	if (compact)
		s->flags |= CYANFS_SUPER_FLAG_COMPACT_ENABLED;

	s->meta.total_extents = cyanfs_extent_from(size - CYANFS_SUPER_BLOCK_SIZE);
	for (i = 0; i < s->meta.total_extents; i++) {
		n = cyanfs_malloc(sizeof(struct cyanfs_extent_node));
		if (!n)
			goto extents_alloc;
		n->v.map = n->v.pending = n->v.error = 0;
		n->v.backend = i;
		CYANFS_RB_INSERT(cyanfs_backend_extents_rb, &s->free_extents, n);
	}
	s->meta.free_extents = s->meta.total_extents;
	s->meta.journal_extents = 0;
	s->meta.size = size;
	s->meta.files = 0;
	s->meta.reserved_extents = ({
		uint64_t journal_size = cyanfs_journal_entry_size(CYANFS_JOURNAL_FORK) * CYANFS_SUPER_MAX_FILES +
					cyanfs_journal_entry_size(CYANFS_JOURNAL_BIND) * s->meta.total_extents;
		(journal_size / CYANFS_EXTENT_SIZE + 1) * 8;
	});
	s->journal_cursor.page = cyanfs_malloc(CYANFS_JOURNAL_PAGE_SIZE);
	if (!s->journal_cursor.page)
		goto extents_alloc;

	t = cyanfs_malloc(sizeof(struct cyanfs_task_journal_loader));
	if (!t)
		goto page_out;
	cyanfs_journal_loader_init(t);
	t->cursor = &s->journal_cursor;
	t->operations.error = cyanfs_super_open_error;
	t->operations.finish = cyanfs_super_open_finish;
	t->operations.parser = cyanfs_super_open_parser;
	if (cyanfs_journal_loader_start(s, t))
		cyanfs_super_new_task(s);

	return s;

page_out:
	cyanfs_free(s->journal_cursor.page);
extents_alloc:
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->free_extents, tmp)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->free_extents, n);
		cyanfs_free(n);
	}
	cyanfs_free(s);
out:
	return NULL;
}

void cyanfs_super_close(struct cyanfs_super *s)
{
	struct cyanfs_file *f, *next_f;
	struct cyanfs_extent_node *n, *next_n;
	struct cyanfs_list_head *p, *tmp;

	cyanfs_free(s->journal_cursor.page);
	CYANFS_BUG_ON(!cyanfs_list_empty(&s->task_head));
	cyanfs_list_for_each_safe(p, tmp, &s->journal_head)
	{
		struct cyanfs_journal_entry *j = cyanfs_container_of(p, struct cyanfs_journal_entry, list);
		cyanfs_journal_free(j);
	}
	CYANFS_RB_FOREACH_SAFE(f, cyanfs_files_id_rb, &s->files_by_id, next_f)
	{
		f->parent = NULL;
		__cyanfs_delete_file(s, f);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->discard_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->discard_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->wait_discard_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->wait_discard_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->unused_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->unused_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->flush_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->flush_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->journal_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->journal_extents, n);
		cyanfs_free(n);
	}
	CYANFS_RB_FOREACH_SAFE(n, cyanfs_backend_extents_rb, &s->free_extents, next_n)
	{
		CYANFS_RB_REMOVE(cyanfs_backend_extents_rb, &s->free_extents, n);
		cyanfs_free(n);
	}
	cyanfs_free(s);
}

cyanfs_status cyanfs_statfs(struct cyanfs_super *s, struct cyanfs_super_meta *stat)
{
	cyanfs_status err;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	*stat = s->meta;
unlock:
	cyanfs_read_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_stat(struct cyanfs_super *s, cyanfs_file_id_t id, struct cyanfs_file_meta *stat)
{
	cyanfs_status err;
	struct cyanfs_file *f;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = __cyanfs_lookup_file_by_id(s, id);
	if (!f) {
		err = -CYANFS_ERR_NOENT;
		goto unlock;
	}
	*stat = f->meta;
unlock:
	cyanfs_read_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_list(struct cyanfs_super *s, struct cyanfs_file_meta *iter)
{
	cyanfs_status err;
	struct cyanfs_file tmp, *f;
	tmp.meta.id = iter->id + 1;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = CYANFS_RB_NFIND(cyanfs_files_id_rb, &s->files_by_id, &tmp);
	if (f) {
		*iter = f->meta;
	} else {
		err = -CYANFS_ERR_NOENT;
	}
unlock:
	cyanfs_read_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_lookup(struct cyanfs_super *s, cyanfs_file_name_t name, struct cyanfs_file_meta *meta)
{
	cyanfs_status err;
	struct cyanfs_file *f;

	err = __cyanfs_file_name_normalize(&name);
	if (err)
		return err;

	cyanfs_read_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = __cyanfs_lookup_file_by_name(s, name);
	if (!f) {
		err = -CYANFS_ERR_NOENT;
		goto unlock;
	}
	*meta = f->meta;
unlock:
	cyanfs_read_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_open(struct cyanfs_super *s, cyanfs_file_id_t id, int write, struct cyanfs_file **out)
{
	cyanfs_status err;
	struct cyanfs_file *f;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = __cyanfs_lookup_file_by_id(s, id);
	if (!f) {
		err = -CYANFS_ERR_NOENT;
	} else if (write) {
		if (f->open_type != CYANFS_FILE_CLOSED || f->meta.children) {
			err = -CYANFS_ERR_BUSY;
		} else {
			f->open_type = CYANFS_FILE_READWRITE;
			*out = f;
		}
	} else {
		if (f->open_type == CYANFS_FILE_READWRITE) {
			err = -CYANFS_ERR_BUSY;
		} else {
			f->open_type = CYANFS_FILE_READONLY;
			++f->reader;
			*out = f;
		}
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	return err;
}

cyanfs_status cyanfs_create(struct cyanfs_super *s, cyanfs_file_name_t name, cyanfs_file_id_t *id)
{
	cyanfs_status err = -CYANFS_ERR_NOMEM;
	struct cyanfs_file *f = NULL;
	struct cyanfs_journal_entry *j = NULL;

	err = __cyanfs_file_name_normalize(&name);
	if (err)
		return err;

	f = cyanfs_malloc(sizeof(struct cyanfs_file));
	if (!f)
		goto out;

	j = cyanfs_journal_alloc(CYANFS_JOURNAL_CREATE);
	if (!j)
		goto out;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	if (s->meta.files >= CYANFS_SUPER_MAX_FILES) {
		err = -CYANFS_ERR_NOSPACE;
		goto unlock;
	}
	if (__cyanfs_lookup_file_by_name(s, name)) {
		err = -CYANFS_ERR_EXIST;
	} else {
		__cyanfs_init_file(s, f, name, 0, NULL);
		*j->create.name = name;
		j->create.id = f->meta.id;
		__cyanfs_super_journal_append(s, j);
		err = 0;
		if (id)
			*id = f->meta.id;
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_flush_new_journal(s);

out:
	if (err) {
		if (j)
			cyanfs_journal_free(j);
		if (f)
			cyanfs_free(f);
	}

	return err;
}

cyanfs_status cyanfs_fork(struct cyanfs_super *s, cyanfs_file_id_t from, cyanfs_file_name_t name_to,
			  cyanfs_file_id_t *id_to)
{
	cyanfs_status err = -CYANFS_ERR_NOMEM;
	struct cyanfs_file *f = NULL, *p;
	struct cyanfs_journal_entry *j = NULL;

	err = __cyanfs_file_name_normalize(&name_to);
	if (err)
		return err;

	f = cyanfs_malloc(sizeof(struct cyanfs_file));
	if (!f)
		goto out;

	j = cyanfs_journal_alloc(CYANFS_JOURNAL_FORK);
	if (!j)
		goto out;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	if (s->meta.files >= CYANFS_SUPER_MAX_FILES) {
		err = -CYANFS_ERR_NOSPACE;
		goto unlock;
	}
	p = __cyanfs_lookup_file_by_id(s, from);
	if (!p) {
		err = -CYANFS_ERR_NOENT;
	} else if (p->open_type == CYANFS_FILE_READWRITE) {
		err = -CYANFS_ERR_BUSY;
	} else if (__cyanfs_lookup_file_by_name(s, name_to)) {
		err = -CYANFS_ERR_EXIST;
	} else {
		__cyanfs_init_file(s, f, name_to, 0, p);
		*j->fork.name = name_to;
		j->fork.id = f->meta.id;
		j->fork.pid = p->meta.id;
		__cyanfs_super_journal_append(s, j);
		err = 0;
		if (id_to)
			*id_to = f->meta.id;
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_flush_new_journal(s);

out:
	if (err) {
		if (j)
			cyanfs_journal_free(j);
		if (f)
			cyanfs_free(f);
	}

	return err;
}

cyanfs_status cyanfs_rename(struct cyanfs_super *s, cyanfs_file_id_t id, cyanfs_file_name_t name)
{
	cyanfs_status err;
	struct cyanfs_file *f;
	struct cyanfs_journal_entry *j;

	err = __cyanfs_file_name_normalize(&name);
	if (err)
		return err;

	j = cyanfs_journal_alloc(CYANFS_JOURNAL_RENAME);
	if (!j)
		return -CYANFS_ERR_NOMEM;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	if (__cyanfs_lookup_file_by_name(s, name)) {
		err = -CYANFS_ERR_EXIST;
	} else if ((f = __cyanfs_lookup_file_by_id(s, id)) == NULL) {
		err = -CYANFS_ERR_NOENT;
	} else {
		j->rename.id = f->meta.id;
		*j->rename.name = name;
		err = __cyanfs_rename_file(f, name);
		if (!err)
			__cyanfs_super_journal_append(s, j);
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_flush_new_journal(s);

	if (err)
		cyanfs_journal_free(j);

	return err;
}

cyanfs_status cyanfs_truncate(struct cyanfs_super *s, cyanfs_file_id_t id, uint64_t size)
{
	cyanfs_status err;
	struct cyanfs_file *f;
	struct cyanfs_journal_entry *j;

	err = __cyanfs_file_size_validate(size);
	if (err)
		return err;

	j = cyanfs_journal_alloc(CYANFS_JOURNAL_TRUNCATE);
	if (!j)
		return -CYANFS_ERR_NOMEM;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = __cyanfs_lookup_file_by_id(s, id);
	if (!f) {
		err = -CYANFS_ERR_NOENT;
	} else if (f->open_type != CYANFS_FILE_CLOSED || f->meta.children) {
		err = -CYANFS_ERR_BUSY;
	} else {
		j->truncate.id = f->meta.id;
		j->truncate.size = size;
		j->flush = 1;
		err = __cyanfs_file_truncate(f, size);
		if (!err)
			__cyanfs_super_journal_append(s, j);
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_flush_new_journal(s);

	if (err)
		cyanfs_journal_free(j);

	return err;
}

cyanfs_status cyanfs_delete(struct cyanfs_super *s, cyanfs_file_id_t id)
{
	cyanfs_status err;
	struct cyanfs_file *f;
	struct cyanfs_journal_entry *j;

	j = cyanfs_journal_alloc(CYANFS_JOURNAL_DELETE);
	if (!j)
		return -CYANFS_ERR_NOMEM;

	cyanfs_write_lock(&s->files_lock);
	err = __cyanfs_super_ensure_status(s);
	if (err)
		goto unlock;
	f = __cyanfs_lookup_file_by_id(s, id);
	if (!f) {
		err = -CYANFS_ERR_NOENT;
	} else if (f->open_type != CYANFS_FILE_CLOSED || f->meta.children) {
		err = -CYANFS_ERR_BUSY;
	} else {
		j->delete.id = f->meta.id;
		err = __cyanfs_delete_file(s, f);
		if (!err)
			__cyanfs_super_journal_append(s, j);
	}
unlock:
	cyanfs_write_unlock(&s->files_lock);

	cyanfs_super_flush_new_journal(s);

	if (err)
		cyanfs_journal_free(j);

	return err;
}

void cyanfs_close(struct cyanfs_file *f)
{
	struct cyanfs_super *s = f->super;
	cyanfs_write_lock(&s->files_lock);
	if (f->open_type == CYANFS_FILE_CLOSED) {
		CYANFS_BUG_ON(1);
	} else if (f->open_type == CYANFS_FILE_READWRITE) {
		f->open_type = CYANFS_FILE_CLOSED;
	} else if (--f->reader == 0) {
		f->open_type = CYANFS_FILE_CLOSED;
	}
	cyanfs_write_unlock(&s->files_lock);
}

uint64_t cyanfs_size(struct cyanfs_file *f)
{
	return f->meta.size;
}
