# Thread Isolation Framework

Framework for setting up CPU thread isolation environment and synchronize entry into
nohz state. The tif_jitter application demonstrates use of the framework. Also includes
a bare bones instructive example (tif_example.c).

Building tif_jitter:
make

Building tif_example:
make example

Files:
Framework - tif_helper.c and tif_helper.h
Workload - tif_workload.c (Can be replaced with oher workloads)
Jtter tool - tif_jitter.c
Simple example - tif_example.c

Uses NUMA library. Use gcc option -lnuma
Requires NUMA library to be present. Install as follows if not present.
Debian/Ubuntu:
apt-get install libnuma-dev
RHEL/CentOs/Fedora:
yum install numactl-devel

Uses pthreads. Use gcc option -pthread

<pre>
Usage:

tif_jitter [options]

-a &lt;cpu&gt;         NOHZ CPU to run workload in
-t &lt;num tests&gt;   Number of tests to run
-l &lt;num loops&gt;   Number of loops per test
-d &lt;minutes&gt;     Max duration in minutes
-D &lt;hours&gt;       Max duration in hours
-c               Use TSC instead of default clock
-h               Generate histogram in nohz.hist file
-H &lt;file name>   Generate histogram in file with given name
</pre>

All the options are optional. If no CPU is passed, the tool will pick the first
NOHZ CPU.

Number of tests (-t) and duration (-d or -D) are mutually exclusive. Duration
option takes precedence.

Number of times to run the workload per test can be specified with (-l) option.

Default is clock time in nanoseconds which is more intuitive. Use (-c) option
to use TSC ticks if desired.

Histogram can be generated with option (-h or -H). (-h) will generate in a
filed named "tif.hist". (-H) can be used to specify a custom file name.

The program outputs running max, min and mean jitter. The histogram output
can be used to plot graphs and calculate median.

Example output:

`./tif_jitter -d1`
<pre>
NOHZ CPU : 1
Max duration : 1m
Num tests : N/A
Num loops : 1000
Time unit : Nanoseconds
Histogram : No

RT jitter measurement tool using NOHZ_FULL state

        *** Press Ctrl-C to exit ***

                (Jitter in nanoseconds)
     Test#     Jitter        Max        Min       Mean
-------------------------------------------------------
        24       2300       2848        520       2259
</pre>

NOHZ state setup and workload:
Workload is a function named tif_workload in the tif_workload.c file.
It can be replaced with other workloads. The default one reads and writes to
memory several times.

The sequence of setting up and synchronizing entry into nohz state is shown
in the tif_example.c. Following are the steps.

The tif_jitter runs RT task in its own thread. This thread does not do prints
or makes system calls. This is to ensure there is no interruption causing
jitter.

A known issue in the PREEMPT_RT kernel causes entry into nohz state to fail at
times. The 'forced' parameter of nohz_wait function helps get around the issue.
Due to this, anytime the application calls a function that causes the thread to get
scheduled out (e.g. sleep, system calls), the nohz_wait function needs to be
called again to ensure nohz state is reentered.

1. nohz_enter - Sets 100% scheduler runtime for RT tasks
2. set_cpu_affinity - Affine RT thread to a NOHZ CPU
3. set_sched_fifo - Sets RT thread to FIFO scheduler policy with max priority
4. nohz_wait - Wait till nohz state is entered. Use 'forced' option for PREEMPT_RT kernel.
5. Run RT workload
6. nohz_exit - Reverses the 100% scheduler runtime setting
