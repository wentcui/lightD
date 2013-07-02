#include <linux/notifier.h>
#include <linux/schednotify.h>
#include <linux/time.h>   // for using jiffies  
#include <linux/timer.h>
#include <linux/sched.h>  // for task_struct
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/kernel_stat.h>

//#include <asm/div64.h>

#include <linux/tick.h>
#include "logger.h"

#define TRACED_PROC_NR	8

#ifndef nsecs_to_cputime
# define nsecs_to_cputime(__nsecs)	nsecs_to_jiffies(__nsecs)
#endif


static struct per_proc_stat *topN_proc_records_prev;
static struct per_proc_stat *topN_proc_records_curr;

extern struct record *my_record;

u64 nsecs_to_jiffies64(u64 n)
{
#if (NSEC_PER_SEC % HZ) == 0
	/* Common case, HZ = 100, 128, 200, 250, 256, 500, 512, 1000 etc. */
	return div_u64(n, NSEC_PER_SEC / HZ);
#elif (HZ % 512) == 0
	/* overflow after 292 years if HZ = 1024 */
	return div_u64(n * HZ / 512, NSEC_PER_SEC / 512);
#else
	/*
	 * Generic case - optimized for cases where HZ is a multiple of 3.
	 * overflow after 64.99 years, exact for HZ = 60, 72, 90, 120 etc.
	 */
	return div_u64(n * 9, (9ull * NSEC_PER_SEC + HZ / 2) / HZ);
#endif
}


unsigned long nsecs_to_jiffies(u64 n)
{
	return (unsigned long)nsecs_to_jiffies64(n);
}

static inline long sum_cpu_time(struct cpu_times* ct) {
	return ct->user + ct->nice + ct->system + ct->idle;
}

static inline long sum_proc_time(struct proc_times* pt) {
	return pt->utime + pt->stime + pt->cstime + pt->cutime;
}

static inline void update_cpu_time(struct cpu_times *prev, struct cpu_times *curr) {
	prev->user = curr->user;
	prev->nice = curr->nice;
	prev->system = curr->system;
	prev->idle = curr->idle;
}

void task_times1(struct task_struct *p, cputime_t *ut, cputime_t *st)
{
	cputime_t rtime, utime = p->utime, total = utime + p->stime;

	/*
	 * Use CFS's precise accounting:
	 */
	rtime = nsecs_to_cputime(p->se.sum_exec_runtime);

	if (total) {
		u64 temp = (__force u64) rtime;

		temp *= (__force u64) utime;
		do_div(temp, (__force u32) total);
		utime = (__force cputime_t) temp;
	} else
		utime = rtime;

	/*
	 * Compare with previous values, to keep monotonicity:
	 */
	p->prev_utime = max(p->prev_utime, utime);
	p->prev_stime = max(p->prev_stime, rtime - p->prev_utime);

	*ut = p->prev_utime;
	*st = p->prev_stime;
}


long get_idle_time(int cpu)
{
	long idle, idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		/* !NO_HZ so we can rely on cpustat.idle */
		idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	else
		idle = usecs_to_cputime64(idle_time);

	return idle;
}

static void collect_cpu_time(struct cpu_times *cput) {
	long user, nice, system, idle;
	int i = 0;
	user = nice = system = idle = 0;
	for_each_possible_cpu(i) {
		cput->user += kcpustat_cpu(i).cpustat[CPUTIME_USER];
		cput->nice += kcpustat_cpu(i).cpustat[CPUTIME_NICE];
		cput->system += kcpustat_cpu(i).cpustat[CPUTIME_SYSTEM];
		cput->idle += get_idle_time(i);
	}
}

static int collect_proc_time(struct per_proc_stat *curr) {
	//unsigned long flags;
	struct proc_times *proct = &curr->proc_time;
	struct task_struct *task = curr->tsk;
	struct signal_struct *sig = task->signal;

	cputime_t cutime, cstime, utime, stime;

//	if (lock_task_sighand(task, &flags)) {
	//	struct signal_struct *sig = task->signal;
		task_times1(task, &utime, &stime);
		cutime = sig->cutime;
		cstime = sig->cstime;
	
		proct->utime = utime;
		proct->stime = stime;
		proct->cstime = cstime;
		proct->cutime = cutime;

//		unlock_task_sighand(task, &flags);
		return 0;
//	}

//	return -1;
}

