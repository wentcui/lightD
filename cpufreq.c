#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>

#include "logger.h"

extern struct record my_record;

static int cpufreq_notifier_call(struct notifier_block *my_notifier, 
								unsigned long change_type, void *data)
{
	struct cpufreq_freqs *freq = data;
	if (!freq)
		return -1;

//	printk("cpuid: %d, cpu_freq: %d\n", freq->cpu, freq->old);
	my_record.freqs.cpufreqs[freq->cpu] = freq->old;
	my_record.freqs.modified = true;
	return 0;
}

struct notifier_block cpufreq_notifier = {
	.notifier_call = cpufreq_notifier_call,
};

int add_cpufreq_notifier(void)
{
	return cpufreq_register_notifier(&cpufreq_notifier, 0);
}

int del_cpufreq_notifier(void)
{
	return cpufreq_unregister_notifier(&cpufreq_notifier, 0);
}
