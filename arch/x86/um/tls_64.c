#include "linux/sched.h"

void clear_flushed_tls(struct task_struct *task)
{
}

int arch_copy_tls(struct task_struct *t)
{
	t->thread.arch.fs = t->thread.regs.regs.gp[R8 / sizeof(long)];

	return 0;
}
