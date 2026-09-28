/* Test the userspace fatal-invariant primitive without filesystem input. */
#include <assert.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core/core.h"

int main(void)
{
	struct rlimit limit = { 0, 0 };
	int evaluated = 0, status;
	pid_t child;

	CYANFS_BUG_ON(evaluated++);
	assert(evaluated == 1);
	if (evaluated)
		CYANFS_BUG_ON(0);
	else
		return 1;

	assert(setrlimit(RLIMIT_CORE, &limit) == 0);
	child = fork();
	assert(child >= 0);
	if (child == 0) {
		signal(SIGABRT, SIG_DFL);
		alarm(5);
		CYANFS_BUG_ON(1);
		_exit(1);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
	return 0;
}
