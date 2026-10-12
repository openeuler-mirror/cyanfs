#include <assert.h>
#include <stdlib.h>
#include "core/internal.h"

static struct cyanfs_super *error_on_allocation;

void *__real_malloc(size_t);
void __real_free(void *);

void *__wrap_malloc(size_t size)
{
	void *p = __real_malloc(size);

	if (p && error_on_allocation) {
		struct cyanfs_super *s = error_on_allocation;

		error_on_allocation = NULL;
		cyanfs_super_mark_error(s);
	}
	return p;
}

void __wrap_free(void *p)
{
	__real_free(p);
}

static cyanfs_status count_mapping(cyanfs_map_type_t type, uint64_t b_off, uint64_t len, void *ctx)
{
	int *calls = ctx;

	(void)b_off;
	(void)len;
	assert(type == CYANFS_MAP_NOP);
	++*calls;
	return 0;
}

int main(void)
{
	struct cyanfs_super s = { 0 };
	struct cyanfs_file file = { 0 };
	struct cyanfs_extent_node extent = { 0 };
	int calls = 0;

	assert(!cyanfs_init_rwlock(&s.files_lock));
	CYANFS_INIT_LIST_HEAD(&s.journal_head);
	CYANFS_RB_INIT(&file.extents);
	s.flags = CYANFS_SUPER_FLAG_READY;
	file.super = &s;
	file.open_type = CYANFS_FILE_READWRITE;
	file.meta.size = CYANFS_EXTENT_SIZE;

	assert(!cyanfs_discard(&file, &calls, 0, CYANFS_FILE_ALIGN_SIZE, count_mapping));
	assert(calls == 1);
	calls = 0;
	assert(!cyanfs_discard(&file, &calls, 0, CYANFS_EXTENT_SIZE, count_mapping));
	assert(calls == 1);

	s.flags |= CYANFS_SUPER_FLAG_ERROR;
	calls = 0;
	assert(cyanfs_discard(&file, &calls, 0, CYANFS_FILE_ALIGN_SIZE, count_mapping) == -CYANFS_ERR_IO);
	assert(!calls);
	assert(cyanfs_discard(&file, &calls, 0, CYANFS_EXTENT_SIZE, count_mapping) == -CYANFS_ERR_IO);
	assert(!calls);

	s.flags = CYANFS_SUPER_FLAG_READY;
	extent.v.map = 1;
	extent.v.error = 1;
	extent.v.file = 0;
	assert(!CYANFS_RB_INSERT(cyanfs_file_extents_rb, &file.extents, &extent));
	assert(cyanfs_discard(&file, &calls, 0, CYANFS_EXTENT_SIZE, count_mapping) == -CYANFS_ERR_IO);
	assert(!calls);

	extent.v.error = 0;
	error_on_allocation = &s;
	assert(cyanfs_discard(&file, &calls, 0, CYANFS_EXTENT_SIZE, count_mapping) == -CYANFS_ERR_IO);
	assert(!error_on_allocation && !calls);
	assert(CYANFS_RB_FIND(cyanfs_file_extents_rb, &file.extents, &extent) == &extent);
	assert(cyanfs_list_empty(&s.journal_head));

	cyanfs_destroy_rwlock(&s.files_lock);
	puts("discard-errors: PASS");
	return 0;
}
