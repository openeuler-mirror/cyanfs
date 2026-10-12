#include <assert.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "core/internal.h"

#define EXTENTS 16
#define DISK_SIZE (CYANFS_SUPER_BLOCK_SIZE + EXTENTS * CYANFS_EXTENT_SIZE)

static unsigned char disk[DISK_SIZE];
static int allocation_count;
static int fail_allocation;

void *__real_malloc(size_t size);

void *__wrap_malloc(size_t size)
{
	if (++allocation_count == fail_allocation)
		return NULL;
	return __real_malloc(size);
}

static void make_disk(void)
{
	cyanfs_uuid_t uuid = { { 1, 2, 3, 4 } };

	memset(disk, 0, sizeof(disk));
	cyanfs_super_make(uuid, disk);
}

static void drain(struct cyanfs_super *s, int fail_read)
{
	struct cyanfs_task *task;
	int count = 0;

	while ((task = cyanfs_super_get_task(s))) {
		cyanfs_status err = 0;

		assert(++count < 100);
		switch (task->type) {
		case CYANFS_TASK_NOP:
		case CYANFS_TASK_FLUSH:
			break;
		case CYANFS_TASK_READ:
			if (fail_read) {
				err = -CYANFS_ERR_IO;
				fail_read = 0;
			} else {
				assert(task->read.b_off <= DISK_SIZE);
				assert(task->read.len <= DISK_SIZE - task->read.b_off);
				memcpy(task->read.buf, disk + task->read.b_off, task->read.len);
			}
			break;
		case CYANFS_TASK_WRITE:
			assert(task->write.b_off <= DISK_SIZE);
			assert(task->write.len <= DISK_SIZE - task->write.b_off);
			memcpy(disk + task->write.b_off, task->write.buf, task->write.len);
			break;
		case CYANFS_TASK_COPY:
			assert(task->copy.b_src_off <= DISK_SIZE);
			assert(task->copy.len <= DISK_SIZE - task->copy.b_src_off);
			assert(task->copy.b_dst_off <= DISK_SIZE);
			assert(task->copy.len <= DISK_SIZE - task->copy.b_dst_off);
			memmove(disk + task->copy.b_dst_off, disk + task->copy.b_src_off, task->copy.len);
			break;
		case CYANFS_TASK_DISCARD:
			assert(task->discard.b_off <= DISK_SIZE);
			assert(task->discard.len <= DISK_SIZE - task->discard.b_off);
			memset(disk + task->discard.b_off, 0, task->discard.len);
			break;
		case CYANFS_TASK_REQUEUE:
			task->requeue.fn(task->requeue.ctx);
			break;
		}
		cyanfs_super_task_done(task, err);
	}
}

static struct cyanfs_super *open_ready(int compact)
{
	struct cyanfs_super *s;

	make_disk();
	s = cyanfs_super_open(DISK_SIZE, 0, compact);
	assert(s);
	drain(s, 0);
	assert(cyanfs_super_is_ready(s));
	assert(!cyanfs_super_status(s));
	return s;
}

int main(void)
{
	struct cyanfs_super *s;

	alarm(30);

	make_disk();
	s = cyanfs_super_open(DISK_SIZE, 0, 1);
	assert(s);
	assert(cyanfs_super_compact(s) == -CYANFS_ERR_BUSY);
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	drain(s, 0);
	cyanfs_super_close(s);

	make_disk();
	s = cyanfs_super_open(DISK_SIZE, 0, 1);
	assert(s);
	drain(s, 1);
	assert(cyanfs_super_status(s) == -CYANFS_ERR_IO);
	assert(cyanfs_super_compact(s) == -CYANFS_ERR_IO);
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	cyanfs_super_close(s);

	s = open_ready(0);
	assert(!cyanfs_super_compact(s));
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	assert(!cyanfs_super_get_task(s));
	cyanfs_super_close(s);

	s = open_ready(1);
	assert(!cyanfs_super_compact(s));
	assert(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING);
	assert(cyanfs_super_compact(s) == -CYANFS_ERR_BUSY);
	drain(s, 0);
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	assert(!cyanfs_super_status(s));
	assert(!cyanfs_super_compact(s));
	assert(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING);
	drain(s, 0);
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	assert(!cyanfs_super_status(s));
	cyanfs_super_close(s);

	s = open_ready(1);
	assert(!cyanfs_super_compact(s));
	drain(s, 1);
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	assert(cyanfs_super_status(s) == -CYANFS_ERR_IO);
	assert(cyanfs_super_compact(s) == -CYANFS_ERR_IO);
	cyanfs_super_close(s);

	s = open_ready(1);
	fail_allocation = allocation_count + 1;
	assert(cyanfs_super_compact(s) == -CYANFS_ERR_NOMEM);
	fail_allocation = 0;
	assert(!(s->flags & CYANFS_SUPER_FLAG_COMPACT_WORKING));
	assert(cyanfs_super_status(s) == -CYANFS_ERR_IO);
	assert(!cyanfs_super_get_task(s));
	cyanfs_super_close(s);

	return 0;
}
