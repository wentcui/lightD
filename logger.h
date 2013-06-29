#ifndef _LOGGER_H
#define _LOGGER_H

#define LOG_MAX_CPU_NR 4
#define MAX_TRACED_PROCESS 20

#include <linux/cpuset.h>

struct cpu_times {
	/* ALL CPU time */
	u64 user;
	u64 nice;
	u64 system;
	u64 idle;
};

struct cpufreqs {
	unsigned int cpufreqs[LOG_MAX_CPU_NR];
	bool modified;

	/* TODO */
	//spinlock
};

struct per_proc_stat {
	int pid;
	long counter;
	struct task_struct* tsk;

	cputime_t cutime, cstime, utime, stime;
};

struct per_cpu_procstat {
	struct per_proc_stat per_cpu_stats[MAX_TRACED_PROCESS];
};

struct procstat {
	/* Total CPU time, sum for all cpus*/
	struct cpu_times cpu_time;

	/* process array for each CPU */
	struct per_cpu_procstat stats[LOG_MAX_CPU_NR];
};

struct record {
	struct cpufreqs freqs;
	struct procstat procstat;
	struct completion event;
};

//struct record my_record;
#endif
