//SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Intel Corporation
 *
 * TIF functions to setup CPU thread isolation environment and
 * synchronize entry into nohz state.
 *
 * Author: Ramesh Thomas
 * Created: 5/11/2020
 *
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include <ctype.h>
#include <numa.h>

//Wait time in secs for sched 100% runtime setting to take effect
#define SCHED_RUNTIME_WAIT_SEC 1

/*******************************************************************
 * Functions to synchronize nohz state entry
 ******************************************************************/
/*
 * Assigns scheduler runtime to RT tasks passed as percentage
 * of scheduler period in the range of 95 to 100.
 *
 * Params:
 * int runtime_perc: percentage between 95 and 100.
 *
 * Returns 0 on success, -1 on error
 *
 */
static int set_sched_runtime(int runtime_perc)
{
	char str[128];
	FILE *fp;
	int p, r;
	char *ptr;
	int ret = -1;

	fp = fopen("/proc/sys/kernel/sched_rt_period_us", "rb");
	if (fp) {
		ptr = fgets(str, 128, fp);
		fclose(fp);

		if (!ptr)
			return -1;

		p = atoi(str);

		r = -1; //-1 = 100%
		if (runtime_perc < 100) {
			runtime_perc = runtime_perc < 95 ? 95 : runtime_perc;
			r = ((p * runtime_perc) / 100);
		}

		fp = fopen("/proc/sys/kernel/sched_rt_runtime_us", "wb");
		if (fp) {
			sprintf(str, "%d", r);
			fputs(str, fp);
			fclose(fp);
			ret = 0;
		}
	}

	return ret;
}

int set_cpu_affinity(int cpu, int pid);

/*
 * Toggles the affinity of the current thread between current
 * CPU and CPU 0. This forces scheduler to reset its states for
 * the thread. This is repeated till the scheduler finds the
 * conditions necessary to enter nohz state.
 */
static void toggle_affinity(void)
{
	int cpu = sched_getcpu();

	set_cpu_affinity(0, 0);
	set_cpu_affinity(cpu, 0);
}

/*
 * Get monotonic clock count in microseconds
 */
static long get_time(void)
{
	struct timespec time;

	clock_gettime(CLOCK_MONOTONIC, &time);

	return  time.tv_sec * 1000000L + time.tv_nsec / 1000L;
}

static int is_cur_cpu_data(int cpu, char *line)
{
	while (*line && isspace(*line))
		line++;

	if (!memcmp("cpu:", line, 4))
		return (cpu == atoi(line + 4));

	return 0;
}

static int get_tick_stopped(char *line)
{
	while (*line && isspace(*line))
		line++;

	if (*line && !memcmp(".tick_stopped", line, 13)) {
		while (*line && *line != ':')
			line++;

		if (*line)
			line++;

		return atoi(line+1);
	}

	return -1;
}

/*
 * Returns 1 if tick is stopped, 0 if not stopped, -1 on error
 */
static int is_tick_stopped(int cpu)
{
	FILE *fp;
	char *line = NULL;
	size_t size = 0;
	int stopped = -1;

	fp = fopen("/proc/timer_list", "rb");
	if (fp) {
		while (getline(&line, &size, fp) != -1) {
			if (is_cur_cpu_data(cpu, line)) {
				while (getline(&line, &size, fp) != -1) {
					stopped = get_tick_stopped(line);
					if (stopped != -1)
						break;
				}
				break;
			}
		}
		free(line);
		fclose(fp);
	}

	return stopped;
}

/*
 * This function will wait till nohz state is entered. The scheduler could take
 * some time to clear pending interrupts and other conditions necessary before
 * nohz state can be entered. This function checks a kernel flag indicating
 * tick is stopped to ensure RT workloads get executed after scheduler timer
 * ticks are suppressed. This is necessary at entry of the thread and after
 * function calls that will cause the thread to be scheduled out
 * e.g. sleep(), mlockall()
 *
 * The "forced" option can be used to cause a forced entry as a workaround
 * for an issue found in PREEMPT_RT kernel that fails nohz state entry.
 * If "forced" is set to a non zero value then this function would
 * toggle the affinity of the calling thread between current CPU and CPU 0 to
 * cause the scheduler internal states to get reinitialized forcing it to attempt
 * entry into nohz state again.
 *
 * Note: Setting forced=1 causes longer wait time and is necessary only if
 * kernel has issues entering nohz state.
 *
 * Params:
 * long msecs: microseconds to try before returning failure
 * int forced: 1 = force nohz entry, 0 = only wait for nohz entry
 *
 * Return: 0 - success
 *        -1 - failed nohz entry after max wait time
 *        -2 - no nohz support in kernel
 *
 */
