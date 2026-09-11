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
int debug_dump_journal = 1;
#endif

static void cyanfs_journal_loader_end(struct cyanfs_task_journal_loader *t, int err)
{
	while (!cyanfs_list_empty(&t->journals)) {
		struct cyanfs_extent_set *n = cyanfs_list_first_entry(&t->journals, struct cyanfs_extent_set, node);

		if (!err) {
			int i;
			CYANFS_DEBUG("parse journal set");
			for (i = 0; i < n->count; i++) {
				struct cyanfs_journal_entry j;
				j.type = CYANFS_JOURNAL_NEXT;
				j.next.backend = n->id[i];
				cyanfs_journal_dump_entry(&j);
				if (t->operations.parser(t, &j)) {
					CYANFS_DEBUG("journal link error");
					err = 1;
					break;
				}
			}
		}

		cyanfs_list_del(&n->node);
		if (n != &t->static_journals)
			cyanfs_free(n);
	}
	if (err) {
		if (t->operations.error)
			t->operations.error(t);
	} else {
		if (t->operations.finish)
			t->operations.finish(t);
	}
}

static void cyanfs_journal_loader_parser_page(struct cyanfs_task *base, cyanfs_status err);

struct cyanfs_journal_record_info {
	uint8_t *entries;
	uint16_t regular_count;
	int has_next;
	cyanfs_extent_id next_id;
};

static void cyanfs_journal_loader_read_page(struct cyanfs_task_journal_loader *t)
{
	struct cyanfs_super *s = t->base.super;

	CYANFS_DEBUG_BUG_ON(t->cursor->extent_off > CYANFS_EXTENT_SIZE);
	cyanfs_task_init(s, &t->base, CYANFS_TASK_READ, cyanfs_journal_loader_parser_page);
	t->base.read.buf = t->cursor->page;
	t->base.read.b_off = cyanfs_map_extent_offset(t->cursor->extent_id, t->cursor->extent_off);
	t->base.read.len = CYANFS_EXTENT_SIZE - t->cursor->extent_off;
	if (t->base.read.len > CYANFS_JOURNAL_PAGE_SIZE)
		t->base.read.len = CYANFS_JOURNAL_PAGE_SIZE;

	cyanfs_super_task_add_tail(&t->base);
}

static void cyanfs_journal_loader_read_extent(struct cyanfs_task_journal_loader *t, cyanfs_extent_id b_id)
{
	struct cyanfs_extent_set *n = cyanfs_list_last_entry(&t->journals, struct cyanfs_extent_set, node);

	if (n->count >= CYANFS_EXTENT_SET_LIIMT) {
		n = cyanfs_malloc(sizeof(struct cyanfs_extent_set));
		if (!n) {
			CYANFS_INFO("alloc extent set failed.");
			cyanfs_journal_loader_end(t, 1);
			return;
		}
		n->count = 0;
		cyanfs_list_add_tail(&n->node, &t->journals);
	}
	n->id[n->count++] = b_id;

	t->cursor->extent_off = 0;
	t->cursor->extent_id = b_id;

	cyanfs_journal_loader_read_page(t);
}

static cyanfs_status cyanfs_journal_validate_record(uint8_t *p, uint8_t *record_end, uint16_t count,
					    struct cyanfs_journal_record_info *info)
{
	struct cyanfs_journal_entry j;
	int i;

	if (!count) {
		CYANFS_DEBUG("journal entry count is zero");
		return -CYANFS_ERR_INVAL;
	}

	info->entries = p;
	info->regular_count = 0;
	info->has_next = 0;
	info->next_id = cyanfs_extent_id_invalid;
	for (i = 0; i < count; i++) {
		if (cyanfs_journal_decode_entry_checked(&p, record_end, &j)) {
			CYANFS_DEBUG("journal entry decode error");
			return -CYANFS_ERR_INVAL;
		}
		if (j.type == CYANFS_JOURNAL_NEXT) {
			if (i != count - 1) {
				CYANFS_DEBUG("journal NEXT entry is not last");
				return -CYANFS_ERR_INVAL;
			}
			info->has_next = 1;
			info->next_id = j.next.backend;
		} else {
			++info->regular_count;
		}
	}

	if (record_end - p >= CYANFS_JOURNAL_BLOCK_SIZE) {
		CYANFS_DEBUG("journal padding is too large");
		return -CYANFS_ERR_INVAL;
	}

	return 0;
}

