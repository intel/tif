//SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Intel Corporation
 *
 * Contains the tif_workload function called by tif_jitter
 *
 * Author: Ramesh Thomas
 * Created: 5/11/2020
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define WORKLOAD_LOOPS 50000 //Loops in workload
#define WORK_MEM_SIZE 256

static inline unsigned int random_num(void)
{
	unsigned int x;

	asm volatile ("rdtsc" : "=a" (x));

	return x;
}

void nohz_workload(void)
{
	static unsigned int a[WORK_MEM_SIZE];
	unsigned int i, x, y;

	for (i = 0;i < WORKLOAD_LOOPS / 2;i++) {
		x = random_num() % WORK_MEM_SIZE;
		a[x] = x + 1;
		y = random_num() % WORK_MEM_SIZE;
		a[y] = x + y;
	}
	for (i = 0;i < WORKLOAD_LOOPS / 2;i++) {
		x = a[random_num() % WORK_MEM_SIZE];
		y = a[random_num() % WORK_MEM_SIZE];
		x += y;
		a[x % WORK_MEM_SIZE] = x;
	}
}
