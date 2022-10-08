#!/bin/bash
#
# Test for DRNG with prediction resistance operation
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
	local old_seed=$(dmesg | grep "seeding regular DRNG with" | tail -n1)

	if [ -z "$old_seed" ]
	then
		echo_fail "Cannot obtain initial seeding information"
		return 1
	fi

	#execute read with PR
	dd if=/dev/random of=/dev/null count=1 bs=32 iflag=sync
	if [ $? -ne 0 ]
	then
		echo_fail "Reading with O_SYNC from /dev/random failed"
		return 1
	fi

	local curr_seed=$(dmesg | grep "seeding regular DRNG with" | tail -n1)
	if [ -z "$curr_seed" ]
	then
		echo_fail "Cannot obtain current seeding information"
		return 1
	fi

	if [ "$old_seed" != "$curr_seed" ]
	then
		echo_pass "Reading with O_SYNC from /dev/random caused a reseed"
	else
		echo_pass "Reading with O_SYNC from /dev/random did not cause a reseed"
		return 1
	fi
}

pr_verification()
{
	$(check_testing)
	if [ $? -eq 0 ]
	then
		echo_pass "DRNG with PR: successful testing performed 1st time"
	else
		echo_fail "DRNG with PR: testing unsuccessful 1st time"
	fi

	# Check it again to verify that it is not just a spurious reseed
	$(check_testing)
	if [ $? -eq 0 ]
	then
		echo_pass "DRNG with PR: successful testing performed 2nd time"
	else
		echo_fail "DRNG with PR: testing unsuccessful 2nd time"
	fi

	# Check it again to verify that it is not just a spurious reseed
	$(check_testing)
	if [ $? -eq 0 ]
	then
		echo_pass "DRNG with PR: successful testing performed 3rd time"
	else
		echo_fail "DRNG with PR: testing unsuccessful 3rd time"
	fi
}

create_irqs()
{
	local i=0
	while [ $i -lt 256 ]
	do
		echo "Test message - ignore" >&2
		i=$(($i+1))
	done

	dd if=/bin/bash of=$HOMEDIR/reseed.tmp oflag=sync
	rm -f $HOMEDIR/reseed.tmp
	sync

	dd if=/dev/urandom of=/dev/null bs=32 count=1
}

cause_reseed()
{
	echo > /dev/random
	dd if=/dev/random of=/dev/null bs=32 count=1 > /dev/null 2>&1
}

pr_fail_reseed()
{
	local i=0

	#drain entropy pool
	while [ $i -lt 100 ]
	do
		let i=$(($i+1))
		if [ $(cat /proc/lrng_type  | grep -A 2 IRQ  | tail -n1 | cut -d ":" -f2) -lt 0 ]
		then
			break
		fi
		cause_reseed
	done

	if [ $i -ge 100 ]
	then
		echo_deact "DRNG with PR: cannot drain entropy rate"
		return 0
	fi

	#execute read with PR
	local filesize=$(dd if=/dev/random of=/dev/null count=1 bs=32 iflag=sync 2>&1 | tail -n1  | cut -f1 -d" ")
	if [ $? -le 32 ]
	then
		echo_fail "Reading with O_SYNC from /dev/random failed, obtained $filesize"
		return 1
	fi

	if [ "$filesize" -eq 0 ]
	then
		echo_pass "DRNG with PR: no random number generated without reseeding"
	else
		echo_fail "DRNG with PR: random number generated without reseeding"
	fi

	create_irqs

	# Check with sufficient entropy
	$(check_testing)
	if [ $? -eq 0 ]
	then
		echo_pass "DRNG with PR: successful testing performed with sufficient entropy"
	else
		echo_fail "DRNG with PR: testing unsuccessful with sufficient entropy"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			pr_verification
			;;
		"test2")
			pr_fail_reseed
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	#
	# Validating prediction resistance functionality causing an immediate reseed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=256"

	#
	# Validating prediction resistance functionality causing a delayed reseed
	#
	# TODO - somehow draining of the entropy source does not work reliably
# 	write_cmd "test2"
# 	execvirt $(full_scriptname $0) "lrng_es_jent.jent_entropy=8 fips=0 numa=fake=4"
fi
