#include <linux/notifier.h>
#include <linux/schednotify.h>
#include <linux/time.h>   // for using jiffies  
#include <linux/timer.h>
#include <linux/sched.h>  // for task_struct
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/err.h>

#include "logger.h"

#define TRACED_PROC_NR	8
static struct per_proc_stat *topN_proc_records_prev;
static struct per_proc_stat *topN_proc_records_curr;

extern struct record my_record;

static struct per_proc_stat* find_record(struct per_proc_stat *records, struct per_proc_stat *r) {
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

static void push_procstat(struct per_proc_stat *prev, struct per_proc_stat *curr) {
	//TODO
	printk("pid: %d ", curr->pid);
}

static int pick_topN_proc(struct procstat* copystats) {
	int count, err, i;
	int indexarr[LOG_MAX_CPU_NR];
	struct per_cpu_procstat *per_cpu_stat = NULL;
	struct per_proc_stat *proc_stat, *prev_proc_stat;

	// localmaxindex: current max of indexarr[]	
	int localmax, localmaxindex, localindex; 
	
	memset(indexarr, 0, sizeof(int) * LOG_MAX_CPU_NR);

	if (IS_ERR(topN_proc_records_prev)) {
		err = PTR_ERR(topN_proc_records_prev);
		return err;
	}

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

		/* Find a record in topN_proc_records_prev
		 */
		prev_proc_stat = find_record(topN_proc_records_prev, proc_stat);
		indexarr[localmaxindex]++;

		if (prev_proc_stat) {
			push_procstat(prev_proc_stat, proc_stat);
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

	pa->pid = pb->pid;
	pa->counter = pb->counter;

	pb->pid = pid;
	pb->counter = counter; 
}

void analyse_proc(struct procstat *stats) {
	struct per_cpu_procstat *per_cpu_stat = NULL;
	struct per_proc_stat *proc_stat = NULL;
	struct procstat copystats;
	int i, j;

	// avoid data race, copy first
	memcpy(&copystats, stats, sizeof(struct procstat));
	pick_topN_proc(&copystats);
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
	memset(stats, 0, sizeof(struct procstat));
}

/* Increase the process counter in corresponding cpu
 */
void incr_proc_counter(int *arr) {
	struct procstat *stats = &my_record.procstat;
	struct per_cpu_procstat *per_cpu_stat = NULL;
	struct per_proc_stat *proc_stat = NULL;
	int i, j;
	int pid = 0;
	for (i = 0; i < LOG_MAX_CPU_NR; i++) {
		pid = arr[i];
		if (pid <= 0)
			continue;
		per_cpu_stat = &stats->stats[i];

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
	int *top_proc = (int *)data;
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