static void cyanfs_journal_loader_parser_page(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_task_journal_loader *t = cyanfs_container_of(base, struct cyanfs_task_journal_loader, base);
	struct cyanfs_journal_header h;
	struct cyanfs_journal_entry j;
	struct cyanfs_journal_record_info info;
	uint64_t offset_in_page = 0;
	uint8_t *page_end;

	if (err)
		goto error;

	page_end = (uint8_t *)base->read.buf + base->read.len;

	for (;;) {
		int i;
		uint8_t *p = t->cursor->page + offset_in_page;
		uint8_t *record_end;

		if (p >= page_end || cyanfs_journal_decode_header_checked(p, page_end - p, &h))
			break;
		CYANFS_DEBUG("parse journal from: %" PRIu32 ":%" PRIu64 " len: %" PRIu64 " count: %" PRIu32
			     " seq: %" PRIu64,
			     t->cursor->extent_id, t->cursor->extent_off, h.size, h.count, h.seq);
		if (h.size < CYANFS_JOURNAL_HEADER_SIZE || h.size > CYANFS_JOURNAL_PAGE_SIZE ||
		    t->cursor->extent_off + h.size > CYANFS_EXTENT_SIZE)
			break;

		if (h.size > (uint64_t)(page_end - p)) {
			if (!offset_in_page)
				break;
			cyanfs_journal_loader_read_page(t);
			return;
		}

		if (cyanfs_crc32(cyanfs_le32toh(t->header.uuid.data[0]), p + sizeof(h.crc32),
				 h.size - sizeof(h.crc32)) != h.crc32)
			break;

		if (h.seq != t->cursor->seq)
			break;

		record_end = p + h.size;
		p += CYANFS_JOURNAL_HEADER_SIZE;
		if (cyanfs_journal_validate_record(p, record_end, h.count, &info))
			goto error;
		if (!info.has_next && h.size == CYANFS_EXTENT_SIZE - t->cursor->extent_off) {
			CYANFS_DEBUG("journal record ends extent without NEXT");
			goto error;
		}
		if (info.regular_count > ~(cyanfs_journal_seq_t)0 - t->cursor->seq) {
			CYANFS_DEBUG("journal sequence overflow");
			goto error;
		}

		p = info.entries;
		for (i = 0; i < h.count; i++) {
			if (cyanfs_journal_decode_entry_checked(&p, record_end, &j)) {
				CYANFS_DEBUG("journal entry decode error after validation");
				goto error;
			}
			cyanfs_journal_dump_entry(&j);
			if (j.type != CYANFS_JOURNAL_NEXT && t->operations.parser(t, &j)) {
				CYANFS_DEBUG("journal entry error");
				goto error;
			}
		}
		t->cursor->seq += info.regular_count;

		if (info.has_next) {
			if (info.next_id != t->end_id) {
				cyanfs_journal_loader_read_extent(t, info.next_id);
				return;
			}
			goto done;
		}

		t->cursor->extent_off += h.size;
		offset_in_page += h.size;

		if (offset_in_page == CYANFS_JOURNAL_PAGE_SIZE) {
			cyanfs_journal_loader_read_page(t);
			return;
		}
	}

done:
	cyanfs_journal_loader_end(t, 0);
	return;

error:
	cyanfs_journal_loader_end(t, 1);
}

cyanfs_status cyanfs_super_parse(struct cyanfs_super_header *h, void *super_block)
{
	struct cyanfs_super_header h1, h2;
	uint8_t *p1 = super_block, *p2 = p1 + CYANFS_SUPER_HEADER_SIZE;
	uint32_t s1, s2;

	cyanfs_super_decode_header(p1, &h1);
	s1 = cyanfs_crc32(~0, p1 + sizeof(s1), CYANFS_SUPER_HEADER_SIZE - sizeof(s1));
	cyanfs_super_decode_header(p2, &h2);
	s2 = cyanfs_crc32(~0, p2 + sizeof(s2), CYANFS_SUPER_HEADER_SIZE - sizeof(s2));

	if (h1.crc32 == s1) {
		CYANFS_DEBUG("superblock1 crc ok, version: %" PRIu64, h1.version);
	}
	if (h2.crc32 == s2) {
		CYANFS_DEBUG("superblock2 crc ok, version: %" PRIu64, h2.version);
	}

	if (h1.crc32 == s1 && h2.crc32 == s2) {
		*h = (h2.version > h1.version) ? h2 : h1;
	} else if (h1.crc32 == s1) {
		*h = h1;
	} else if (h2.crc32 == s2) {
		*h = h2;
	} else {
		return -CYANFS_ERR_INVAL;
	}

	if (h->magic != CYANFS_SUPER_MAGIC)
		return -CYANFS_ERR_INVAL;

	return 0;
}

