// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Linux Random Number Generator (LRNG) Raw entropy collection tool
 *
 * Copyright (C) 2019 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <asm/errno.h>

#include "lrng_internal.h"

#define LRNG_TESTING_RINGBUFFER_SIZE	1024
#define LRNG_TESTING_RINGBUFFER_MASK	(LRNG_TESTING_RINGBUFFER_SIZE - 1)

static u32 lrng_testing_rb[LRNG_TESTING_RINGBUFFER_SIZE];
static u32 lrng_rb_reader = 0;
static u32 lrng_rb_writer = 0;
static atomic_t lrng_testing_enabled = ATOMIC_INIT(0);

static DECLARE_WAIT_QUEUE_HEAD(lrng_raw_read_wait);
static DEFINE_SPINLOCK(lrng_raw_lock);

/*
 * 0 ==> No boot test, gathering of runtime data allowed
 * 1 ==> Boot test enabled and ready for collecting data, gathering runtime
 *	 data is disabled
 * 2 ==> Boot test completed and disabled, gathering of runtime data is
 *	 disabled
 */
static u32 boot_test = 0;
module_param(boot_test, uint, 0644);
MODULE_PARM_DESC(boot_test, "Enable gathering boot time entropy of the first entropy events");

static inline void lrng_raw_entropy_reset(void)
{
	unsigned long flags;

	spin_lock_irqsave(&lrng_raw_lock, flags);
	lrng_rb_reader = 0;
	lrng_rb_writer = 0;
	spin_unlock_irqrestore(&lrng_raw_lock, flags);
}

static void lrng_raw_entropy_init(void)
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

static void lrng_raw_entropy_fini(void)
{
	if (boot_test)
		return;

	atomic_set(&lrng_testing_enabled, 0);
	lrng_raw_entropy_reset();
	pr_warn("Disabling raw entropy collection\n");
}

bool lrng_raw_entropy_store(u32 value)
{
	unsigned long flags;

	if (!atomic_read(&lrng_testing_enabled) && (boot_test != 1))
		return false;

	spin_lock_irqsave(&lrng_raw_lock, flags);

	/*
	 * Disable entropy testing for boot time testing after ring buffer
	 * is filled.
	 */
	if (boot_test) {
		if (lrng_rb_writer > LRNG_TESTING_RINGBUFFER_SIZE) {
			boot_test = 2;
			pr_warn_once("Boot time entropy collection test disabled\n");
			spin_unlock_irqrestore(&lrng_raw_lock, flags);
			return false;
		}

		if (lrng_rb_writer == 1)
			pr_warn("Boot time entropy collection test enabled\n");
	}

	lrng_testing_rb[lrng_rb_writer & LRNG_TESTING_RINGBUFFER_MASK] = value;
	lrng_rb_writer++;

	spin_unlock_irqrestore(&lrng_raw_lock, flags);

	if (wq_has_sleeper(&lrng_raw_read_wait))
		wake_up_interruptible(&lrng_raw_read_wait);

	return true;
}

static inline bool lrng_raw_have_data(void)
{
	return ((lrng_rb_writer & LRNG_TESTING_RINGBUFFER_MASK) !=
		 (lrng_rb_reader & LRNG_TESTING_RINGBUFFER_MASK));
}

static int lrng_raw_entropy_reader(u8 *outbuf, u32 outbuflen)
{
	unsigned long flags;
	int collected_data = 0;

	lrng_raw_entropy_init();

	while (outbuflen) {
		spin_lock_irqsave(&lrng_raw_lock, flags);

		/* We have no data or reached the writer. */
		if (!lrng_rb_writer || (lrng_rb_writer == lrng_rb_reader)) {

			spin_unlock_irqrestore(&lrng_raw_lock, flags);

			/*
			 * Now we gathered all boot data, enable regular data
			 * collection.
			 */
			if (boot_test) {
				boot_test = 0;
				goto out;
			}

			wait_event_interruptible(lrng_raw_read_wait,
						 lrng_raw_have_data());
			if (signal_pending(current)) {
				collected_data = -ERESTARTSYS;
				goto out;
			}

			continue;
		}

		/* We copy out word-wise */
		if (outbuflen < sizeof(u32)) {
			spin_unlock_irqrestore(&lrng_raw_lock, flags);
			goto out;
		}

		memcpy(outbuf, &lrng_testing_rb[lrng_rb_reader], sizeof(u32));
		lrng_rb_reader++;

		spin_unlock_irqrestore(&lrng_raw_lock, flags);

		outbuf += sizeof(u32);
		outbuflen -= sizeof(u32);
		collected_data += sizeof(u32);
	}

out:
	lrng_raw_entropy_fini();
	return collected_data;
}

/**************************************************************************
 * Debugfs interface
 **************************************************************************/
static int lrng_raw_extract_user(char __user *buf, size_t nbytes)
{
	u8 *tmp, *tmp_aligned;
	int ret = 0, large_request = (nbytes > 256);

	/*
	 * The intention of this interface is for collecting at least
	 * 1000 samples due to the SP800-90B requirements. So, we make no
	 * effort in avoiding allocating more memory that actually needed
	 * by the user. Hence, we allocate sufficient memory to always hold
	 * that amount of data.
	 */
	tmp = kmalloc(LRNG_TESTING_RINGBUFFER_SIZE + sizeof(u32), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	tmp_aligned = PTR_ALIGN(tmp, sizeof(u32));

	while (nbytes) {
		int i;

		if (large_request && need_resched()) {
			if (signal_pending(current)) {
				if (ret == 0)
					ret = -ERESTARTSYS;
				break;
			}
			schedule();
		}

		i = min_t(int, nbytes, LRNG_TESTING_RINGBUFFER_SIZE);
		i = lrng_raw_entropy_reader(tmp_aligned, i);
		if (i <= 0) {
			if (i < 0)
				ret = i;
			break;
		}
		if (copy_to_user(buf, tmp_aligned, i)) {
			ret = -EFAULT;
			break;
		}

		nbytes -= i;
		buf += i;
		ret += i;
	}

	kzfree(tmp);
	return ret;
}

/* DebugFS operations and definition of the debugfs files */
static ssize_t lrng_raw_read(struct file *file, char __user *to,
			     size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	int ret;

	if (!count)
		return 0;

	ret = lrng_raw_extract_user(to, count);
	if (ret < 0)
		return ret;

	count -= ret;
	*ppos = pos + count;

	return ret;
}

static const struct file_operations lrng_raw_name_fops = {
	.owner = THIS_MODULE,
	.read = lrng_raw_read,
};

static int __init lrng_raw_init(void)
{
	struct dentry *lrng_raw_debugfs_root;

	lrng_raw_debugfs_root = debugfs_create_dir(KBUILD_MODNAME, NULL);
	debugfs_create_file_unsafe("lrng_raw", 0400, lrng_raw_debugfs_root,
				   NULL, &lrng_raw_name_fops);

	return 0;
}

module_init(lrng_raw_init);
