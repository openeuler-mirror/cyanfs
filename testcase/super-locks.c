/* Real super lifecycle, with a ledger for initialized locks and allocations. */
#include <assert.h>
#include <unistd.h>
#include "core/internal.h"

#define EXTENTS 16
#define DISK_SIZE (CYANFS_SUPER_BLOCK_SIZE + EXTENTS * CYANFS_EXTENT_SIZE)
static unsigned char disk[DISK_SIZE];
static void *allocations[128];
static int live, allocation_count, fail_allocation, fail_rwlock, fail_mutex;
static int rwlocks, mutexes, rw_attempts, mutex_attempts, destroys;
static pthread_rwlock_t *rwlock;
static pthread_mutex_t *mutex;
static struct cyanfs_super *error_on_allocation;

void *__real_malloc(size_t);
void __real_free(void *);
int __real_pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *);
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_rwlock_destroy(pthread_rwlock_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);

void *__wrap_malloc(size_t size)
{
	void *p;
	unsigned int i;
	if (++allocation_count == fail_allocation)
		return NULL;
	p = __real_malloc(size);
	assert(p);
	memset(p, 0xa5, size);
	for (i = 0; i < 128 && allocations[i]; ++i)
		;
	assert(i < 128);
	allocations[i] = p;
	++live;
	if (error_on_allocation) {
		struct cyanfs_super *s = error_on_allocation;

		error_on_allocation = NULL;
		cyanfs_super_mark_error(s);
	}
	return p;
}

void __wrap_free(void *p)
{
	unsigned int i;
	if (!p)
		return;
	for (i = 0; i < 128 && allocations[i] != p; ++i)
		;
	assert(i < 128);
	allocations[i] = NULL;
	--live;
	__real_free(p);
}

int __wrap_pthread_rwlock_init(pthread_rwlock_t *lock, const pthread_rwlockattr_t *attr)
{
	++rw_attempts;
	assert(!rwlocks && !mutexes);
	if (fail_rwlock)
		return EAGAIN;
	assert(__real_pthread_rwlock_init(lock, attr) == 0);
	rwlock = lock;
	++rwlocks;
	return 0;
}

int __wrap_pthread_mutex_init(pthread_mutex_t *lock, const pthread_mutexattr_t *attr)
{
	++mutex_attempts;
	assert(rwlocks == 1 && !mutexes);
	if (fail_mutex)
		return EAGAIN;
	assert(__real_pthread_mutex_init(lock, attr) == 0);
	mutex = lock;
	++mutexes;
	return 0;
}

int __wrap_pthread_mutex_destroy(pthread_mutex_t *lock)
{
	assert(rwlocks == 1 && mutexes == 1 && mutex == lock);
	assert(__real_pthread_mutex_destroy(lock) == 0);
	--mutexes;
	++destroys;
	return 0;
}

int __wrap_pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
	assert(rwlocks == 1 && !mutexes && rwlock == lock);
	assert(__real_pthread_rwlock_destroy(lock) == 0);
	--rwlocks;
	++destroys;
	return 0;
}

static void reset(void)
{
	assert(!live && !rwlocks && !mutexes);
	assert(!error_on_allocation);
	allocation_count = fail_allocation = fail_rwlock = fail_mutex = 0;
	rw_attempts = mutex_attempts = destroys = 0;
}

