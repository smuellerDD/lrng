#!/bin/bash
#
# Test for /proc/sys/kernel/random/write_wakeup_threshold operation
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

TESTNAME="Write threshold"

write_threshold_file="/proc/sys/kernel/random/write_wakeup_threshold"

threshold_verification()
{
	local thresh=$(cat $write_threshold_file)

	# Check for default value
	if [ $thresh -eq 256 ]
	then
		echo_pass "$TESTNAME: expected initial value found"
	else
		echo_fail "$TESTNAME: expected initial value not found"
	fi

	# Default hash is SHA-256 -> threshold cannot be larger than 256
	echo 300 > $write_threshold_file 2>/dev/null
	thresh=$(cat $write_threshold_file)
	if [ $thresh -eq 256 ]
	then
		echo_pass "$TESTNAME: expected initial value found after wrong update attempt"
	else
		echo_fail "$TESTNAME: expected initial value not found"
	fi

	# Default hash is SHA-256 -> threshold can be lower than 256
	echo 100 > $write_threshold_file 2>/dev/null
	thresh=$(cat $write_threshold_file)
	if [ $thresh -eq 100 ]
	then
		echo_pass "$TESTNAME: expected updated value found"
	else
		echo_fail "$TESTNAME: expected updated value not found"
	fi

	# Insert DRBG with its SHA-512 -> threshold max is 512
	# Check that low value is unchanged as it does not violate threshold max
	modprobe lrng_drbg
	thresh=$(cat $write_threshold_file)
	if [ $thresh -eq 100 ]
	then
		echo_pass "$TESTNAME: expected updated value after inserting DRBG found"
	else
		echo_fail "$TESTNAME: expected updated value not found"
	fi

	# SHA-512 -> threshold can be up to 512
	echo 300 > $write_threshold_file 2>/dev/null
	thresh=$(cat $write_threshold_file)
	if [ $thresh -eq 300 ]
	then
		echo_pass "$TESTNAME: expected updated value found"
	else
		echo_fail "$TESTNAME: expected updated value not found"
	fi

	rmmod lrng_drbg

	# Now we have SHA-256 again -> kernel must automatically reduce value
	# to new max
	thresh=$(cat $write_threshold_file)
	if [ $thresh -eq 256 ]
	then
		echo_pass "$TESTNAME: expected kernel-updated value found"
	else
		echo_fail "$TESTNAME: expected kernel-updated value not found"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			threshold_verification
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_DRBG=m")
	if [ $? -ne 0 ]
	then
		echo_deact "$TESTNAME: testing skipped"
		return
	fi

	#
	# Validating threshold functionality
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0)
fi
