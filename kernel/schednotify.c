#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/syscore_ops.h>

#include <trace/events/power.h>

#include <linux/schednotify.h>

static struct srcu_notifier_head scheduler_notifier_list;
static bool init_scheduler_notifier_list_called = false;

static int __init init_scheduler_notifier_list(void)
{
	srcu_init_notifier_head(&scheduler_notifier_list);
	init_scheduler_notifier_list_called = true;
	return 0;
}
pure_initcall(init_scheduler_notifier_list);

/**
 *	scheduler_register_notifier - register a driver with scheduler
 *	@nb: notifier function to register
 *
 *	Add a driver to scheduler notifier list.
 *
 *	This function may sleep, and has the same return conditions as
 *	srcu_notifier_chain_register.
 */
int scheduler_register_notifier(struct notifier_block *nb)
{
	if (!init_scheduler_notifier_list_called)
		return -1;

	return srcu_notifier_chain_register(&scheduler_notifier_list, nb);
}
EXPORT_SYMBOL(scheduler_register_notifier);


/**
 *	cpufreq_unregister_notifier - unregister a driver with scheduler
 *	@nb: notifier block to be unregistered
 *
 *	Remove a driver from scheduler notifier list.
 *
 *	This function may sleep, and has the same return conditions as
 *	srcu_notifier_chain_register.
 */
int scheduler_unregister_notifier(struct notifier_block *nb)
{
	if (!init_scheduler_notifier_list_called)
		return -1;

	return srcu_notifier_chain_unregister(
				&scheduler_notifier_list, nb);
}
EXPORT_SYMBOL(scheduler_unregister_notifier);


void scheduler_notify_transition(struct proc_record *array)
{
	if (!init_scheduler_notifier_list_called)
		return;

	srcu_notifier_call_chain(&scheduler_notifier_list, 0, (void *)array);	
}
EXPORT_SYMBOL_GPL(scheduler_notify_transition);
