// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Linux Random Number Generator (LRNG) Raw entropy collection tool
 *
 * Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
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
static atomic_t lrng_rb_reader = ATOMIC_INIT(0);
static atomic_t lrng_rb_writer = ATOMIC_INIT(0);
static atomic_t lrng_rb_first_in = ATOMIC_INIT(0);
static atomic_t lrng_testing_enabled = ATOMIC_INIT(0);

static DECLARE_WAIT_QUEUE_HEAD(lrng_raw_read_wait);

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

	write_ptr = (unsigned int)atomic_add_return_relaxed(1, &lrng_rb_writer);
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

	/* We got at least one event, enable the reader now. */
	atomic_set(&lrng_rb_first_in, 1);

	if (wq_has_sleeper(&lrng_raw_read_wait))
		wake_up_interruptible(&lrng_raw_read_wait);

	/*
	 * Our writer is taking over the reader - this means the reader
	 * one full ring buffer available. Thus we "push" the reader ahead
	 * to guarantee that he will be able to consume the full ring.
	 */
	if (!boot_test &&
	    ((write_ptr & LRNG_TESTING_RINGBUFFER_MASK) ==
	    (read_ptr & LRNG_TESTING_RINGBUFFER_MASK)))
		atomic_inc_return_relaxed(&lrng_rb_reader);

	return true;
}

static inline bool lrng_raw_have_data(void)
{
	unsigned int read_ptr = (unsigned int)atomic_read(&lrng_rb_reader);
	unsigned int write_ptr = (unsigned int)atomic_read(&lrng_rb_writer);

	return (atomic_read(&lrng_rb_first_in) &&
		(write_ptr & LRNG_TESTING_RINGBUFFER_MASK) !=
		 (read_ptr & LRNG_TESTING_RINGBUFFER_MASK));
}

static int lrng_raw_entropy_reader(u8 *outbuf, u32 outbuflen)
{
	int collected_data = 0;

	if (!atomic_read(&lrng_testing_enabled) && !boot_test)
		return -EAGAIN;

	if (!atomic_read(&lrng_rb_first_in)) {
		wait_event_interruptible(lrng_raw_read_wait,
					 lrng_raw_have_data());
		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	while (outbuflen) {
		unsigned int read_ptr =
			(unsigned int)atomic_add_return_relaxed(
							1, &lrng_rb_reader);
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
			wait_event_interruptible(lrng_raw_read_wait,
						 lrng_raw_have_data());
			if (signal_pending(current))
				return -ERESTARTSYS;

			continue;
		}

		/* We copy out word-wise */
		if (outbuflen < sizeof(u32)) {
			atomic_dec_return_relaxed(&lrng_rb_reader);
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

/*
 * This data structure holds the dentry's of the debugfs files establishing
 * the interface to user space.
 */
struct lrng_raw_debugfs {
	struct dentry *lrng_raw_debugfs_root; /* root dentry */
	struct dentry *lrng_raw_debugfs_lrng_raw; /* .../lrng_raw */
};

static struct lrng_raw_debugfs lrng_raw_debugfs;

/* DebugFS operations and definition of the debugfs files */
static ssize_t lrng_raw_read(struct file *file, char __user *to,
			     size_t count, loff_t *ppos)
{
	loff_t pos = *ppos;
	int ret;

	if (!count)
		return 0;
	lrng_raw_entropy_init();
	ret = lrng_raw_extract_user(to, count);
	lrng_raw_entropy_fini();
	if (ret < 0)
		return ret;
	count -= ret;
	*ppos = pos + count;
	return ret;
}

/* Module init: allocate memory, register the debugfs files */
static int lrng_raw_debugfs_init(void)
{
	lrng_raw_debugfs.lrng_raw_debugfs_root =
		debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (IS_ERR(lrng_raw_debugfs.lrng_raw_debugfs_root)) {
		lrng_raw_debugfs.lrng_raw_debugfs_root = NULL;
		return PTR_ERR(lrng_raw_debugfs.lrng_raw_debugfs_root);
	}
	return 0;
}

static struct file_operations lrng_raw_name_fops = {
	.owner = THIS_MODULE,
	.read = lrng_raw_read,
};

static int lrng_raw_debugfs_init_name(void)
{
	lrng_raw_debugfs.lrng_raw_debugfs_lrng_raw =
		debugfs_create_file("lrng_raw", 0400,
				    lrng_raw_debugfs.lrng_raw_debugfs_root,
				    NULL, &lrng_raw_name_fops);
	if (IS_ERR(lrng_raw_debugfs.lrng_raw_debugfs_lrng_raw)) {
		lrng_raw_debugfs.lrng_raw_debugfs_lrng_raw = NULL;
		return PTR_ERR(lrng_raw_debugfs.lrng_raw_debugfs_lrng_raw);
	}
	return 0;
}

static int __init lrng_raw_init(void)
{
	int ret = lrng_raw_debugfs_init();

	if (ret < 0)
		return ret;

	ret = lrng_raw_debugfs_init_name();
	if (ret < 0)
		debugfs_remove_recursive(
					lrng_raw_debugfs.lrng_raw_debugfs_root);

	return ret;
}

static void __exit lrng_raw_exit(void)
{
	debugfs_remove_recursive(lrng_raw_debugfs.lrng_raw_debugfs_root);
}

module_init(lrng_raw_init);
module_exit(lrng_raw_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Kernel module for gathering raw entropy");
