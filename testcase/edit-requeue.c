/* Exercise the actual edit helper with controlled initialization and scheduling. */
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include "core/core.h"

static int test_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
static int test_cond_init(pthread_cond_t *, const pthread_condattr_t *);
static int test_mutex_destroy(pthread_mutex_t *);
static int test_cond_destroy(pthread_cond_t *);
static int test_cond_wait(pthread_cond_t *, pthread_mutex_t *);
static cyanfs_status test_requeue(struct cyanfs_file *, void *, cyanfs_ctx_fn);

#define main cyanfs_edit_main
#define pthread_mutex_init test_mutex_init
#define pthread_cond_init test_cond_init
#define pthread_mutex_destroy test_mutex_destroy
#define pthread_cond_destroy test_cond_destroy
#define pthread_cond_wait test_cond_wait
#define cyanfs_requeue test_requeue
#include "../utils/edit.c"
#undef main
#undef pthread_mutex_init
#undef pthread_cond_init
#undef pthread_mutex_destroy
#undef pthread_cond_destroy
#undef pthread_cond_wait
#undef cyanfs_requeue

enum { FAIL_MUTEX, FAIL_COND, FAIL_QUEUE, EARLY, DELAYED };
static int mode, mutexes, conditions, queued, waits, destroys;
static sem_t gate;
static pthread_t worker;
static void *queued_ctx;
static cyanfs_ctx_fn queued_fn;

static int test_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	if (mode == FAIL_MUTEX)
		return EAGAIN;
	assert(pthread_mutex_init(mutex, attr) == 0);
	++mutexes;
	return 0;
}

static int test_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	assert(mutexes == 1);
	if (mode == FAIL_COND)
		return EAGAIN;
	assert(pthread_cond_init(cond, attr) == 0);
	++conditions;
	return 0;
}

static int test_mutex_destroy(pthread_mutex_t *mutex)
{
	assert(mutexes == 1 && conditions == 0);
	assert(pthread_mutex_destroy(mutex) == 0);
	--mutexes;
	++destroys;
	return 0;
}

static int test_cond_destroy(pthread_cond_t *cond)
{
	assert(mutexes == 1 && conditions == 1);
	assert(pthread_cond_destroy(cond) == 0);
	--conditions;
	++destroys;
	return 0;
}

static int test_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	assert(mode == DELAYED);
	++waits;
	/* Two successful wakes with a false predicate, then allow completion. */
	if (waits <= 2)
		return 0;
	if (waits == 3)
		assert(sem_post(&gate) == 0);
	return pthread_cond_wait(cond, mutex);
}

static void *complete_requeue(void *unused)
{
	assert(sem_wait(&gate) == 0);
	queued_fn(queued_ctx);
	return NULL;
}

static cyanfs_status test_requeue(struct cyanfs_file *file, void *ctx, cyanfs_ctx_fn fn)
{
	assert(mutexes == 1 && conditions == 1);
	++queued;
	if (mode == FAIL_QUEUE)
		return -ENOMEM;
	if (mode == EARLY) {
		fn(ctx);
	} else {
		assert(mode == DELAYED);
		queued_ctx = ctx;
		queued_fn = fn;
		assert(pthread_create(&worker, NULL, complete_requeue, NULL) == 0);
	}
	return 0;
}

int main(void)
{
	int iteration;
	alarm(30);
	assert(sem_init(&gate, 0, 0) == 0);
	for (iteration = 0; iteration < 100; ++iteration) {
		for (mode = FAIL_MUTEX; mode <= DELAYED; ++mode) {
			char buffer[4];
			struct ioctx ctx = { .buffer = buffer, .offset = 7, .op = io_op_read };
			int err;
			mutexes = conditions = queued = waits = destroys = 0;
			if (mode <= FAIL_QUEUE) {
				err = do_io_partial(CYANFS_MAP_REQUEUE, 0, sizeof(buffer), &ctx);
				assert(ctx.buffer == buffer && ctx.offset == 7);
			} else {
				err = wait_requeue(NULL);
			}
			if (mode == DELAYED)
				assert(pthread_join(worker, NULL) == 0);
			assert(mutexes == 0 && conditions == 0);
			assert(queued == (mode >= FAIL_QUEUE));
			assert(destroys == (mode == FAIL_MUTEX ? 0 : mode == FAIL_COND ? 1 : 2));
			assert(err == (mode <= FAIL_COND ? -EAGAIN : mode == FAIL_QUEUE ? -ENOMEM : 0));
			assert(mode == DELAYED ? waits >= 3 : waits == 0);
		}
	}
	assert(sem_destroy(&gate) == 0);
	puts("edit-requeue: PASS");
	return 0;
}
