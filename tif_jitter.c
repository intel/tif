//SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2020 Intel Corporation
 *
 * tif_jitter - a simple jitter measurment tool that uses TIF
 * to set up CPU thread isolation environment.
 *
 * Author: Ramesh Thomas
 * Created: 5/11/2020
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <inttypes.h>
#include <x86intrin.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include "tif_helper.h"

void nohz_workload(void);

#define PRINT_INFO 1

#define NUM_TESTS 1000 //Default number of tests
#define NUM_LOOPS 1000 //Default number of times workload is run per test
#define HIST_FILE "nohz.hist" //Default histogram file

//Global options set by command line arguments
int use_tsc;
int num_tests = NUM_TESTS;
int num_loops = NUM_LOOPS;
int nohz_cpu;
int duration;
int hist;
FILE *hist_fd;

uint64_t cmax, cmin = -1, avg;
uint64_t cstart, cend;

int tests_done;

struct thread_data {
	uint64_t jitter;
	int cpu;
	int ret;
};

static inline uint64_t get_time(void)
{
	uint64_t retval;

	if (use_tsc) {
		unsigned int tsc_aux;

		asm volatile ("lfence":::"memory");
		retval = __rdtscp(&tsc_aux);
		asm volatile ("lfence":::"memory");
	} else {
		struct timespec time;

		asm volatile ("lfence":::"memory");
		clock_gettime(CLOCK_MONOTONIC, &time);
		asm volatile ("lfence":::"memory");

		retval = time.tv_sec * 1000000000L + time.tv_nsec;
	}

	return retval;
}

static inline void print_jitter(uint64_t jitter)
{
	static int once;

	if (!once) {
		printf("                (Jitter in %s)\n",
				use_tsc ? "TSC ticks" : "nanoseconds");
		printf("     Test#     Jitter        Max        Min");
		printf("       Mean\n");
		printf("-------------------------------------------");
		printf("------------\n");
		once = 1;
	}
	printf("%10u %10lu %10lu %10lu %10lu\n\033[1A",
			tests_done, jitter, cmax, cmin, avg/tests_done);
}

static void cleanup(void)
{
	printf("\n\n");
	nohz_exit();
	if (hist_fd)
		fclose(hist_fd);
}

static int time_expired(void)
{
	static int elapsed;
	struct timespec time;

	clock_gettime(CLOCK_REALTIME, &time);
	if (!elapsed) {
		elapsed = time.tv_sec/60;
		return 0;
	}

	if (time.tv_sec/60 - elapsed > duration)
		return 1;

	return 0;
}

static void signal_handler(int signalno)
{
	if (signalno == SIGINT) {
		cleanup();

		exit(0);
	}
}

static void *rt_thread(void *arg)
{
	struct thread_data *td_ptr = (struct thread_data *) arg;
	uint64_t max = 0, min = -1;
	long ret;

	td_ptr->ret = 0;

	/*
	 * Nohz is setup as follows
	 * - Assign 100% scheduler runtime to RT tasks
	 * - Affine CPU to isolated CPU
	 * - Set scheduling policy to FIFO with max priority
	 */
	if (set_cpu_affinity(td_ptr->cpu, 0) < 0) {
		printf("Thread [%d]:Error setting affinity to CPU %d\n",
				getpid(), td_ptr->cpu);
		td_ptr->ret = -1;
		return NULL;
	}

	if (set_sched_fifo(0) < 0) {
		printf("Thread [%d]:Error setting FIFO scheduling policy\n",
				getpid());
		td_ptr->ret = -1;
		return NULL;
	}

	/* First try without 'forced' and shorter wait */
	ret = nohz_wait(5000, 0);

	/* If failed, try with 'forced' and longer wait */
	if (ret < 0)
		ret = nohz_wait(5000000, 1);

	if (ret < 0) {
		printf("Thread [%d]:Error entering nohz state\n", getpid());
		td_ptr->ret = -1;
		return NULL;
	}

	for (int l = 0; l < num_loops; l++) {
		uint64_t start, end, diff;

		start = get_time();

		nohz_workload();

		end = get_time();

		diff = end - start;

		if (diff > max)
			max = diff;

		if (diff < min)
			min = diff;
	}

	td_ptr->jitter = max - min;

	return NULL;
}

