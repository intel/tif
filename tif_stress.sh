#!/bin/bash

SECONDS=0
count=0
echo
while [ true ]
do 
	echo "Test#" $count
	./tif_test

	if [ $? -ne 0 ]
	then
		echo "Reproduced NOHZ_FULL failure after" $count "tries!!!"
		echo "Test elapsed time:" $SECONDS "seconds"
		break
	fi
	((count=count+1))
	printf "\033[1A\033[1A"
done
