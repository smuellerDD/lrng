// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Linux Random Number Generator (LRNG) Raw entropy collection tool
 *
 * Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <linux/atomic.h>
#include <linux/lrng.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/types.h>
#include <asm/bug.h>
#include <asm/errno.h>

#define LRNG_TESTING_RINGBUFFER_SIZE	128
#define LRNG_TESTING_RINGBUFFER_MASK	(LRNG_TESTING_RINGBUFFER_SIZE - 1)

static u32 lrng_testing_rb[LRNG_TESTING_RINGBUFFER_SIZE];
static atomic_t lrng_rb_reader = ATOMIC_INIT(0);
static atomic_t lrng_rb_writer = ATOMIC_INIT(0);
static atomic_t lrng_rb_first_in = ATOMIC_INIT(0);
static atomic_t lrng_testing_enabled = ATOMIC_INIT(0);

static u32 boot_test = 0;
module_param(boot_test, uint, 0644);
MODULE_PARM_DESC(boot_test, "Enable gathering boot time entropy of the first"
			    " entropy events");

static inline void lrng_raw_entropy_reset(void)
{
	atomic_set(&lrng_rb_reader, 0);
	atomic_set(&lrng_rb_writer, 0);
	atomic_set(&lrng_rb_first_in, 0);
}

void lrng_raw_entropy_init(void)
{
	/*
	 * The boot time testing implies we have a running test. If the
	 * caller wants to clear it, he has to unset the boot_test flag
	 * at runtime via sysfs to enable regular runtime testing
	 */
	if (boot_test)
		return;

	lrng_raw_entropy_reset();
	atomic_set(&lrng_testing_enabled, 1);
	pr_warn("Enabling raw entropy collection\n");
}

void lrng_raw_entropy_fini(void)
{
	if (boot_test)
		return;

	lrng_raw_entropy_reset();
	atomic_set(&lrng_testing_enabled, 0);
	pr_warn("Disabling raw entropy collection\n");
}

bool lrng_raw_entropy_store(u32 value)
{
	unsigned int write_ptr;
	unsigned int read_ptr;

	if (!atomic_read(&lrng_testing_enabled) && !boot_test)
		return false;

	write_ptr = (unsigned int)atomic_add_return(1, &lrng_rb_writer);
	read_ptr = (unsigned int)atomic_read(&lrng_rb_reader);

	/*
	 * Disable entropy testing for boot time testing after ring buffer
	 * is filled.
	 */
	if (boot_test && write_ptr > LRNG_TESTING_RINGBUFFER_SIZE) {
		pr_warn_once("Boot time entropy collection test disabled\n");
		return false;
	}

	if (boot_test && !atomic_read(&lrng_rb_first_in))
		pr_warn("Boot time entropy collection test enabled\n");

	lrng_testing_rb[write_ptr & LRNG_TESTING_RINGBUFFER_MASK] = value;

	/*
	 * Our writer is taking over the reader - this means the reader
	 * one full ring buffer available. Thus we "push" the reader ahead
	 * to guarantee that he will be able to consume the full ring.
	 */
	if (!boot_test &&
	    ((write_ptr & LRNG_TESTING_RINGBUFFER_MASK) ==
	    (read_ptr & LRNG_TESTING_RINGBUFFER_MASK)))
		atomic_inc(&lrng_rb_reader);

	/* We got at least one event, enable the reader now. */
	atomic_set(&lrng_rb_first_in, 1);

	return true;
}

int lrng_raw_entropy_reader(u8 *outbuf, u32 outbuflen)
{
	int collected_data = 0;

	if (!atomic_read(&lrng_testing_enabled) && !boot_test)
		return -EAGAIN;

	if (!atomic_read(&lrng_rb_first_in))
		return 0;

	while (outbuflen) {
		unsigned int read_ptr =
			(unsigned int)atomic_add_return(1, &lrng_rb_reader);
		unsigned int write_ptr =
			(unsigned int)atomic_read(&lrng_rb_writer);

		/*
		 * For boot time testing, only output one round of ring buffer.
		 */
		if (boot_test && read_ptr > LRNG_TESTING_RINGBUFFER_SIZE) {
			collected_data = -ENOMSG;
			goto out;
		}

		/* We reached the writer */
		if (!boot_test && ((write_ptr & LRNG_TESTING_RINGBUFFER_MASK) ==
		    (read_ptr & LRNG_TESTING_RINGBUFFER_MASK))) {
			atomic_dec(&lrng_rb_reader);
			goto out;
		}

		/* We copy out word-wise */
		if (outbuflen < sizeof(u32)) {
			atomic_dec(&lrng_rb_reader);
			goto out;
		}

		memcpy(outbuf,
		       &lrng_testing_rb[read_ptr & LRNG_TESTING_RINGBUFFER_MASK],
		       sizeof(u32));
		outbuf += sizeof(u32);
		outbuflen -= sizeof(u32);
		collected_data += sizeof(u32);
	}

out:
	return collected_data;
}
