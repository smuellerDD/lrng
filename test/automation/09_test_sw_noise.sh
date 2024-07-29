#!/bin/bash
#
# Test for SW noise operation
#
# Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
#
# License: see LICENSE file in root directory
#
# THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
# WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
# BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
# DAMAGE.
#

. $(dirname $0)/libtest.sh

SYSFS="/sys/module/lrng_es_irq/parameters/irq_entropy"

cap_rate()
{
	local rate=$1

	local sp80090c=$(cat /proc/lrng_type | grep "Standards compliance:" | head -n1 | cut -d":" -f 2)
	sp80090c=$(echo $sp80090c)

	local osr=0

	if ( echo $sp80090c | grep -q "SP800-90C")
	then
		osr=64
	fi

	local exprate=$((256-$osr))

	# Hash has max entropy rate of 256 bits - reduce by SP800-90C OSR
	if [ $rate -gt $exprate ]
	then
		rate=192
	fi
	echo $rate
}

verify_entropyrate()
{
	local expected_sysfs=$1

	#convert any hex to decimal
	expected_sysfs=$(printf "%u" $expected_sysfs)

	# Generate some entropy
	dd if=/bin/bash of=$HOMEDIR/irq_noise.tmp oflag=sync
	rm -f $HOMEDIR/irq_noise.tmp

	# Force reseed
	echo > /dev/random; dd if=/dev/random of=/dev/null bs=32 count=1

	# This works only because we only have one CPU
	local found=$(dmesg | grep "lrng_es_irq: .* interrupts " | tail -n 1 | sed 's/^.*: \([0-9]\+\) interrupts.*$/\1/')

	local expected_irq_rate=$(($found*$LRNG_SEC_STRENGTH/$expected_sysfs))
	expected_irq_rate=$(cap_rate $expected_irq_rate)

	found=$(dmesg | grep "lrng_es_irq: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	if [ -z "$found" ]
	then
		echo_deact "SW ES: Cannot obtain entropy rate from kernel log"
	else
		if [ $expected_irq_rate -eq $found ]
		then
			echo_pass "SW ES: Verified entropy rate $expected_irq_rate with kernel log"
		else
			echo_fail "SW ES: Entropy rate $expected_irq_rate does not match with kernel log: $found"
		fi
	fi

	found=$(cat $SYSFS)
	if [ -z "$found" ]
	then
		echo_deact "SW ES: Cannot obtain entropy rate from SysFS file $SYSFS"

	else
		if [ $expected_sysfs -eq $found ]
		then
			echo_pass "SW ES: Verified entropy rate $expected_sysfs with SysFS file $SYSFS"
		else
			echo_fail "SW ES: Entropy rate $expected_sysfs does not match with SysFS: $found"
		fi
	fi
}

verify_entropyrate_boot()
{
	local rate=256

	for i in $(cat /proc/cmdline | sed 's/*//')
	do
		if (echo $i | grep -q irq_entropy)
		then
			rate=$(echo $i | cut -d"=" -f 2)
			break
		fi
	done

	verify_entropyrate $rate
}

verify_entropyrate_boot_underflow()
{
	local rate=256

	for i in $(cat /proc/cmdline | sed 's/*//')
	do
		if (echo $i | grep -q irq_entropy)
		then
			rate=$(echo $i | cut -d"=" -f 2)
			break
		fi
	done

	if [ $rate -ge 256 ]
	then
		echo_deact "SW ES: Underflow test skipped due wrong invocation"
		return
	fi

	echo_log "SW ES: Verify underflow handling for configured rate $rate"

	# In case of an underflow, the LRNG must set the rate to 256
	verify_entropyrate 256
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	LRNG_SEC_STRENGTH=$(grep "LRNG security strength in bits:" /proc/lrng_type | cut -d":" -f2)
	case $(read_cmd) in
		"test1")
			verify_entropyrate_boot
			;;
		"test2")
			verify_entropyrate_boot_underflow
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_IRQ=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SW ES: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_CPU=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SW ES: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SW ES: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_IRQ_ENTROPY_RATE=256")
	if [ $? -ne 0 ]
	then
		echo_deact "SW ES: tests skipped - configure IRQ entropy rate to 256 bits"
		exit
	fi

	#
	# Validating FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=512"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=1024"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=0xffffffff"

	#
	# Validating non-FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=2048"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=0xffffffff"

	#
	# Validating NTG mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=2048"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256  lrng_es_irq.irq_entropy=0xffffffff"

	#
	# Validating NTG and FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=256"

	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=0xffffffff"

	#
	# Underflow
	#
	write_cmd "test2"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=0"

	write_cmd "test2"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=128"

	write_cmd "test2"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256 lrng_es_irq.irq_entropy=255"
fi
