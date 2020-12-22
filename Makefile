#SPDX-License-Identifier: GPL-2.0-or-later
#Copyright (C) 2020 Intel Corporation

all:
	# NUMA library must be present.
	gcc -Wall -O2 tif_jitter.c tif_workload.c tif_helper.c -lnuma -pthread -o tif_jitter

example:
	gcc -Wall -O2 tif_example.c tif_helper.c -lnuma -o tif_example

test:
	gcc -Wall -O2 tif_test.c tif_helper.c -lnuma -o tif_test

clean:
	rm -f tif_jitter tif_example tif_test
