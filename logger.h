#ifndef _LOGGER_H
#define _LOGGER_H

#define LOG_MAX_CPU_NR 4
#define MAX_TRACED_PROCESS 10

struct cpufreqs {
	unsigned int cpufreqs[LOG_MAX_CPU_NR];
	bool modified;

	/* TODO */
	//spinlock
};

struct per_proc_stat {
	int pid;
	long counter;
};

struct per_cpu_procstat {
	struct per_proc_stat per_cpu_stats[MAX_TRACED_PROCESS];
};

struct procstat {
	struct per_cpu_procstat stats[LOG_MAX_CPU_NR];
};

struct record {
	struct cpufreqs freqs;
	struct procstat procstat;
	struct completion event;
};

//struct record my_record;
#endif