void cyanfs_super_make(cyanfs_uuid_t uuid, void *super_block)
{
	uint8_t *h1 = super_block, *h2 = h1 + CYANFS_SUPER_HEADER_SIZE;
	struct cyanfs_super_header h;
	h.journal_id = 0;
	h.journal_seq = 0;
	h.uuid = uuid;
	h.crc32 = 0;
	h.magic = CYANFS_SUPER_MAGIC;

	h.version = 0;
	cyanfs_super_encode_header(h1, &h);
	h.crc32 = cyanfs_crc32(~0, h1 + sizeof(h.crc32), CYANFS_SUPER_HEADER_SIZE - sizeof(h.crc32));
	cyanfs_super_encode_header(h1, &h);

	h.version = 1;
	cyanfs_super_encode_header(h2, &h);
	h.crc32 = cyanfs_crc32(~0, h2 + sizeof(h.crc32), CYANFS_SUPER_HEADER_SIZE - sizeof(h.crc32));
	cyanfs_super_encode_header(h2, &h);
}

static void cyanfs_journal_loader_parser_header(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_task_journal_loader *t = cyanfs_container_of(base, struct cyanfs_task_journal_loader, base);

	if (err)
		goto error;

	err = cyanfs_super_parse(&t->header, t->cursor->page);
	if (err)
		goto error;

	t->cursor->seq = t->header.journal_seq;

	if (t->header.journal_id == t->end_id) {
		cyanfs_journal_loader_end(t, 0);
		return;
	}

	cyanfs_journal_loader_read_extent(t, t->header.journal_id);
	return;

error:
	cyanfs_journal_loader_end(t, 1);
}

int cyanfs_journal_loader_start(struct cyanfs_super *s, struct cyanfs_task_journal_loader *t)
{
	CYANFS_BUILD_BUG_ON(CYANFS_SUPER_BLOCK_SIZE > CYANFS_JOURNAL_PAGE_SIZE);
	cyanfs_task_init(s, &t->base, CYANFS_TASK_READ, cyanfs_journal_loader_parser_header);
	t->base.read.buf = t->cursor->page;
	t->base.read.b_off = 0;
	t->base.read.len = CYANFS_SUPER_BLOCK_SIZE;
	return cyanfs_super_task_add_tail(&t->base);
}