static void drain(struct cyanfs_super *s, int fail_read)
{
	struct cyanfs_task *task;
	int count = 0;
	while ((task = cyanfs_super_get_task(s))) {
		int err = 0;
		assert(++count < 100);
		switch (task->type) {
		case CYANFS_TASK_NOP:
		case CYANFS_TASK_FLUSH:
			break;
		case CYANFS_TASK_READ:
			if (fail_read) {
				err = -EIO;
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
		task->done(task, err);
	}
}

static cyanfs_file_name_t make_name(unsigned char value)
{
	cyanfs_file_name_t name = { 0 };

	name.data[0] = value;
	return name;
}

static cyanfs_status count_mapping(cyanfs_map_type_t type, uint64_t b_off, uint64_t len, void *ctx)
{
	int *calls = ctx;

	(void)b_off;
	(void)len;
	assert(type == CYANFS_MAP_SUBMIT);
	++*calls;
	return 0;
}

static void check_write_error_after_relock(struct cyanfs_super *s, cyanfs_file_id_t parent)
{
	struct cyanfs_file *parent_file, *child_file;
	cyanfs_file_id_t child;
	uint32_t data_extents, free_extents;
	int calls = 0, live_before;

	assert(!cyanfs_truncate(s, parent, CYANFS_FILE_ALIGN_SIZE));
	drain(s, 0);
	assert(!cyanfs_open(s, parent, 1, &parent_file));
	assert(!cyanfs_write(parent_file, &calls, 0, CYANFS_FILE_ALIGN_SIZE, count_mapping));
	assert(calls == 1);
	cyanfs_close(parent_file);
	drain(s, 0);

	assert(!cyanfs_fork(s, parent, make_name('w'), &child));
	drain(s, 0);
	assert(!cyanfs_open(s, child, 1, &child_file));
	assert(child_file->meta.extents == 0);
	data_extents = s->meta.data_extents;
	free_extents = s->meta.free_extents;
	live_before = live;
	calls = 0;
	error_on_allocation = s;
	assert(cyanfs_write(child_file, &calls, 0, CYANFS_FILE_ALIGN_SIZE, count_mapping) == -CYANFS_ERR_IO);
	assert(!error_on_allocation);
	assert(!calls && live == live_before);
	assert(child_file->meta.extents == 0);
	assert(s->meta.data_extents == data_extents && s->meta.free_extents == free_extents);
	cyanfs_close(child_file);
}

static void check_file_allocations(struct cyanfs_super *s)
{
	struct cyanfs_super_meta meta;
	cyanfs_file_id_t id, parent;
	int i, live_before;

	for (i = 1; i <= 2; ++i) {
		id = (cyanfs_file_id_t)-1;
		live_before = live;
		fail_allocation = allocation_count + i;
		assert(cyanfs_create(s, make_name('c'), &id) == -CYANFS_ERR_NOMEM);
		fail_allocation = 0;
		assert(id == (cyanfs_file_id_t)-1);
		assert(live == live_before);
		assert(!cyanfs_statfs(s, &meta) && meta.files == 0);
	}

	assert(!cyanfs_create(s, make_name('p'), &parent));
	drain(s, 0);
	assert(!cyanfs_statfs(s, &meta) && meta.files == 1);

	for (i = 1; i <= 2; ++i) {
		id = (cyanfs_file_id_t)-1;
		live_before = live;
		fail_allocation = allocation_count + i;
		assert(cyanfs_fork(s, parent, make_name('f'), &id) == -CYANFS_ERR_NOMEM);
		fail_allocation = 0;
		assert(id == (cyanfs_file_id_t)-1);
		assert(live == live_before);
		assert(!cyanfs_statfs(s, &meta) && meta.files == 1);
	}

	check_write_error_after_relock(s, parent);
}

int main(void)
{
	cyanfs_uuid_t uuid = { { 1, 2, 3, 4 } };
	struct cyanfs_super *s;
	int i, total_allocations;
	alarm(30);
	cyanfs_super_make(uuid, disk);
	reset();
	fail_rwlock = 1;
	assert(cyanfs_super_open(DISK_SIZE, 0, 0) == NULL);
	assert(rw_attempts == 1 && mutex_attempts == 0 && destroys == 0);
	reset();
	fail_mutex = 1;
	assert(cyanfs_super_open(DISK_SIZE, 0, 0) == NULL);
	assert(rw_attempts == 1 && mutex_attempts == 1 && destroys == 1);
	reset();
	s = cyanfs_super_open(DISK_SIZE, 0, 0);
	assert(s && rwlocks == 1 && mutexes == 1);
	assert(!cyanfs_super_is_ready(s) && cyanfs_super_status(s) == -CYANFS_ERR_BUSY);
	total_allocations = allocation_count;
	drain(s, 0);
	assert(cyanfs_super_is_ready(s) && !cyanfs_super_status(s));
	check_file_allocations(s);
	cyanfs_super_close(s);
	assert(destroys == 2);
	reset();
	/* Every synchronous allocation: super, extent nodes, page and loader. */
	for (i = 1; i <= total_allocations; ++i) {
		fail_allocation = i;
		assert(cyanfs_super_open(DISK_SIZE, 0, 0) == NULL);
		assert(allocation_count == i);
		assert(destroys == (i == 1 ? 0 : 2));
		reset();
	}
	for (i = 0; i < 100; ++i) {
		s = cyanfs_super_open(DISK_SIZE, 0, 0);
		assert(s && rwlocks == 1 && mutexes == 1);
		drain(s, i & 1);
		if (i & 1) {
			assert(!cyanfs_super_is_ready(s));
			assert(cyanfs_super_status(s) == -CYANFS_ERR_IO);
		} else {
			assert(cyanfs_super_is_ready(s));
			assert(!cyanfs_super_status(s));
		}
		cyanfs_super_close(s);
		assert(destroys == 2);
		reset();
	}
	puts("super-locks: PASS");
	return 0;
}