static void help(void)
{
	printf("\nUsage:\n\nnohz_jitter [options]\n\n");
	printf("-a <cpu>         NOHZ CPU to run workload in\n");
	printf("-t <num tests>   Number of tests to run\n");
	printf("-l <num loops>   Number of loops per test\n");
	printf("-d <minutes>     Max duration in minutes\n");
	printf("-D <hours>       Max duration in hours\n");
	printf("-c               Use TSC instead of default clock\n");
	printf("-h               Generate histogram in nohz.hist file\n");
	printf("-H <file name>   Generate histogram in file with given name\n");
	printf("\n");
}

int parse_args(int argc, char **argv)
{
	int o;

	for (;;) {
		opterr = 0;
		o = getopt(argc, argv, "a:t:l:d:D:chH:");
		if (o == -1)
			break;

		if (o == '?' || optopt ||
				(optarg && optarg[0] == '-') ||
				(strchr("atldDH", o) && !optarg)) {
			help();

			return -1;
		}

		switch (o) {
		case 'a':
			nohz_cpu = atoi(optarg);
			if (!is_nohz_cpu(nohz_cpu)) {
				printf("Invalid NOHZ CPU\n");
				return -1;
			}
			break;
		case 't':
			num_tests = atoi(optarg);
			if (!num_tests) {
				printf("Invalid num tests\n");
				return -1;
			}
			break;
		case 'l':
			num_loops = atoi(optarg);
			if (!num_loops) {
				printf("Invalid num loops\n");
				return -1;
			}
			break;
		case 'd':
		case 'D':
			duration = atoi(optarg);
			if (!duration) {
				printf("Invalid duration\n");
				return -1;
			}
			if (o == 'D')
				duration *= 60;
			break;
		case 'c':
			use_tsc = 1;
			break;
		case 'h':
		case 'H':
			hist = 1;
			if (o == 'H') {
				hist_fd = fopen(optarg, "w");
				if (!hist_fd) {
					printf("Failed creating histogram file\n");
					return -1;
				}
			}
			break;
		}
	}

	if (hist && !hist_fd) {
		hist_fd = fopen(HIST_FILE, "w");
		if (!hist_fd) {
			printf("Failed creating histogram file\n");
			return -1;
		}
	}

	if (!nohz_cpu) {
		//Get the first nohz_full CPU
		nohz_cpu = get_nohz_full_cpu();
		if (nohz_cpu == -1) {
			printf("No nohz_full CPU found\n");
			return -1;
		}
	}

	return 0;
}

static void dump_opts(void)
{
	printf("NOHZ CPU : %d\n", nohz_cpu);
	if (duration) {
		printf("Max duration : %dm\n", duration);
		printf("Num tests : N/A\n");
	} else {
		printf("Max duration : N/A\n");
		printf("Num tests : %d\n", num_tests);
	}
	printf("Num loops : %d\n", num_loops);
	printf("Time unit : %s\n", use_tsc ? "TSC ticks" : "Nanoseconds");
	printf("Histogram : %s\n", hist_fd ? "Yes" : "No");
}

int main(int argc, char **argv)
{
	pthread_t tid;
	struct thread_data td;

	if (parse_args(argc, argv))
		goto ext;

#if PRINT_INFO
	dump_opts();
#endif

	printf("\nRT jitter measurement tool using TIF\n\n\t*** Press Ctrl-C to exit ***\n\n");

	if (signal(SIGINT, signal_handler) == SIG_ERR)
		printf("Error registering Ctrl-C handler\n");

	if (nohz_enter()) {
		printf("Error setting up NOHZ_FULL\n");
		goto ext;
	}

	for (;;) {
		if (duration) {
			if (time_expired())
				break;
		} else {
			if (tests_done >= num_tests)
				break;
		}

		td.cpu = nohz_cpu;

		if (pthread_create(&tid, NULL, &rt_thread, &td)) {
			printf("Error creating RT workload thread\n");
			exit(EXIT_FAILURE);
		}

		pthread_join(tid, NULL);
		if (td.ret == -1)
			goto ext;

		if (td.jitter > cmax)
			cmax = td.jitter;
		if (td.jitter < cmin)
			cmin = td.jitter;
		avg += td.jitter;
		tests_done++;
		print_jitter(td.jitter);

		if (hist_fd)
			fprintf(hist_fd, "%10u %10lu\n", tests_done, td.jitter);
	}

ext:
	cleanup();

	return 0;
}
