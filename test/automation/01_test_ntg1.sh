#!/bin/bash
#
# Test for NTG.1 compliance
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

check_fully_seeded()
{
	if (dmesg | grep -q "lrng_es_mgr: LRNG fully seeded with")
	then
		return 0
	else
		return 1
	fi
}

check_total_seed()
{
	seed_bits=$(dmesg | grep "lrng_es_mgr: LRNG fully seeded with" | sed 's/^.* \([0-9]\+\) bits.*$/\1/')

	if [ $seed_bits -lt 440 ]
	then
		return 1
	else
		return 0
	fi
}

check_ntg1_flag()
{
	if (grep "Standards compliance" /proc/lrng_type | grep "NTG.1" |  grep -q "2022")
	then
		return 0
	else
		return 1
	fi
}

check_two_es()
{
	local jent_bits=$(dmesg | grep "lrng_es_jent: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	local irq_bits=$(dmesg | grep "lrng_es_irq: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	local cpu_bits=$(dmesg | grep "lrng_es_cpu: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	local aux_bits=$(dmesg | grep "lrng_es_aux: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	local sched_bits=$(dmesg | grep "lrng_es_sched: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')

	echo_log "Jitter RNG ES seed bits: $jent_bits"
	echo_log "Interrupt ES seed bits: $irq_bits"
	echo_log "CPU ES seed bits: $cpu_bits"
	echo_log "Auxiliary ES seed bits: $aux_bits"
	echo_log "Sched ES seed bits: $sched_bits"

	found=0
	if [ $jent_bits -ge 220 ]
	then
		found=$((found+1))
	fi
	if [ $irq_bits -ge 220 ]
	then
		found=$((found+1))
	fi
	if [ $cpu_bits -ge 220 ]
	then
		found=$((found+1))
	fi
	if [ $aux_bits -ge 220 ]
	then
		found=$((found+1))
	fi
	if [ $sched_bits -ge 220 ]
	then
		found=$((found+1))
	fi

	if [ $found -ge 2 ]
	then
		return 0
	else
		return 1
	fi
}

two_es_available()
{
	$(check_ntg1_flag)
	if [ $? -eq 0 ]
	then
		echo_pass "NTG.1: standards flag present"
	else
		echo_fail "NTG.1: standards flag not present"
	fi

	$(check_fully_seeded)
	if [ $? -ne 0 ]
	then
		echo_deact "NTG.1: Fully seeding level not reached"
		return
	fi

	$(check_total_seed)
	if [ $? -eq 0 ]
	then
		echo_pass "NTG.1: Total seed equal or above 440"
	else
		echo_fail "NTG.1: Total seed below 440"
	fi

	$(check_two_es)
	if [ $? -eq 0 ]
	then
		echo_pass "NTG.1: Two ES deliver equal or more 220 bits"
	else
		echo_fail "NTG.1: Two ES do not deliver equal or more 220 bits"
	fi
}

one_es_available()
{
	$(check_ntg1_flag)
	if [ $? -eq 0 ]
	then
		echo_pass "NTG.1: standards flag present"
	else
		echo_fail "NTG.1: standards flag not present"
	fi

	$(check_fully_seeded)
	if [ $? -eq 1 ]
	then
		echo_pass "NTG.1: Fully seeding level with one ES not reached"
	else
		echo_fail "NTG.1: Fully seeding level with one ES reached"
	fi

	$(check_two_es)
	if [ $? -eq 1 ]
	then
		echo_pass "NTG.1: Two ES do not deliver equal or more 220 bits"
	else
		echo_fail "NTG.1: Two ES deliver equal or more 220 bits"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			two_es_available
			;;
		"test2")
			one_es_available
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "NTG.1: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_AIS2031_NTG1_SEEDING_STRATEGY=y")
	if [ $? -ne 0 ]
	then
		echo_deact "NTG.1: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_CRYPTO_FIPS=y")
	if [ $? -ne 0 ]
	then
		echo_deact "NTG.1: tests skipped"
		exit
	fi

	#
	# Validating Jitter RNG and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256 lrng_es_mgr.ntg1=1"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_cpu.cpu_entropy=256 lrng_es_jent.jent_entropy=256 lrng_es_mgr.ntg1=1"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed and
	# that FIPS mode does not interfere
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_cpu.cpu_entropy=256 lrng_es_jent.jent_entropy=256 lrng_es_mgr.ntg1=1"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	# Note: CPU ES provides full seed with a different command line option
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "random.trust_cpu=1 lrng_es_jent.jent_entropy=256 lrng_es_mgr.ntg1=1"

	#
	# Validating CPU and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_cpu.cpu_entropy=256 lrng_es_mgr.ntg1=1"

	#
	# Validating CPU and IRQ ES providing sufficient seed
	# Note: CPU ES provides full seed with a different command line option
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "random.trust_cpu=1 lrng_es_mgr.ntg1=1"

	#
	# Validating that IRQ ES alone does not provide sufficient seed
	#
	write_cmd "test2"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_cpu.cpu_entropy=8 lrng_es_jent.jent_entropy=8"
fi
