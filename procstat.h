#ifndef _LOG_SCHED_NOTIFY_H
#define _LOG_SCHED_NOTIFY_H

int add_scheduler_notifier(void);
int del_scheduler_notifier(void);

void analyse_proc(struct procstat *stats);
//void thread_init(void);
//void thread_exit(void);

#endif
