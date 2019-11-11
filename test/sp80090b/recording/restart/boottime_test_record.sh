#!/bin/bash
#
# Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
#
# Test for analyzing the boot time entropy by power cycling the test machine
# many times and record the first time stamps.
#
# Test execution:
#	1. Enable CONFIG_LRNG_TESTING and compile kernel and ensure that the
#	   Linux kernel command line contains lrng_testing.boot_test=1
#	2. Compile getrawentropy.c and install into /usr/local/sbin
#	3. Copy this file to /usr/local/sbin and make it executable and do not
#	   forget restorecon if applicable
#	4. Copy boottime_test_record.service to /etc/systemd/system/
#	5. systemctl enable boottime_test_record
#	6. reboot and wait until reboot test completes
#	7. Pick up $OUTFILE and analyze
#
# If you want to restart the test, do:
#	1. Clean out $OUTFILE
#	2. start with step 4 from above
#
# Test interruption:
#	boot with kernel command line option of boottime_test_stop
#
OUTFILE="/root/lrng_raw_noise_restart"
TESTS=1000

DIR=$(dirname $OUTFILE)
if [ ! -d "$DIR" ]
then
	mkdir -p $DIR
fi

testruns=$(ls $OUTFILE* | wc -l | cut -d" " -f1)

/usr/local/sbin/getrawentropy > $OUTFILE.$testruns.data

if [ $testruns -ge $TESTS ]; then
	systemctl stop boottime_test_record
	systemctl disable boottime_test_record

	uname -a > /root/platform.txt && 
	cat /proc/cpuinfo >> /root/platform.txt &&
	echo "" >> /root/platform.txt &&
	cat /proc/cpuinfo >> /root/platform.txt &&
	echo "" >> /root/platform.txt &&
	echo "lspci" >> /root/platform.txt &&
	lspci >> /root/platform.txt

	exit 0
fi

if (cat /proc/cmdline | grep -q boottime_test_stop) ; then
	exit 0
fi

# cannot kexec in VM (corruptions)
# Here's the snipped to run the VM:
# kvm -k de -vga vmware -usbdevice tablet -name bootloop -m 768 -smp 2 \
#      	-net nic,model=e1000,macaddr=00:50:45:00:34:0F -net user,hostfwd=tcp:127.0.0.1:24-:22
# 	-drive file=/vm-image-bootlooptests.img,format=raw,cache=writeback -boot c
#
mount -t proc proc /proc > /dev/null 2>&1
if ! grep hypervisor /proc/cpuinfo > /dev/null 2>&1 ; then
  if [ -f /boot/vmlinuz -a -f /boot/initrd ]; then
	e=$( cat /proc/cmdline)
	kexec -l /boot/vmlinuz --initrd=/boot/initrd --append="$e"
  fi
fi

# With kernel 4.9, the reboot may corrupt the file system.
# Hence, the following lines.
# Note, however, that it may be neccessary to enforce disc scan with outomatic repair on every reboot.

sync ; sync
mount -o remount,ro /

# kexec will only return upon error, like if not set up or fail of set up.
kexec -e

reboot -f

