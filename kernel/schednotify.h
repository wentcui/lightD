#ifndef _LINUX_SCHED_NOTIFY_H
#define _LINUX_SCHED_NOTIFY_H

#define MAX_CPU_NR 4

//#define TRACE_MODE

struct proc_record {
	pid_t pid;
	struct task_struct *tsk;
};

int scheduler_register_notifier(struct notifier_block *nb);
int scheduler_unregister_notifier(struct notifier_block *nb);
void scheduler_notify_transition(struct proc_record *);

/* NO lock to protect the access of this array */

#endif