/* output the proc status to console */
static void push_procstat(struct per_proc_stat *prev, 
							struct per_proc_stat *curr, 
							struct cpu_times *prev_cput,
							struct cpu_times *curr_cput) {
	long prev_proc_time, curr_proc_time;
	long prev_cpu_time, curr_cpu_time;
	long proc_time, cpu_time;
//	double rate;
	//long t1 = 1.0, t2 = 2.0;
	struct task_struct *tsk = prev->tsk;
	
	if (tsk != curr->tsk) {
		printk("prev pid: %d, curr pid: %d, ERR, task_struct\n", prev->pid, curr->pid);
		return;
	}
	prev_proc_time = sum_proc_time(&prev->proc_time);
	curr_proc_time = sum_proc_time(&curr->proc_time);

	prev_cpu_time = sum_cpu_time(prev_cput);
	curr_cpu_time = sum_cpu_time(curr_cput);

	proc_time = 10000 * (curr_proc_time - prev_proc_time);
	cpu_time = curr_cpu_time - prev_cpu_time;

	do_div(proc_time, cpu_time);
	
	printk("pid: %d, cpu usage: %ld \n", curr->pid, proc_time);
}

static struct per_proc_stat* find_record(struct per_proc_stat *records, 
												struct per_proc_stat *r) {
	int i, pid;
	for (i = 0; i < MAX_TRACED_PROCESS; i++) {
		pid = records[i].pid;
		if (!pid)
			break;

		if (pid == r->pid)
			return &records[i];
	}
	return NULL;
}

static int pick_topN_proc(struct procstat* copystats,
							struct cpu_times* cput) {
	int count, err, i;
	int indexarr[LOG_MAX_CPU_NR];
	struct per_cpu_procstat *per_cpu_stat;
	struct per_proc_stat *proc_stat, *prev_proc_stat;
	

	// localmaxindex: current max of indexarr[]	
	int localmax, localmaxindex, localindex; 

	memset(indexarr, 0, sizeof(int) * LOG_MAX_CPU_NR);

	if (IS_ERR(topN_proc_records_prev)) {
		err = PTR_ERR(topN_proc_records_prev);
		return err;
	}

	/* current process status array */
	topN_proc_records_curr = kmalloc(sizeof(struct per_proc_stat) * TRACED_PROC_NR, GFP_KERNEL);
	if (IS_ERR(topN_proc_records_curr)) {
		err = PTR_ERR(topN_proc_records_curr);
		return err;
	}

	for (count = 0; count < TRACED_PROC_NR; count++) {
		localmax = 0;
		localmaxindex = -1;
		for (i = 0; i < LOG_MAX_CPU_NR; i++) {
			localindex = indexarr[i];
			per_cpu_stat = &copystats->stats[i];
			proc_stat = &per_cpu_stat->per_cpu_stats[localindex];
			if (!proc_stat->pid)
				continue;

			if (proc_stat->counter > localmax) {
				localmax = proc_stat->counter;
				localmaxindex = i;
			}
		}

		if (localmaxindex < 0)
			break;

		per_cpu_stat = &copystats->stats[localmaxindex];
		proc_stat = &per_cpu_stat->per_cpu_stats[indexarr[localmaxindex]];

		/* Collect run time status of this process
		 */
		collect_proc_time(proc_stat);

		/* Find a record in topN_proc_records_prev
		 */
		prev_proc_stat = find_record(topN_proc_records_prev, proc_stat);
		indexarr[localmaxindex]++;

		if (prev_proc_stat) {
			push_procstat(prev_proc_stat, proc_stat, &copystats->cput, cput);
		}
		memcpy(&topN_proc_records_curr[count], proc_stat, sizeof(struct per_proc_stat));
	}
	printk("\n");
	kfree(topN_proc_records_prev);

	topN_proc_records_prev = topN_proc_records_curr;
	topN_proc_records_curr = NULL;

	return 0;
}

static void swap_proc_stat(struct per_proc_stat *pa, struct per_proc_stat *pb) {
	int pid = pa->pid;
	int counter = pa->counter;
	struct task_struct *tsk = pa->tsk;

	pa->pid = pb->pid;
	pa->counter = pb->counter;
	pa->tsk = pb->tsk;

	pb->pid = pid;
	pb->counter = counter; 
	pa->tsk = tsk;
}

