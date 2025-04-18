#!/bin/bash
#
# Test for forced seeding operation
#
# Copyright (C) 2022, Stephan Mueller <smueller@chronox.de>
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

TESTNAME="Forced Seeding"

verify_forced()
{

	if (dmesg | grep -q "lrng_drng_mgr: Force fully seeding level for")
	then
		echo_pass "$TESTNAME: Forced seeding triggered"
	else
		echo_fail "$TESTNAME: Forced seeding not triggered"
	fi

	if (grep -q "LRNG fully seeded: true" /proc/lrng_type)
	then
		echo_pass "$TESTNAME: Forced seeding results in fully seeded DRNG"
	else
		echo_fail "$TESTNAME: Forced seeding does not result in fully seeded DRNG"
	fi
}

verify_forced_ntg1()
{

	if (dmesg | grep -q "lrng_drng_mgr: Force fully seeding level for")
	then
		echo_fail "$TESTNAME: Forced seeding triggered"
	else
		echo_pass "$TESTNAME: Forced seeding not triggered"
	fi

	if (grep -q "LRNG fully seeded: true" /proc/lrng_type)
	then
		echo_pass "$TESTNAME: Forced seeding results in fully seeded DRNG"
	else
		echo_fail "$TESTNAME: Forced seeding does not result in fully seeded DRNG"
	fi
}

verify_forced_failed()
{

	if (dmesg | grep -q "lrng_drng_mgr: Force fully seeding level by repeatedly pull entropy from available entropy sources")
	then
		echo_fail "$TESTNAME: Forced seeding triggered"
	else
		echo_pass "$TESTNAME: Forced seeding not triggered"
	fi

	if (grep -q "LRNG fully seeded: true" /proc/lrng_type)
	then
		echo_fail "$TESTNAME: Forced seeding results in fully seeded DRNG"
	else
		echo_pass "$TESTNAME: Forced seeding does not result in fully seeded DRNG"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			verify_forced
			;;
		"test2")
			verify_forced_failed
			;;
		"test3")
			verify_forced_ntg1
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	#
	# Validating the operation of the forced seeding
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=16 lrng_es_cpu.cpu_entropy=8 lrng_es_sched.sched_entropy=4294967295 lrng_es_irq.irq_entropy=4294967295 lrng_es_krng.krng_entropy=0"

	$(check_kernel_config "CONFIG_LRNG_AIS2031_NTG1_SEEDING_STRATEGY=y")
	if [ $? -ne 0 ]
	then
		echo_deact "$TESTNAME: tests skipped"
		exit
	fi
	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deact "$TESTNAME: tests skipped"
		exit
	fi
	$(check_kernel_config "CONFIG_LRNG_CPU=y")
	if [ $? -ne 0 ]
	then
		echo_deact "$TESTNAME: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "$TESTNAME: tests skipped"
		exit
	fi

	#
	# Validating the operation of the forced seeding with NTG.1 2024
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=16 lrng_es_cpu.cpu_entropy=8 lrng_es_sched.sched_entropy=4294967295 lrng_es_irq.irq_entropy=4294967295 lrng_es_krng.krng_entropy=0 lrng_es_mgr.ntg1=1"

	#
	# Validating the operation of the forced seeding with NTG.1 2024
	# fails when only one ES present
	#
	write_cmd "test2"
	execvirt_cpu $(full_scriptname $0) "1" "lrng_es_jent.jent_entropy=16 lrng_es_cpu.cpu_entropy=0 lrng_es_sched.sched_entropy=4294967295 lrng_es_irq.irq_entropy=4294967295 lrng_es_krng.krng_entropy=0 lrng_es_mgr.ntg1=1"

	#
	# Validating the operation of the forced seeding with NTG.1 2024
	# fails when only one ES present
	#
	write_cmd "test3"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=240 lrng_es_cpu.cpu_entropy=240 lrng_es_sched.sched_entropy=4294967295 lrng_es_irq.irq_entropy=4294967295 lrng_es_krng.krng_entropy=0 lrng_es_mgr.ntg1=1"
fi
