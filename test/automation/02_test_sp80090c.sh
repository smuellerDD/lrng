#!/bin/bash
#
# Test for SP800-90C compliance
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

create_irqs()
{
	dd if=/bin/bash of=$HOMEDIR/sp80090b.tmp oflag=sync
	rm -f $HOMEDIR/sp80090b.tmp
	sync

	local i=0
	while [ $i -lt 256 ]
	do
		echo "Test message - ignore" >&2
		i=$(($i+1))
	done

	dd if=/dev/urandom of=/dev/null bs=32 count=1
}

# This function assumes that either the Jitter RNG or the CPU entropy source
# provides full entropy. It returns therefore the maximum of both values.
check_normal_seed()
{
	modprobe lrng_drbg
	if [ $? -ne 0 ]
	then
		echo_fail "SP800-90C: Cannot load lrng_drbg.ko"
	fi

	create_irqs

	echo > /dev/urandom
	dd if=/dev/urandom of=/dev/null bs=32 count=1

	create_irqs

	# Starting with the second seeding, we revert back to normal seeding
	# strategy by requiring only security strength bits of entropy
	echo > /dev/urandom
	dd if=/dev/urandom of=/dev/null bs=32 count=1

	local cpu=$(dmesg | grep "lrng_es_archrandom: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	local jent=$(dmesg | grep "lrng_es_jent: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')

	local secstrength=$(grep "LRNG security strength in bits:" /proc/lrng_type  | cut -d ":" -f2)
	local seed=0

	if [ $cpu -gt $jent ]
	then
		seed=$cpu
	else
		seed=$jent
	fi

	if [ $seed -eq 256 ]
	then
		echo_pass "SP800-90C: Second seeding after initial seeding pulls only security strength bits of entropy"
	else
		echo_fail "SP800-90C: Second seeding after initial seeding does not pull security strength bits of entropy - pulled: $seed bits"
	fi
}

load_drbg()
{
	local lastseed=$(dmesg | grep "lrng_drng: DRNG fully seeded" | tail -n 1)
	local oldop=$(dmesg | grep "LRNG fully operational")
	if [ -z "$oldop" ]
	then
		oldop=""
	fi

	modprobe lrng_drbg
	if [ $? -ne 0 ]
	then
		echo_fail "SP800-90C: Cannot load lrng_drbg.ko"
	fi

	create_irqs

	# Check that LRNG is re-set to fully operational
	local op=$(dmesg | grep "LRNG fully operational")
	if [ -z "$op" -o "$op" = "$oldop" ]
	then
		echo_fail "SP800-90C: LRNG is not re-initialized for SP800-90C compliance"
	else
		echo_pass "SP800-90C: LRNG is re-initialized for SP800-90C compliance"
	fi

	local newseed=$(dmesg | grep "lrng_drng: DRNG fully seeded" | tail -n 1)

	if [ -z "$lastseed" -o -z "$newseed" ]
	then
		echo_fail "SP800-90C: Fully seeded messages not found"
		return 1
	fi

	if [ "$lastseed" != "$newseed" ]
	then
		return 0
	else
		return 1
	fi
}

check_fully_post_completed()
{
	local i=0
	while [ $i -lt 10 ]
	do
		if (dmesg | grep -q "lrng_health: SP800-90B startup health tests completed")
		then
			return 0
		fi

		create_irqs

		i=$((i+1))
	done

	return 1
}

check_fully_seeded()
{
	if (dmesg | grep -q "lrng_es_mgr: LRNG fully seeded with")
	then

		# It may be possible that the DRNG is seeded so early in the
		# boot cycle that the dyndbg output is not yet created
		# Thus trigger a forced DRNG reseeding with a DRNG that is
		# known to be not fully seeded yet
		modprobe lrng_drbg
		if [ $? -ne 0 ]
		then
			echo_fail "SP800-90C: Cannot load lrng_drbg.ko"
		fi

		local i=0
		while [ $i -lt 10 ]
		do
			if (dmesg | grep -q "lrng_drng: DRNG fully seeded")
			then
				rmmod lrng_drbg
				return 0
			fi

			create_irqs

			i=$((i+1))
		done

		rmmod lrng_drbg
		return 1
	else
		return 1
	fi
}

check_oversampling_seed()
{
	jent_bits=$(dmesg | grep "lrng_es_jent: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	irq_bits=$(dmesg | grep "lrng_es_irq: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	cpu_bits=$(dmesg | grep "lrng_es_archrandom: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	aux_bits=$(dmesg | grep "lrng_es_aux: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')

	sec_strength=$(grep "LRNG security strength in bits" /proc/lrng_type | cut -d":" -f2)

	echo_log "Jitter RNG ES seed bits: $jent_bits"
	echo_log "Interrupt ES seed bits: $irq_bits"
	echo_log "CPU ES seed bits: $cpu_bits"
	echo_log "Auxiliary ES seed bits: $aux_bits"
	echo_log "LRNG security strength: $sec_strength"

	seed_bits=$((jent_bits+$irq_bits+$cpu_bits+$aux_bits))
	seed_req=$(($sec_strength+128))

	if [ $seed_bits -lt $seed_req ]
	then
		return 1
	else
		return 0
	fi
}

check_90c_flag()
{
	if (grep "Standards compliance" /proc/lrng_type | grep -q "SP800-90C")
	then
		return 0
	else
		return 1
	fi
}

check_oversampling_es()
{
	# Only check ES which the LRNG conditions

	irq_obtained_bits=$(dmesg | grep "lrng_es_irq: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	irq_collected_bits=$(dmesg | grep "lrng_es_irq: obtained" | tail -n 1 | sed 's/^.* collecting \([0-9]\+\) bits.*$/\1/')
	aux_obtained_bits=$(dmesg | grep "lrng_es_aux: obtained" | tail -n 1 | sed 's/^.* obtained \([0-9]\+\) bits.*$/\1/')
	aux_collected_bits=$(dmesg | grep "lrng_es_aux: obtained" | tail -n 1 | sed 's/^.* collecting \([0-9]\+\) bits.*$/\1/')

	echo_log "Interrupt ES obtained seed bits: $irq_obtained_bits"
	echo_log "Interrupt ES collected seed bits: $irq_collected_bits"
	echo_log "Auxiliary ES obtained seed bits: $aux_obtained_bits"
	echo_log "Auxiliary ES collected seed bits: $aux_collected_bits"

	if [ $irq_collected_bits -ge 64 ]
	then
		req_bits=$(($irq_obtained_bits+64))
		if [ $req_bits -eq $irq_collected_bits ]
		then
			return 0
		else
			return 1
		fi
	else
		if [ $irq_obtained_bits -eq 0 ]
		then
			return 0
		else
			return 1
		fi
	fi

	if [ $aux_collected_bits -ge 64 ]
	then
		req_bits=$(($aux_obtained_bits+64))
		if [ $req_bits -eq $aux_collected_bits ]
		then
			return 0
		else
			return 1
		fi
	else
		if [ $aux_obtained_bits -eq 0 ]
		then
			return 0
		else
			return 1
		fi
	fi
}

sp80090c_compliance()
{
	#
	# Test purpose: Check that after loading the DRBG, the DRBG is fully
	#		seeded gain.
	#
	$(check_fully_seeded)
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90C: Fully seeding level not reached"
		return
	fi

	#
	# Test purpose: Check that DRBG extension of LRNG can be loaded
	#		successfully
	#
	$(load_drbg)
	if [ $? -eq 0 ]
	then
		echo_pass "SP800-90C: Loading and seeding DRBG successful"
	else
		echo_fail "SP800-90C: Loading and seeding DRBG failed"
	fi

	#
	# Test purpose: Check that SP800-90C flag is show in /proc/lrng_type
	#
	$(check_90c_flag)
	if [ $? -eq 0 ]
	then
		echo_pass "SP800-90C: standards flag present"
	else
		echo_fail "SP800-90C: standards flag not present"
	fi

	#
	# Test purpose: Check that 1st seed after initializing an SP800-90C
	#		compliant LRNG pulls at least security strength bits
	#		plus 128 bits of entropy
	#
	$(check_oversampling_seed)
	if [ $? -eq 0 ]
	then
		echo_pass "SP800-90C: Total seed contains 128 more bits than security strength"
	else
		echo_fail "SP800-90C: Total seed does not contain 128 more bits than security strength"
	fi

	#
	# Test purpose: Check that 2nd seed after initializing an SP800-90C
	#		compliant LRNG pulls exactly security strength bits
	#		of entropy
	#
	check_normal_seed

	#
	# Test purpose: Check that a conditioning operation pulls 64 more bits
	#		of entropy than returned to the caller
	#
	$(check_oversampling_es)
	if [ $? -eq 0 ]
	then
		echo_pass "SP800-90C: Conditioning obtains 64 more bits of entropy"
	else
		echo_fail "SP800-90C: Conditioning does not obtain 64 more bits of entropy"
	fi
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			sp80090c_compliance
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90C: tests skipped"
		exit
	fi

#	$(check_kernel_config "CONFIG_LRNG_AIS2031_NTG1_SEEDING_STRATEGY=y")
#	if [ $? -ne 0 ]
#	then
#		echo_deact "SP800-90C: tests skipped"
#		exit
#	fi

	$(check_kernel_config "CONFIG_CRYPTO_FIPS=y")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90C: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_DRBG=m")
	if [ $? -ne 0 ]
	then
		echo_deact "SP800-90C: tests skipped"
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
