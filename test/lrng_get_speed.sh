#!/bin/bash
#
# Copyright (C) 2018, Stephan Mueller <smueller@chronox.de>
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
# Measure the speed of the deterministic RNGs parts supported by the LRNG
#

SPEED="./speedtest"

remove_module()
{
	module=$1

	rmmod $module > /dev/null 2>&1
}

insert_module()
{
	module=$1
	shift
	params=$@

	modprobe $module $params
}

measure_speed()
{
	name=$@

	for i in 16 32 64 128 256 512 1024 4096
	do
		if [ -x "$SPEED" ]; then
			speed=$($SPEED -b $i | cut -d "|" -f 2)
		else
			speed=$(dd if=/dev/urandom of=/dev/null bs=$i count=1000000 2>&1 | tail -n1 | awk '{print $(NF-1) " " $NF}')
		fi

		echo -e "$name\t$i\t$speed"
	done
}

hmac_drbg_speed()
{
	remove_module lrng_drbg
	insert_module lrng_drbg lrng_drbg_type=1

	if [ -z "$(lsmod | grep lrng_drbg)" ]
	then
		echo "HMAC DRBG test disabled"
		return
	fi

	measure_speed "HMAC DRBG SHA-512"
	remove_module lrng_drbg
}

hash_drbg_speed()
{
	remove_module lrng_drbg
	insert_module lrng_drbg lrng_drbg_type=2

	if [ -z "$(lsmod | grep lrng_drbg)" ]
	then
		echo "Hash DRBG test disabled"
		return
	fi

	measure_speed "Hash DRBG SHA-512"
	remove_module lrng_drbg
}

ctr_drbg_speed()
{
	remove_module lrng_drbg
	insert_module lrng_drbg lrng_drbg_type=0

	if [ -z "$(lsmod | grep lrng_drbg)" ]
	then
		echo "CTR DRBG test disabled"
		return
	fi

	measure_speed "CTR DRBG AES-256"
	remove_module lrng_drbg
}

chacha20_drng_speed()
{
	remove_module lrng_drbg

	measure_speed "ChaCha20 DRNG"
}

if [ $(id -u) -ne 0 ]
then
	echo "Test must be run as root"
	exit 1
fi

CPU=$(cat /proc/cpuinfo  | grep "model name" | tail -n1 | cut -d":" -f2)

if [ -f /proc/sys/kernel/random/lrng_type ]
then
	echo "LRNG Speed test on $CPU"
	echo -e "DRNG name\tBlocksize\tSpeed"

	# Enable/disable as needed
	ctr_drbg_speed
	hash_drbg_speed
	hmac_drbg_speed
	chacha20_drng_speed
else
	echo "Upstream /dev/urandom Speed test on $CPU"
	echo -e "DRNG name\tBlocksize\tSpeed"

	chacha20_drng_speed
fi
