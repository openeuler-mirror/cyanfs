#include <assert.h>
#include "core/internal.h"

#define EXTENTS 2
#define DISK_SIZE (CYANFS_SUPER_BLOCK_SIZE + EXTENTS * CYANFS_EXTENT_SIZE)

static unsigned char disk[DISK_SIZE];
static int errors, finishes, parser_calls, reads;

static cyanfs_status parse_entry(struct cyanfs_task_journal_loader *loader, struct cyanfs_journal_entry *journal)
{
	(void)loader;
	assert(journal->type == CYANFS_JOURNAL_NEXT);
	++parser_calls;
	return 0;
}

static void finish_loader(struct cyanfs_task_journal_loader *loader)
{
	(void)loader;
	++finishes;
}

static void error_loader(struct cyanfs_task_journal_loader *loader)
{
	(void)loader;
	++errors;
}

static void run_loader(cyanfs_extent_id end_id)
{
	struct cyanfs_super super = { 0 };
	struct cyanfs_task_journal_loader loader;
	struct cyanfs_journal_cursor cursor = { 0 };
	unsigned char page[CYANFS_JOURNAL_PAGE_SIZE];
	struct cyanfs_task *task;

	errors = finishes = parser_calls = reads = 0;
	super.meta.total_extents = EXTENTS;
	assert(!cyanfs_init_lock(&super.task_lock));
	CYANFS_INIT_LIST_HEAD(&super.task_head);
	cursor.page = page;
	cyanfs_journal_loader_init(&loader);
	loader.cursor = &cursor;
	loader.end_id = end_id;
	loader.operations.parser = parse_entry;
	loader.operations.finish = finish_loader;
	loader.operations.error = error_loader;
	cyanfs_journal_loader_start(&super, &loader);

	while ((task = cyanfs_super_get_task(&super))) {
		assert(task->type == CYANFS_TASK_READ);
		assert(task->read.b_off <= DISK_SIZE);
		assert(task->read.len <= DISK_SIZE - task->read.b_off);
		memcpy(task->read.buf, disk + task->read.b_off, task->read.len);
		++reads;
		task->done(task, 0);
	}
	cyanfs_destroy_lock(&super.task_lock);
}

int main(void)
{
	cyanfs_uuid_t uuid = { { 1, 2, 3, 4 } };

	cyanfs_super_make(uuid, disk);

	run_loader(cyanfs_extent_id_invalid);
	assert(finishes == 1 && !errors && parser_calls == 1 && reads == 2);

	run_loader(1);
	assert(!finishes && errors == 1 && !parser_calls && reads == 2);

	run_loader(0);
	assert(finishes == 1 && !errors && !parser_calls && reads == 1);
	return 0;
}
