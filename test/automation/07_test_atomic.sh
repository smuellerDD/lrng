#!/bin/bash
#
# Test for LRNG operation in atomic contexts
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

ATOMIC_DIR="../atomic"
$(in_hypervisor)
if [ $? -eq 1 ]
then
	TESTMODDIR="$(dirname $0)/$ATOMIC_DIR"
else
	TESTMODDIR="$(full_scriptname $ATOMIC_DIR)"
fi
echo $TESTMODDIR
TESTMOD="getrandom.ko"

load_testmod()
{
	modprobe lrng_drbg

	if [ ! -d "$TESTMODDIR" ]
	then
		echo_deact "Atomic: Kernel module directory $TESTMODDIR not found"
		return
	fi

	insmod $TESTMODDIR/$TESTMOD

	# We expect EAGAIN
	if [ $? -ne 1 ]
	then
		echo_fail "Atomic: Kernel module remains in kernel"
	else
		echo_pass "Atomic: LRNG executing in atomic contexts"
	fi

	local atomic=$(dmesg | grep "seeding atomic DRNG with")

	if [ -z "$atomic" ]
	then
		atomic=""
	fi

	# Check that the atomic DRNG is automatically reseeded
	# This only happens if the atomic DRNG != the initial DRNG
	# which happens if, say, lrng_drbg is loaded
	echo > /dev/random
	dd if=/dev/urandom of=/dev/null bs=32 count=1 > /dev/null 2>&1
	sleep 1
	local atomic2=$(dmesg | grep "seeding atomic DRNG with")

	if [ -z "$atomic2" ]
	then
		atomic2=""
	fi

	if [ "$atomic" != "$atomic2" ]
	then
		echo_pass "Atomic: seeding of atomic DRNG performed"
	else
		echo_fail "Atomic: seeding of atomic DRNG not performed"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			load_testmod
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "Atomic: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deakt "Atomic: Jitter RNG not enabled"
		exit
	fi

	if [ ! -d "$TESTMODDIR" ]
	then
		echo_deact "Atomic: Kernel module directory $TESTMODDIR not found"
		exit
	fi

	make -C "${KERNEL_BASE}/${TESTKERN}" M=$TESTMODDIR
	if [ $? -ne 0 ]
	then
		echo_fail "Atomic: Kernel module cannot be compiled"
		exit
	fi

	#
	# Validating FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jitterrng=256 lrng_archrandom.archrandom=256"

	#
	# Validating non-FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_jent.jitterrng=256 lrng_archrandom.archrandom=256"

	#
	# Validating NTG mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "lrng_es_mgr.ntg1=1 lrng_es_jent.jitterrng=256 lrng_archrandom.archrandom=256"

	#
	# Validating NTG and FIPS mode
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_mgr.ntg1=1 lrng_es_jent.jitterrng=256 lrng_archrandom.archrandom=256"

	make -C "${KERNEL_BASE}/${TESTKERN}" M=$TESTMODDIR clean
fi
