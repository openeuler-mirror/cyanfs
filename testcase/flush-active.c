#include <assert.h>
#include "core/internal.h"

static void task_done(struct cyanfs_task *task, cyanfs_status err)
{
	int *done = task->requeue.ctx;

	assert(!err);
	++*done;
}

int main(void)
{
	struct cyanfs_super super = { 0 };
	struct cyanfs_file file = { 0 };
	struct cyanfs_task task;
	cyanfs_map_type_t type;
	int done = 0;

	assert(!cyanfs_init_rwlock(&super.files_lock));
	assert(!cyanfs_init_lock(&super.task_lock));
	CYANFS_INIT_LIST_HEAD(&super.journal_head);
	CYANFS_INIT_LIST_HEAD(&super.task_head);
	super.flags = CYANFS_SUPER_FLAG_READY;
	file.super = &super;

	cyanfs_task_init(&super, &task, CYANFS_TASK_NOP, task_done);
	task.requeue.ctx = &done;
	assert(cyanfs_super_task_add_tail(&task));
	assert(!cyanfs_flush(&file, &type));
	assert(type == CYANFS_MAP_REQUEUE);

	assert(cyanfs_super_get_task(&super) == &task);
	assert(cyanfs_list_empty(&super.task_head) && super.active_tasks == 1);
	assert(!cyanfs_flush(&file, &type));
	assert(type == CYANFS_MAP_REQUEUE);

	cyanfs_super_task_done(&task, 0);
	assert(done == 1 && !super.active_tasks);
	assert(!cyanfs_flush(&file, &type));
	assert(type == CYANFS_MAP_SUBMIT);

	cyanfs_destroy_lock(&super.task_lock);
	cyanfs_destroy_rwlock(&super.files_lock);
	puts("flush-active: PASS");
	return 0;
}