static void cyanfs_journal_writer_encode(struct cyanfs_task *base, cyanfs_status err)
{
	struct cyanfs_task_journal_writer *t = cyanfs_container_of(base, struct cyanfs_task_journal_writer, base);
	struct cyanfs_super *s = t->base.super;
	struct cyanfs_list_head *node;
	uint8_t *p;
	int flush = 0;
	int all_entries_selected;
	int at_extent_end;
	int final_link;
	int need_next;
	uint16_t regular_count = 0;
	uint64_t extent_remaining;
	uint64_t record_capacity;
	uint64_t payload_limit;
	uint64_t used;
	uint64_t final_used;
	uint64_t padding;
	struct cyanfs_journal_header h;
	cyanfs_extent_id new_extent = cyanfs_extent_id_invalid;

	if (err) {
		if (t->operations.error)
			t->operations.error(t);
		return;
	}

	if (cyanfs_list_empty(&t->head)) {
		if (t->operations.finish)
			t->operations.finish(t);
		return;
	}

	if (!t->cursor || !t->cursor->page || t->cursor->extent_off >= CYANFS_EXTENT_SIZE ||
	    (t->cursor->extent_off & CYANFS_JOURNAL_BLOCK_MASK)) {
		CYANFS_INFO("invalid journal cursor.");
		goto error;
	}

	extent_remaining = CYANFS_EXTENT_SIZE - t->cursor->extent_off;
	record_capacity = extent_remaining;
	if (record_capacity > CYANFS_JOURNAL_PAGE_SIZE)
		record_capacity = CYANFS_JOURNAL_PAGE_SIZE;
	if (record_capacity < CYANFS_JOURNAL_BLOCK_SIZE || record_capacity < CYANFS_JOURNAL_TAIL_SIZE) {
		CYANFS_INFO("invalid journal record capacity.");
		goto error;
	}

	payload_limit = record_capacity - CYANFS_JOURNAL_TAIL_SIZE;
	used = CYANFS_JOURNAL_HEADER_SIZE;
	for (node = t->head.next; node != &t->head; node = node->next) {
		struct cyanfs_journal_entry *j = cyanfs_list_entry(node, struct cyanfs_journal_entry, list);
		uint64_t entry_size = cyanfs_journal_entry_size(j->type);

		if (!entry_size || j->type == CYANFS_JOURNAL_NEXT) {
			CYANFS_INFO("invalid journal entry.");
			goto error;
		}
		if (used > payload_limit || entry_size > payload_limit - used)
			break;
		used += entry_size;
		++regular_count;
		if (j->flush)
			flush = 1;
	}
	if (!regular_count) {
		CYANFS_INFO("journal record cannot fit an entry.");
		goto error;
	}
	if (regular_count > ~(cyanfs_journal_seq_t)0 - t->cursor->seq) {
		CYANFS_INFO("journal sequence overflow.");
		goto error;
	}

	all_entries_selected = node == &t->head;
	padding = (CYANFS_JOURNAL_BLOCK_SIZE - (used & CYANFS_JOURNAL_BLOCK_MASK)) &
		  CYANFS_JOURNAL_BLOCK_MASK;
	if (used > record_capacity || padding > record_capacity - used) {
		CYANFS_INFO("journal record exceeds capacity.");
		goto error;
	}
	at_extent_end = used + padding == extent_remaining;
	final_link = all_entries_selected && t->next_id != cyanfs_extent_id_invalid;
	need_next = at_extent_end || final_link;

	final_used = used;
	if (need_next) {
		if (CYANFS_JOURNAL_TAIL_SIZE > record_capacity - final_used) {
			CYANFS_INFO("journal NEXT exceeds capacity.");
			goto error;
		}
		final_used += CYANFS_JOURNAL_TAIL_SIZE;
	}
	padding = (CYANFS_JOURNAL_BLOCK_SIZE - (final_used & CYANFS_JOURNAL_BLOCK_MASK)) &
		  CYANFS_JOURNAL_BLOCK_MASK;
	if (padding > record_capacity - final_used) {
		CYANFS_INFO("journal padding exceeds capacity.");
		goto error;
	}
	h.size = final_used + padding;

	if (need_next) {
		if (final_link) {
			new_extent = t->next_id;
		} else {
			struct cyanfs_extent_node *n;

			cyanfs_write_lock(&s->files_lock);
			n = __cyanfs_super_find_nearly_extent(s, 0);
			if (!n) {
				__cyanfs_super_mark_error(s);
				cyanfs_write_unlock(&s->files_lock);
				CYANFS_INFO("alloc journal extent failed.");
				goto error;
			}
			__cyanfs_extent_super_to_super(s, n, &s->free_extents, &s->journal_extents);
			new_extent = n->v.backend;
			cyanfs_write_unlock(&s->files_lock);
		}
	}

	p = t->cursor->page + CYANFS_JOURNAL_HEADER_SIZE;
	h.count = 0;
	while (h.count < regular_count) {
		struct cyanfs_journal_entry *j =
			cyanfs_list_first_entry(&t->head, struct cyanfs_journal_entry, list);

		cyanfs_journal_encode_entry(&p, j);
		cyanfs_journal_dump_entry(j);
		++h.count;
		cyanfs_list_del(&j->list);
		cyanfs_journal_free(j);
	}

	if (need_next) {
		struct cyanfs_journal_entry j;

		j.type = CYANFS_JOURNAL_NEXT;
		j.next.backend = new_extent;
		cyanfs_journal_encode_entry(&p, &j);
		cyanfs_journal_dump_entry(&j);
		++h.count;
	}
	CYANFS_DEBUG_BUG_ON((uint64_t)(p - t->cursor->page) != final_used);

	h.seq = t->cursor->seq;
	t->cursor->seq += regular_count;
	h.crc32 = 0;
	cyanfs_journal_encode_header(t->cursor->page, &h);
	h.crc32 = cyanfs_crc32(cyanfs_le32toh(s->meta.uuid.data[0]), t->cursor->page + sizeof(h.crc32),
			       h.size - sizeof(h.crc32));
	cyanfs_journal_encode_header(t->cursor->page, &h);

	cyanfs_task_init(s, &t->base, CYANFS_TASK_WRITE, cyanfs_journal_writer_encode);
	t->base.write.buf = t->cursor->page;
	t->base.write.b_off = cyanfs_map_extent_offset(t->cursor->extent_id, t->cursor->extent_off);
	t->base.write.len = h.size;
	CYANFS_DEBUG("write journal to: %" PRIu32 ":%" PRIu64 "(%" PRIu64 ") len: %" PRIu64 " count: %" PRIu32
		     " seq: %" PRIu64,
		     t->cursor->extent_id, t->cursor->extent_off, t->base.write.b_off, h.size, h.count, h.seq);
	if (t->operations.flush) {
		// 追加写日志，需要确保队列顺序
		cyanfs_super_task_add_head(&t->base);
		if (flush)
			t->operations.flush(t);
	} else {
		// 意味着compact journal
		cyanfs_super_task_add_tail(&t->base);
	}

	if (new_extent != cyanfs_extent_id_invalid) {
		t->cursor->extent_id = new_extent;
		t->cursor->extent_off = 0;
	} else {
		t->cursor->extent_off += h.size;
		CYANFS_DEBUG_BUG_ON(t->cursor->extent_off >= CYANFS_EXTENT_SIZE);
	}
	return;

error:
	if (t->operations.error)
		t->operations.error(t);
}

int cyanfs_journal_writer_start(struct cyanfs_super *s, struct cyanfs_task_journal_writer *t)
{
	cyanfs_task_init(s, &t->base, CYANFS_TASK_NOP, cyanfs_journal_writer_encode);
	return cyanfs_super_task_add_tail(&t->base);
}
