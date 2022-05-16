#!/bin/bash
#
# Test for DRNG operation
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

verify_hash()
{
	local expected_hash=$1

	local hash_ent_name=$(cat /proc/lrng_type | grep -A1 "Name: Auxiliary" | grep "Hash for operating entropy" | cut -d ":" -f2)
	local hash_aux_name=$(cat /proc/lrng_type | grep -A1 "Name: IRQ" | grep "Hash for operating entropy" | cut -d ":" -f2)

	hash_ent_name=$(echo $hash_ent_name)
	hash_aux_name=$(echo $hash_aux_name)

	echo_log "Hash for entropy pool loaded: $hash_ent_name"
	echo_log "Hash for aux pool loaded: $hash_aux_name"

	if [ x"$hash_ent_name" != x"$expected_hash" ]
	then
		echo_fail "DRNG: Unexpected Hash type - expected: $expected_hash, found: $hash_ent_name"
	else
		echo_pass "DRNG: Hash type $hash_ent_name as expected"
	fi

	if [ x"$hash_aux_name" != x"$expected_hash" ]
	then
		echo_fail "DRNG: Unexpected Hash type - expected: $expected_hash, found: $hash_aux_name"
	else
		echo_pass "DRNG: Hash type $hash_aux_name as expected"
	fi
}
verify_drng()
{
	local expected_drng=$1

	local drng_name=$(cat /proc/lrng_type  | grep "DRNG name" | cut -d ":" -f2)

	drng_name=$(echo $drng_name)

	echo_log "DRNG loaded: $name"

	if [ x"$drng_name" != x"$expected_drng" ]
	then
		echo_fail "DRNG: Unexpected DRNG type - expected: $expected_drng, found: $drng_name"
	else
		echo_pass "DRNG: DRNG type $drng_name as expected"
	fi
}

load_drbg() {
	local type=$1
	local expected_drng=$2

	modprobe lrng_drng_drbg lrng_drbg_type=$type
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: cannot load kernel module lrng_drng_kcapi with parameter drng_name=$expected_drng"
		return
	fi

	verify_drng $expected_drng
	rmmod lrng_drng_drbg
}

load_drng_kcapi()
{
	local name=$1
	local expected_drng=$2

	modprobe lrng_drng_kcapi drng_name="$name" seed_hash="sha384"
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: cannot load kernel module lrng_drng_kcapi with parameter drng_name=\"$name\" seed_hash=\"sha384\""
		return
	fi

	verify_drng $expected_drng
	rmmod lrng_drng_kcapi
}

load_hash_kcapi()
{
	local name=$1
	local expected_hash=$1

	modprobe lrng_hash_kcapi lrng_hash_name="$name"
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: cannot load kernel module lrng_hash_kcapi with parameter lrng_hash_name=\"$name\""
		return
	fi

	verify_hash $expected_hash
	rmmod lrng_hash_kcapi
}

check_chacha20()
{
	verify_drng "ChaCha20 DRNG"
}

check_default_hash()
{
	local hash="SHA-256"
	$(check_kernel_config "CONFIG_CRYPTO_LIB_SHA256=y")
	if [ $? -ne 0 ]
	then
		hash="SHA-1"
	fi

	verify_hash "$hash"
}

check_drbgs()
{
	$(check_kernel_config "CONFIG_LRNG_DRBG=m")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: testing DRBG skipped"
		return
	fi

	load_drbg 0 "drbg_nopr_ctr_aes256"
	check_chacha20
	load_drbg 1 "drbg_nopr_hmac_sha512"
	check_chacha20
	load_drbg 2 "drbg_nopr_sha512"
	check_chacha20
}

check_kcapi_drng()
{
	$(check_kernel_config "CONFIG_LRNG_DRNG_KCAPI=m")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: testing KCAPI DRNG skipped"
		return
	fi

	modprobe ansi_cprng
	local ansi_cprng="ansi_cprng"
	if (cat /proc/sys/crypto/fips_enabled | grep -q 1)
	then
		ansi_cprng="fips_ansi_cprng"
	fi

	load_drng_kcapi "$ansi_cprng" "$ansi_cprng"
	check_chacha20
}

check_kcapi_hash()
{
	$(check_kernel_config "CONFIG_LRNG_HASH_KCAPI=m")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: testing KCAPI hash skipped"
		return
	fi

	load_hash_kcapi "sha512"
	check_default_hash
}

exec_test()
{
	check_chacha20
	check_drbgs
	check_kcapi_drng
	check_kcapi_hash
}

$(in_hypervisor)
if [ $? -eq 1 ]
then
	case $(read_cmd) in
		"test1")
			exec_test
			;;
		*)
			echo_fail "Test $1 not found"
			;;
	esac
else
	$(check_kernel_config "CONFIG_LRNG_CPU=y")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_JENT=y")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_LRNG_RUNTIME_ES_CONFIG=y")
	if [ $? -ne 0 ]
	then
		echo_deact "DRNG: tests skipped"
		exit
	fi

	$(check_kernel_config "CONFIG_CRYPTO_FIPS=y")
	if [ $? -ne 0 ]
	then
		echo_log "DRNG: FIPS mode not present - not all code paths tested"
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

	$(check_kernel_config "CONFIG_LRNG_AIS2031_NTG1_SEEDING_STRATEGY=y")
	if [ $? -ne 0 ]
	then
		echo_log "DRNG: NTG mode not present - not all code paths tested"
		exit
	fi

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
