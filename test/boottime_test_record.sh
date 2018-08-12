#!/bin/bash
#
# Copyright (C) 2016, Stephan Mueller <smueller@chronox.de>
#
# Test for analyzing the boot time entropy by power cycling the test machine
# many times and record the first time stamps.
#
# Test execution:
#	1. Apply patch boottime_test.diff and compile LRNG code
#	2. Copy this file to /usr/local/sbin and make it executable and do not
#	   forget restorecon if applicable
#	3. Copy boottime_test_record.service to /etc/systemd/system/
#	4. systemctl enable boottime_test_record
#	5. reboot and wait until reboot test completes
#	6. Pick up $OUTFILE and analyze
#
# If you want to restart the test, do:
#	1. Clean out $OUTFILE
#	2. start with step 4 from above
#
# Test interruption:
#	boot with kernel command line option of boottime_test_stop
#
OUTFILE="/home/sm/boottime_test.out"
TESTS=50000

dmesg | grep "lrng_boot_test" | cut -d":" -f2 >> $OUTFILE
testruns=$(wc -l $OUTFILE | cut -d" " -f1)

if [ $testruns -gt $TESTS ]; then
	systemctl stop boottime_test_record
	systemctl disable boottime_test_record
fi

if (cat /proc/cmdline | grep -q boottime_test_stop) ; then
	exit 0
fi

reboot
