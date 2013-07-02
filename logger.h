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

struct proc_times {
	u64 utime;
	u64 stime;
	u64 cstime;
	u64 cutime;
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

	/* proc run time */
	struct proc_times proc_time;
};

struct per_cpu_procstat {
	struct per_proc_stat per_cpu_stats[MAX_TRACED_PROCESS];
};

struct procstat {
	/* Total CPU time, sum for all cpus */
	struct cpu_times cput;

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
