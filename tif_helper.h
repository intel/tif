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

#ifndef _TIF_HELPER_H
#define _TIF_HELPER_H

long nohz_wait(long msecs, int forced);
int nohz_enter(void);
void nohz_exit(void);

int set_sched_fifo(int pid);
int set_cpu_affinity(int cpu, int pid);
int get_nohz_full_cpu(void);
int is_nohz_cpu(int cpu);

#endif //#ifndef _TIF_HELPER_H
