// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG proc interfaces
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
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

#include <linux/lrng.h>
#include <linux/sysctl.h>
#include <linux/uuid.h>

#include "lrng_internal.h"

/* Number of online DRNGs */
static u32 numa_drngs = 1;

void lrng_pool_inc_numa_node(void)
{
	numa_drngs++;
}

/*
 * This function is used to return both the bootid UUID, and random
 * UUID.  The difference is in whether table->data is NULL; if it is,
 * then a new UUID is generated and returned to the user.
 *
 * If the user accesses this via the proc interface, the UUID will be
 * returned as an ASCII string in the standard UUID format; if via the
 * sysctl system call, as 16 bytes of binary data.
 */
static int lrng_proc_do_uuid(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	unsigned char buf[64], tmp_uuid[16], *uuid;

	uuid = table->data;
	if (!uuid) {
		uuid = tmp_uuid;
		generate_random_uuid(uuid);
	} else {
		static DEFINE_SPINLOCK(bootid_spinlock);

		spin_lock(&bootid_spinlock);
		if (!uuid[8])
			generate_random_uuid(uuid);
		spin_unlock(&bootid_spinlock);
	}

	sprintf(buf, "%pU", uuid);

	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);

	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
}

static int lrng_proc_do_type(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct lrng_sdrng *lrng_sdrng_init = lrng_sdrng_init_instance();
	struct ctl_table fake_table;
	unsigned long flags = 0;
	unsigned char buf[300];

	lrng_sdrng_lock(lrng_sdrng_init, &flags);
	snprintf(buf, sizeof(buf),
#ifdef CONFIG_LRNG_TRNG_SUPPORT
		 "TRNG present: true\n"
#else
		 "TRNG present: false\n"
#endif
		 "DRNG name: %s\n"
		 "Hash for reading entropy pool: %s\n"
		 "DRNG security strength: %d bits\n"
		 "number of secondary DRNG instances: %u\n"
		 "SP800-90B compliance: %s\n"
		 "High-resolution timer: %s\n"
		 "LRNG minimally seeded: %s\n"
		 "LRNG fully seeded: %s",
		 lrng_sdrng_init->crypto_cb->lrng_drng_name(),
		 lrng_sdrng_init->crypto_cb->lrng_hash_name(),
		 LRNG_DRNG_SECURITY_STRENGTH_BITS, numa_drngs,
		 lrng_sp80090b_compliant() ? "true" : "false",
		 lrng_pool_highres_timer() ? "true" : "false",
		 lrng_state_min_seeded() ? "true" : "false",
		 lrng_state_fully_seeded() ? "true" : "false");
	lrng_sdrng_unlock(lrng_sdrng_init, &flags);

	fake_table.data = buf;
	fake_table.maxlen = sizeof(buf);

	return proc_dostring(&fake_table, write, buffer, lenp, ppos);
}

static int lrng_proc_do_entropy(struct ctl_table *table, int write,
				void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table fake_table;
	int entropy_count;

	entropy_count = lrng_avail_entropy();

	fake_table.data = &entropy_count;
	fake_table.maxlen = sizeof(entropy_count);

	return proc_dointvec(&fake_table, write, buffer, lenp, ppos);
}

static int lrng_sysctl_poolsize = LRNG_POOL_SIZE_BITS;
static int lrng_min_read_thresh = LRNG_POOL_WORD_BITS;
static int lrng_min_write_thresh;
static int lrng_max_read_thresh = LRNG_POOL_SIZE_BITS;
static int lrng_max_write_thresh = LRNG_POOL_SIZE_BITS;
static char lrng_sysctl_bootid[16];
static int lrng_sdrng_reseed_max_min;

extern struct ctl_table random_table[];
struct ctl_table random_table[] = {
	{
		.procname	= "poolsize",
		.data		= &lrng_sysctl_poolsize,
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "entropy_avail",
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= lrng_proc_do_entropy,
	},
	{
		.procname	= "read_wakeup_threshold",
		.data		= &lrng_read_wakeup_bits,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &lrng_min_read_thresh,
		.extra2		= &lrng_max_read_thresh,
	},
	{
		.procname	= "write_wakeup_threshold",
		.data		= &lrng_write_wakeup_bits,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &lrng_min_write_thresh,
		.extra2		= &lrng_max_write_thresh,
	},
	{
		.procname	= "boot_id",
		.data		= &lrng_sysctl_bootid,
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= lrng_proc_do_uuid,
	},
	{
		.procname	= "uuid",
		.maxlen		= 16,
		.mode		= 0444,
		.proc_handler	= lrng_proc_do_uuid,
	},
	{
		.procname       = "urandom_min_reseed_secs",
		.data           = &lrng_sdrng_reseed_max_time,
		.maxlen         = sizeof(int),
		.mode           = 0644,
		.proc_handler   = proc_dointvec,
		.extra1		= &lrng_sdrng_reseed_max_min,
	},
	{
		.procname	= "lrng_type",
		.maxlen		= 30,
		.mode		= 0444,
		.proc_handler	= lrng_proc_do_type,
	},
	{ }
};