void analyse_proc(struct procstat *stats) {
	struct per_cpu_procstat *per_cpu_stat = NULL;
	struct per_proc_stat *proc_stat = NULL;
	struct procstat *copystats;
	struct cpu_times *cput;
	int i, j;

	copystats = kmalloc(sizeof(struct procstat), GFP_KERNEL);
	if (IS_ERR(copystats))
		return;
	
	/* Current cpu time.
	 * Previous cpu time stored in procstat->cputime
	 */
	cput = kmalloc(sizeof(struct cpu_times), GFP_KERNEL);
	if (IS_ERR(cput)) {
		printk("kmalloc cpu_times failed");
		return ;
	}
	memset(cput, 0, sizeof(struct cpu_times));

	/* avoid data race, copy first */
	memcpy(copystats, stats, sizeof(struct procstat));

	/* clear processes heap, prepared for next iteration */
	memset(stats, 0, sizeof(struct procstat));

	collect_cpu_time(cput);

	/* Pick top N processes during each iteration,
	 * output the status of those processes that appeared
	 * in two consecutive iterations.
	 */
	pick_topN_proc(copystats, cput);

	/* Copy the current cpu time to previous cpu time */
	update_cpu_time(&stats->cput, cput);

	/* We've already copied cpu time to previous cpu time */
	kfree(cput);

	for (i = 0; i < LOG_MAX_CPU_NR; i++) {
		per_cpu_stat = &stats->stats[i];
		printk("cpu: %d\n", i);
		for (j = 0; j < MAX_TRACED_PROCESS; j++) {
			proc_stat = &per_cpu_stat->per_cpu_stats[j];
			printk("pid: %d, counter: %ld; ", proc_stat->pid, proc_stat->counter);
		}
		printk("\n");
	}
	printk("\n\n");
	kfree(copystats);
}

/* Increase the process counter in corresponding cpu
 */
void incr_proc_counter(struct proc_record *arr) {
	struct procstat *stats = &my_record->procstat;
	struct per_cpu_procstat *per_cpu_stat;
	struct per_proc_stat *proc_stat;
	struct proc_record *proc_r;
	struct task_struct *tsk;
	int i, j, pid;
	for (i = 0; i < LOG_MAX_CPU_NR; i++) {
		proc_r = &arr[i];
		if (!proc_r || proc_r->pid <= 0)
			continue;
		per_cpu_stat = &stats->stats[i];
		pid = proc_r->pid;
		tsk = proc_r->tsk;

		for (j = 0; j < MAX_TRACED_PROCESS; j++) {
			proc_stat = &per_cpu_stat->per_cpu_stats[j];
			if (proc_stat->pid == pid) {
				proc_stat->counter++;
				//printk("cpu: %d, pid: %d, counter: %ld\n", i, pid, proc_stat->counter);
				if (j > 0 && proc_stat->counter >= per_cpu_stat->per_cpu_stats[j - 1].counter) {
					swap_proc_stat(proc_stat, &per_cpu_stat->per_cpu_stats[j - 1]);
				}
				break;
			} else if (j == MAX_TRACED_PROCESS - 1 || proc_stat->pid <= 0) {
				proc_stat->pid = pid;
				proc_stat->counter = 1;
				proc_stat->tsk = tsk;
				break;
			}
		}
	}
}

static int procstat_init(void) {
	int err = 0;
	topN_proc_records_prev = kmalloc(sizeof(struct per_proc_stat) * TRACED_PROC_NR, GFP_KERNEL);
	if (IS_ERR(topN_proc_records_prev)) {
		err = PTR_ERR(topN_proc_records_prev);
		printk("kmalloc topN_proc_records_prev failed");
		return err;
	}
	return err;
}

static int sched_notifier_call(struct notifier_block *my_notifier, 
		unsigned long change_type, void *data)
{
	struct proc_record *top_proc = (struct proc_record *)data;
	incr_proc_counter(top_proc);

	return 0;
}

struct notifier_block sched_notifier = {
	.notifier_call = sched_notifier_call,
};

int add_scheduler_notifier(void)
{
	if (procstat_init())
		return -1;

	return scheduler_register_notifier(&sched_notifier);
}

int del_scheduler_notifier(void)
{
	return scheduler_unregister_notifier(&sched_notifier);
}
