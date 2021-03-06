#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <generated/autoconf.h>
#include <linux/delay.h>

#include <linux/kthread.h>  // for threads
#include <linux/sched.h>  // for task_struct
#include <linux/time.h>   // for using jiffies  
#include <linux/timer.h>
//#include <linux/completion.h>

#include <linux/wait.h>

#include "logger.h"
#include "cpufreq.h"
#include "procstat.h"

#define SLEEP_TIME 3

static struct task_struct *analyse_proc_thread;
struct record *my_record;

/* Thread to analyse the record we get every SLEEP_TIME(s)
 */
int analyse_record(void *data) {
	struct procstat *stats = &my_record->procstat;
	int delay = SLEEP_TIME * HZ;

	while (1) {
		/* Analyse the collected processes */
		analyse_proc(stats);

		/* see you later */
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (kthread_should_stop())
			break;

		schedule_timeout(delay);
	}
	return 0;
}


int thread_init(void) {
	int err;
	char name[8] = "analyse\0";
	analyse_proc_thread = kthread_create(analyse_record, NULL, name);
	if (IS_ERR(analyse_proc_thread)) {
		err = PTR_ERR(analyse_proc_thread);
		analyse_proc_thread = NULL;
		return err;
	}
	wake_up_process(analyse_proc_thread);
	return 0;
}

void thread_exit(void) {
	if (analyse_proc_thread)
		kthread_stop(analyse_proc_thread);
}

int init_logger(void)
{
	int err = 0;
	my_record = kmalloc(sizeof(struct record), GFP_KERNEL);
	if (IS_ERR(my_record)) {
		err = PTR_ERR(my_record);
		return err;
	}
	
	memset(my_record, 0, sizeof(struct record));
	return err;
}

int __init lkp_init(void)
{
	int r = 0;
	r = init_logger();
	if (r)
		printk("init logger failed\n");

	thread_init();

	r = add_cpufreq_notifier();
	if (r)
		printk("add cpu notifier failed");

	r = add_scheduler_notifier();
	if (r)
		printk("add scheduler notifier failed");

	return r;
}

void __exit lkp_cleanup(void)
{
	thread_exit();
	del_scheduler_notifier();
	del_cpufreq_notifier();
	kfree(my_record);
}

module_init(lkp_init);
module_exit(lkp_cleanup);
MODULE_LICENSE("Dual BSD/GPL");
