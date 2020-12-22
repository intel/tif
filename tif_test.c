//SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Intel Corporation
 *
 * Tests nohz state entry and measures time taken
 *
 * Author: Ramesh Thomas
 * Created: 12/17/2020
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "tif_helper.h"

#define MAX_WAIT_US 15000000 

int main(int argc, char **argv)
{
	long ret, wait_us;

	struct timespec t1, t2;

	if (nohz_enter()) {
		printf("Error setting up NOHZ_FULL\n");
		goto ext;
	}

	int nohz_cpu = get_nohz_full_cpu();

	if (nohz_cpu < 0) {
		printf("NOHZ CPU not found\n");
		goto ext;
	}

	if (set_cpu_affinity(nohz_cpu, 0) < 0) {
		printf("Error setting affinity to CPU %d\n", nohz_cpu);
		goto ext;
	}

	if (set_sched_fifo(0) < 0) {
		printf("Error setting FIFO scheduling policy\n");
		goto ext;
	}

	clock_gettime(CLOCK_MONOTONIC, &t1);

	/* Wait for MAX_WAIT_US without forcing nohz entry*/
	ret = nohz_wait(MAX_WAIT_US, 0);

	clock_gettime(CLOCK_MONOTONIC, &t2);

	wait_us = (t2.tv_sec * 1000000L + t2.tv_nsec / 1000L) -
		       (t1.tv_sec * 1000000L + t1.tv_nsec / 1000L);
	if (ret < 0) {
		printf("\n\nError entering nohz state after %luus\n", wait_us);
		nohz_exit();
		exit(-1);
	}

	printf("Successfully entered nohz state in %luus%10s\n", wait_us, "");

ext:
	nohz_exit();
	return 0;
}
