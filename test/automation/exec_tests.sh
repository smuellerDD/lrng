#!/bin/bash

. $(dirname $0)/libtest.sh

rm -f $FAILUREFILE*

for i in ??_*.sh
do
	./$i
done

echo_final
exit_test