long nohz_wait(long usecs, int forced)
{
	long t1, t2;
	int cpu = sched_getcpu();
	int tick_stopped;

	t1 = get_time();

	do {
		t2 = get_time();

		tick_stopped = is_tick_stopped(cpu);
		if (tick_stopped == -1)
			return -2;
		if (tick_stopped == 1)
			return 0;
		if (forced)
			toggle_affinity();

	} while (t2-t1 < usecs);

	return -1;
}

/*
 * Assigns 100% scheduler runtime to RT tasks by setting
 * /proc/sys/kernel/sched_rt_runtime_us to -1
 *
 * Return: 0 on success, -1 on error
 *
 */
int nohz_enter(void)
{
	// Percentage must be in the range 95 - 100
	if (set_sched_runtime(100))
		return -1;

	//Wait for runtime setting to take effect
	sleep(SCHED_RUNTIME_WAIT_SEC);

	return 0;
}

/*
 * Called to revert 100% scheduler runtime assignment
 *
 * Since scheduler runtime setting is a system wide setting,
 * this should be called only after all RT tasks are completed.
 *
 * Returns 0 on succes, -1 on error
 */
int nohz_exit(void)
{
	return set_sched_runtime(95);
}

/*******************************************************************
 * Utility functions
 ******************************************************************/

/*
 * Affines the currently executing thread to the passed CPU
 *
 * Params:
 * int cpu: CPU to be affined to
 * int pid: Process id of thread. 0 for current thread.
 *
 * Returns -1 on error
 *
 */
int set_cpu_affinity(int cpu, int pid)
{
	cpu_set_t mask;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	return sched_setaffinity(pid, sizeof(mask), &mask);
}

/*
 * Sets scheduling policy to FIFO with max priority
 *
 * Params:
 * int pid: Process id of thread. 0 for current thread.
 *
 * Returns -1 on error
 *
 */
int set_sched_fifo(int pid)
{
	struct sched_param param;

	param.sched_priority = sched_get_priority_max(SCHED_FIFO);

	return sched_setscheduler(pid, SCHED_FIFO | SCHED_RESET_ON_FORK, &param);
}

/*
 * Retrieves all CPUs listed as nohz_full
 *
 * Returns:
 * void* - pointer to object with cpu mask, NULL on error
 *
 */
static void *get_nohz_full_cpu_mask(void)
{
	FILE *fp;
	char str[128];
	void *p;

	fp = fopen("/sys/devices/system/cpu/nohz_full", "rb");
	if (!fp) {
		printf("Kernel does not support NOHZ\n");
		return NULL;
	}

	p = fgets(str, 128, fp);
	fclose(fp);

	if (!p || !*str || *str == '\n') {
		printf("NOHZ cpu not found\n");
		return NULL;
	}

	strtok(str, "\n");

	p = numa_parse_cpustring_all(str);

	if (!p)
		printf("Error parsing NOHZ cpu list\n");

	return p;
}

/*
 * Verifies if passed CPU is a valid nohz CPU.
 * NOHZ CPUs must be available, CPU should be non-zero and
 * CPU must be one of the NOHZ CPUs.
 *
 * Returns 1 if valid, 0 if not or error
 */
int is_nohz_cpu(int cpu)
{
	struct bitmask *c = get_nohz_full_cpu_mask();

	if (c && cpu && numa_bitmask_isbitset(c, cpu)) {
		numa_bitmask_free(c);
		return 1;
	}

	return 0;
}

/*
 * Finds the first nohz_full CPU.
 *
 * Returns nohz_full CPU if found, -1 if none found
 *
 */
int get_nohz_full_cpu(void)
{
	FILE *fp;
	char str[128];
	int c;
	char *ret;

	fp = fopen("/sys/devices/system/cpu/nohz_full", "rb");
	if (fp) {
		ret = fgets(str, 128, fp);

		fclose(fp);

		if (ret) {
			c = atoi(str);
			if (c)
				return c;
		}
	}

	return -1;
}
