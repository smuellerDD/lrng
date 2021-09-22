#!/bin/bash
#
# Test for SP800-90B compliance
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

check_fully_post_completed()
{
	local i=0
	while [ $i -lt 10 ]
	do
		if (dmesg | grep -q "lrng_health: SP800-90B startup health tests completed")
		then
			return 0
		fi

		dd if=/bin/bash of=$HOMEDIR/sp80090b.tmp oflag=sync
		rm -f $HOMEDIR/sp80090b.tmp

		i=$((i+1))
	done

	return 1
}

sp80090b_compliance()
{
	$(check_fully_post_completed)
	if [ $? -eq 0 ]
	then
		echo_pass "SP800-90B: Power-up health test successfully completed"
	else
		echo_fail "SP800-90B: Power-up health test failed"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			sp80090b_compliance
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_IRQ=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_CPU=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_CRYPTO_FIPS=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_HEALTH_TESTS=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90B: tests skipped"
		exit
	fi

	#
	# Validating Jitter RNG and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jitterrng=256"

	#
	# Validating Jitter RNG and IRQ ES providing sufficient seed
	# Note: Check that NTG.1 setup does not interfere
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_jent.jitterrng=256 lrng_es_mgr.ntg1=1"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_archrandom.archrandom=256 lrng_es_jent.jitterrng=256"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	# Note: Check that NTG.1 setup does not interfere
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_archrandom.archrandom=256 lrng_es_jent.jitterrng=256 lrng_es_mgr.ntg1=1"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	# Note: CPU ES provides full seed with a different command line option
	# Note: Check that NTG.1 setup does not interfere
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 random.trust_cpu=1 lrng_es_jent.jitterrng=256"

	#
	# Validating Jitter RNG, CPU and IRQ ES providing sufficient seed
	# Note: CPU ES provides full seed with a different command line option
	# Note: Check that NTG.1 setup does not interfere
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 random.trust_cpu=1 lrng_es_jent.jitterrng=256 lrng_es_mgr.ntg1=1"

	#
	# Validating CPU and IRQ ES providing sufficient seed
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 lrng_es_archrandom.archrandom=256"

	#
	# Validating CPU and IRQ ES providing sufficient seed
	# Note: CPU ES provides full seed with a different command line option
	#
	write_cmd "test1"
	execvirt $(full_scriptname $0) "fips=1 random.trust_cpu=1"
fi
