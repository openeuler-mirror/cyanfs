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
		if (task->type == CYANFS_TASK_READ) {
			if (fail_read) {
				err = -EIO;
				fail_read = 0;
			} else {
				assert(task->read.b_off <= DISK_SIZE);
				assert(task->read.len <= DISK_SIZE - task->read.b_off);
				memcpy(task->read.buf, disk + task->read.b_off, task->read.len);
			}
		} else {
			assert(task->type == CYANFS_TASK_NOP);
		}
		task->done(task, err);
	}
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
	total_allocations = allocation_count;
	drain(s, 0);
	assert(cyanfs_super_is_ready(s) && !cyanfs_super_status(s));
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
		assert((i & 1) ? cyanfs_super_status(s) != 0 : cyanfs_super_is_ready(s));
		cyanfs_super_close(s);
		assert(destroys == 2);
		reset();
	}
	puts("super-locks: PASS");
	return 0;
}
