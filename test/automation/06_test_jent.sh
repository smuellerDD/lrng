#!/bin/bash
#
# Test for Jitter RNG operation
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

SYSFS="/sys/module/lrng_es_jent/parameters/jent_entropy"
RESEED_CTR=1

verify_entropyrate()
{
	local expected_rate=$1
	local i=0

	while [ $i -lt $RESEED_CTR ]
	do
		# Force reseed
		echo >/dev/random; dd if=/dev/random of=/dev/null bs=32 count=1 > /dev/null 2>&1
		i=$((i+1))
		echo "Async Jitter RNG reseed: Reseed operation $i of $RESEED_CTR"
	done

	local found=$(dmesg | grep "lrng_es_jent: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')

	if [ -z "$found" ]
	then
		echo_deact "Jitter RNG: Cannot obtain entropy rate from kernel log"
	else
		if [ $expected_rate -eq $found ]
		then
			echo_pass "Jitter RNG: Verified entropy rate $expected_rate with kernel log"
		else
			echo_fail "Jitter RNG: Entropy rate $expected_rate does not match with kernel log: $found"
		fi
	fi

	local found=$(cat $SYSFS)
	if [ -z "$found" ]
	then
		echo_deact "Jitter RNG: Cannot obtain entropy rate from SysFS file $SYSFS"

	# The SysFS file is not reset in case of an overflow
	elif [ $found -le 256 ]
	then
		if [ $expected_rate -eq $found ]
		then
			echo_pass "Jitter RNG: Verified entropy rate $expected_rate with SysFS file $SYSFS"
		else
			echo_fail "Jitter RNG: Entropy rate $expected_rate does not match with SysFS: $found"
		fi
	fi
}

verify_entropyrate_boot()
{
	local rate=16

	for i in $(cat /proc/cmdline)
	do
		if (echo $i | grep -q jent_entropy)
		then
			rate=$(echo $i | cut -d"=" -f 2)
			break
		fi
	done

	verify_entropyrate $rate
}

verify_set_entropyrate()
{
	local rate=$1

	echo $rate > $SYSFS
	verify_entropyrate $rate
}

verify_set_entropyrate_overflow()
{
	local rate=$1

	echo $rate > $SYSFS
	verify_entropyrate 256
}

verify_jent()
{
	if (dmesg | grep -q "lrng_es_jent: Jitter RNG working on current system")
	then
		echo_pass "Jitter RNG: Jitter RNG working on system"
	else
		echo_deact "Jitter RNG: Jitter RNG not working on system"
		return
	fi

	# Force reseed
	echo >/dev/random; dd if=/dev/random of=/dev/null bs=32 count=1

	if (dmesg | grep -q "lrng_es_jent: obtained")
	then
		echo_pass "Jitter RNG: used for seeding"
	else
		echo_fail "Jitter RNG: not used for seeding"
		return
	fi

	verify_entropyrate_boot
	verify_set_entropyrate 1
	verify_set_entropyrate 10
	verify_set_entropyrate 256

	verify_set_entropyrate_overflow 257
	verify_set_entropyrate_overflow 1000
	verify_set_entropyrate_overflow 16384
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_32=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=32
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_64=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=64
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_128=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=128
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_256=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=256
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_512=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=512
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT_ENTROPY_BLOCKS_NO_1024=y")
	if [ $? -eq 0 ]
	then
		RESEED_CTR=1024
	fi

	case $(read_cmd) in
		"test1")
			verify_jent
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deact "Jitter RNG: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "Jitter RNG: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deakt "Jitter RNG: Jitter RNG not enabled"
		exit
	fi

	#
	# Validating FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	#
	# Validating non-FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	#
	# Validating NTG mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"

	#
	# Validating NTG and FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jent_entropy=256 lrng_es_cpu.cpu_entropy=256"
fi
