#!/bin/bash
#
# Test for selftest operation
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

check_testing()
{
	local state=$(dmesg | grep "lrng_selftest: LRNG self-tests")
	if [ -z "$state" ]
	then
		echo_fail "Selftest: No LRNG self test found"
		return 1
	fi

	if (echo $state | grep -q "passed")
	then
		return 0
	else
		return 1
	fi
}

selftest_sysfsfile="/sys/module/lrng_selftest/parameters/selftest_status"
check_test_state()
{
	if [ ! -f "$selftest_sysfsfile" ]
	then
		echo_fail "Selftest: SysFS file $selftest_sysfsfile not present"
		return 1
	fi

	state=$(cat $selftest_sysfsfile)
	echo_log "Selftest: SysFS file $selftest_sysfsfile content: $state"

	if [ -z "$state" ]
	then
		echo_fail "Selftest: SysFS file $selftest_sysfsfile empty"
		return 1
	fi

	return $state
}

rerun_selftest()
{
	echo 1 > $selftest_sysfsfile
	if [ $? -ne 0 ]
	then
		echo_fail "Cannot write into SysFS file $selftest_sysfsfile"
		return 1
	fi

	local testnum=$(dmesg | grep "lrng_selftest: LRNG self-tests" | wc -l)
	if [ $testnum -eq 2 ]
	then
		return 0
	else
		return 1
	fi
}

selftest_verification()
{
	$(check_testing)
	if [ $? -eq 0 ]
	then
		echo_pass "Selftest: successful testing performed"
	else
		echo_fail "Selftest: testing unsuccessful"
	fi

	$(check_test_state)
	if [ $? -eq 0 ]
	then
		echo_pass "Selftest: SysFS status file checked"
	else
		echo_fail "Selftest: SysFS status file unsuccessful"
	fi

	$(rerun_selftest)
	if [ $? -eq 0 ]
	then
		echo_pass "Selftest: Selftests rerun"
	else
		echo_fail "Selftest: Selftests cannot be rerun"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			selftest_verification
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_SELFTEST=y")
	if [ $? -ne 0 ]
	then
		echo_deact "Selftest: tests skipped"
		return
	fi

	#
	# Validating self test functionality
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0)
fi
