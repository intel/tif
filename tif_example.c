//SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Intel Corporation
 *
 * Example using TIF to setup CPU thread isolation environment.
 *
 * Author: Ramesh Thomas
 * Created: 5/11/2020
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "tif_helper.h"

int main(int argc, char **argv)
{
	struct timespec t1, t2;

	printf("\nExample using TIF\n\n");

	/*
	 * CPU thread isolation environment is setup as follows
	 * - Assign 100% scheduler runtime to RT tasks
	 * - Affine CPU to isolated CPU
	 * - Set scheduling policy to FIFO with max priority
	 */

	/******************************************************************
	 *        Entry procedure common for all CPUs/RT threads
	 *****************************************************************/

	/*
	 * Assigns 100% scheduler runtime to RT tasks. This is a global setting
	 * common to all CPUs and should be called only once at the beginning.
	 */
	if (nohz_enter()) {
		printf("Error setting up NOHZ_FULL\n");
		goto ext;
	}


	/******************************************************************
	 *        Procedures per CPU/RT thread
	 *****************************************************************/
	/*
	 * Utility function retrieves first CPU in nohz_full CPU list.
	 * Return -1 if no CPU was setup as nohz_full
	 */
	int nohz_cpu = get_nohz_full_cpu();
	if (nohz_cpu < 0) {
		printf("NOHZ CPU not found\n");
		goto ext;
	}

	/*
	 * Set affinity to one of the CPUs listed in nohz_full and isolcpus
	 * boot parameters. First parameter is an isolated nohz CPU. Second
	 * parameter is thread id. If thread id is zero then calling thread
	 * is affined to the CPU.
	 */
	if (set_cpu_affinity(nohz_cpu, 0) < 0) {
		printf("Error setting affinity to CPU %d\n", nohz_cpu);
		goto ext;
	}

	/*
	 * To ensure no other task preempts the RT task, it should use a RT
	 * scheduling policy with highest priority. This function sets the
	 * thread's scheduling policy as FIFO and sets it to max priority of
	 * that policy. Parameter is thread id which can be zero to indicate
	 * calling thread.
	 */
	if (set_sched_fifo(0) < 0) {
		printf("Error setting FIFO scheduling policy\n");
		goto ext;
	}

	/*
	 * This synchronizes the entry into nohz state. When the RT thread is
	 * scheduled in the isolated CPU, the kernel detecting it is the only
	 * task running will enter nohz state and suppress scheduler timer ticks.
	 * However, * this may take time. This function checks kernel flags to
	 * ensure scheduler timer ticks have stopped.
	 *
	 * There is a known issue in PREEMPT_RT kernel where nohz state entry
	 * fails. The second parameter uses methods known to get around this
	 * issse forcing entry into nohz state. First parameter is max wait time
	 * in microseconds after which the function would return failure.
	 */

	clock_gettime(CLOCK_MONOTONIC, &t1);

	/* First try without 'forced' and shorter wait */
	long ret = nohz_wait(5000, 0);

	/* If failed, try with 'forced' and longer wait */
	if (ret < 0)
		ret = nohz_wait(5000000, 1);

	if (ret < 0) {
		printf("Error entering nohz state\n");
		goto ext;
	}

	/* Run RT workloads */

	clock_gettime(CLOCK_MONOTONIC, &t2);

	/*
	 * RT tasks should be uninterrupted during execution. System calls,
	 * prints etc. can introduce jitter. Running RT tasks in its own
	 * thread gives better performance.
	 *
	 * The PREEMPT_RT kernel issue with NOHZ state requires forced entry
	 * after every call that yields the CPU causing it to be scheduled
	 * out. Functions like sleep() or call into kernel like mlockall()
	 * will cause the thread to get scheduled out. Either avoid them
	 * in the RT workload or call nohs_wait() with the forced parameter
	 * set after calling.
	 */

	long wait_us = (t2.tv_sec * 1000000L + t2.tv_nsec / 1000L) -
		       (t1.tv_sec * 1000000L + t1.tv_nsec / 1000L);
	printf("Successfully entered nohz state in %ldus\n", wait_us);

ext:
	/******************************************************************
	 *        Exit procedure common for all CPUs/RT threads
	 *****************************************************************/
	/*
	 * Reverses 100% scheduler runtime assignment to RT tasks.
	 * This is a global setting and calling this will cause RT tasks in all
	 * CPUSs to exit nohz state.
	 */
	nohz_exit();

	return 0;
}
