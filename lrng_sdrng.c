// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG secondary DRNG processing
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>

#include "lrng_internal.h"

/*
 * Maximum number of seconds between DRNG reseed intervals of the secondary
 * DRNG. Note, this is enforced with the next request of random numbers from
 * the secondary DRNG. Setting this value to zero implies a reseeding attempt
 * before every generated random number.
 */
int lrng_sdrng_reseed_max_time = 600;

static atomic_t lrng_avail = ATOMIC_INIT(0);

DEFINE_MUTEX(lrng_crypto_cb_update);

/* Secondary DRNG for /dev/urandom, getrandom(2), get_random_bytes */
static struct lrng_sdrng lrng_sdrng_init = {
	.sdrng		= &secondary_chacha20,
	.crypto_cb	= &lrng_cc20_crypto_cb,
	.lock		= __MUTEX_INITIALIZER(lrng_sdrng_init.lock),
	.spin_lock	= __SPIN_LOCK_UNLOCKED(lrng_sdrng_init.spin_lock)
};

/*
 * Secondary DRNG for get_random_bytes when called in atomic context. This
 * DRNG will always use the ChaCha20 DRNG. It will never benefit from a
 * DRNG switch like the "regular" secondary DRNG. If there was no DRNG
 * switch, the atomic secondary DRNG is identical to the "regular" secondary
 * DRNG.
 *
 * The reason for having this is due to the fact that DRNGs other than
 * the ChaCha20 DRNG may sleep.
 */
static struct lrng_sdrng lrng_sdrng_atomic = {
	.sdrng		= &secondary_chacha20,
	.crypto_cb	= &lrng_cc20_crypto_cb,
	.spin_lock	= __SPIN_LOCK_UNLOCKED(lrng_sdrng_atomic.spin_lock)
};

/********************************** Helper ************************************/

bool lrng_get_available(void)
{
	return likely(atomic_read(&lrng_avail));
}

void lrng_set_available(void)
{
	atomic_set(&lrng_avail, 1);
}

struct lrng_sdrng *lrng_sdrng_init_instance(void)
{
	return &lrng_sdrng_init;
}

struct lrng_sdrng *lrng_sdrng_atomic_instance(void)
{
	return &lrng_sdrng_atomic;
}

void lrng_sdrng_reset(struct lrng_sdrng *sdrng)
{
	atomic_set(&sdrng->requests, LRNG_DRNG_RESEED_THRESH);
	sdrng->last_seeded = jiffies;
	sdrng->fully_seeded = false;
	sdrng->force_reseed = true;
	pr_debug("reset secondary DRNG\n");
}

/************************* Random Number Generation ***************************/

/* Inject a data buffer into the secondary DRNG */
static void lrng_sdrng_inject(struct lrng_sdrng *sdrng,
			      const u8 *inbuf, u32 inbuflen)
{
	const char *drng_type = unlikely(sdrng == &lrng_sdrng_atomic) ?
				"atomic" : "secondary";
	unsigned long flags = 0;

	BUILD_BUG_ON(LRNG_DRNG_RESEED_THRESH > INT_MAX);
	pr_debug("seeding %s DRNG with %u bytes\n", drng_type, inbuflen);
	lrng_sdrng_lock(sdrng, &flags);
	if (sdrng->crypto_cb->lrng_drng_seed_helper(sdrng->sdrng,
						    inbuf, inbuflen) < 0) {
		pr_warn("seeding of %s DRNG failed\n", drng_type);
		atomic_set(&sdrng->requests, 1);
	} else {
		pr_debug("%s DRNG stats since last seeding: %lu secs; "
			 "generate calls: %d\n", drng_type,
			 (time_after(jiffies, sdrng->last_seeded) ?
			  (jiffies - sdrng->last_seeded) : 0) / HZ,
			 (LRNG_DRNG_RESEED_THRESH -
			  atomic_read(&sdrng->requests)));
		sdrng->last_seeded = jiffies;
		atomic_set(&sdrng->requests, LRNG_DRNG_RESEED_THRESH);
		sdrng->force_reseed = false;

		if (sdrng->sdrng == lrng_sdrng_atomic.sdrng) {
			lrng_sdrng_atomic.last_seeded = jiffies;
			atomic_set(&lrng_sdrng_atomic.requests,
				   LRNG_DRNG_RESEED_THRESH);
			lrng_sdrng_atomic.force_reseed = false;
		}
	}
	lrng_sdrng_unlock(sdrng, &flags);
}

#ifdef CONFIG_LRNG_TRNG_SUPPORT
static inline int _lrng_sdrng_seed(struct lrng_sdrng *sdrng)
{
	u8 seedbuf[LRNG_DRNG_SECURITY_STRENGTH_BYTES]
						__aligned(LRNG_KCAPI_ALIGN);
	int ret = lrng_trng_seed(seedbuf, sizeof(seedbuf),
				 sdrng->fully_seeded ? LRNG_EMERG_ENTROPY : 0);

	/* Update the DRNG state even though we received zero random data */
	if (ret < 0) {
		/* Try to reseed at next round */
		atomic_set(&sdrng->requests, 1);
		return ret;
	}

	lrng_sdrng_inject(sdrng, seedbuf, sizeof(seedbuf));
	memzero_explicit(seedbuf, sizeof(seedbuf));

	return ret;
}
#else	/* CONFIG_LRNG_TRNG_SUPPORT */
static inline int _lrng_sdrng_seed(struct lrng_sdrng *sdrng)
{
	struct entropy_buf seedbuf __aligned(LRNG_KCAPI_ALIGN);
	unsigned long flags = 0;
	u32 total_entropy_bits;
	int ret;

	lrng_sdrng_lock(sdrng, &flags);
	total_entropy_bits = lrng_fill_seed_buffer(sdrng->crypto_cb,
						   sdrng->hash, &seedbuf, 0);
	lrng_sdrng_unlock(sdrng, &flags);

	/* Allow the seeding operation to be called again */
	lrng_pool_unlock();
	lrng_init_ops(total_entropy_bits);
	ret = total_entropy_bits >> 3;

	lrng_sdrng_inject(sdrng, (u8 *)&seedbuf, sizeof(seedbuf));
	memzero_explicit(&seedbuf, sizeof(seedbuf));

	return ret;
}
#endif	/* CONFIG_LRNG_TRNG_SUPPORT */

static int lrng_sdrng_get(struct lrng_sdrng *sdrng, u8 *outbuf, u32 outbuflen);
static void lrng_sdrng_seed(struct lrng_sdrng *sdrng)
{
	int ret = _lrng_sdrng_seed(sdrng);

	if (ret >= LRNG_DRNG_SECURITY_STRENGTH_BYTES)
		sdrng->fully_seeded = true;

	BUILD_BUG_ON(LRNG_MIN_SEED_ENTROPY_BITS >
		     LRNG_DRNG_SECURITY_STRENGTH_BITS);

	/*
	 * Reseed atomic DRNG from current secondary DRNG,
	 *
	 * We can obtain random numbers from secondary DRNG as the lock type
	 * chosen by lrng_sdrng_get is usable with the current caller.
	 */
	if ((sdrng->sdrng != lrng_sdrng_atomic.sdrng) &&
	    (lrng_sdrng_atomic.force_reseed ||
	     atomic_read(&lrng_sdrng_atomic.requests) <= 0 ||
	     time_after(jiffies, lrng_sdrng_atomic.last_seeded +
			lrng_sdrng_reseed_max_time * HZ))) {
		u8 seedbuf[LRNG_DRNG_SECURITY_STRENGTH_BYTES]
						__aligned(LRNG_KCAPI_ALIGN);

		ret = lrng_sdrng_get(sdrng, seedbuf, sizeof(seedbuf));

		if (ret < 0) {
			pr_warn("Error generating random numbers for atomic "
				"DRNG: %d\n", ret);
		} else {
			lrng_sdrng_inject(&lrng_sdrng_atomic, seedbuf, ret);
		}
		memzero_explicit(&seedbuf, sizeof(seedbuf));
	}
}

static inline void _lrng_sdrng_seed_work(struct lrng_sdrng *sdrng, u32 node)
{
	pr_debug("reseed triggered by interrupt noise source "
		 "for secondary DRNG on NUMA node %d\n", node);
	lrng_sdrng_seed(sdrng);
	if (sdrng->fully_seeded) {
		/* Prevent reseed storm */
		sdrng->last_seeded += node * 100 * HZ;
		/* Prevent draining of pool on idle systems */
		lrng_sdrng_reseed_max_time += 100;
	}
}

/**
 * DRNG reseed trigger: Kernel thread handler triggered by the schedule_work()
 */
void lrng_sdrng_seed_work(struct work_struct *dummy)
{
	struct lrng_sdrng **lrng_sdrng = lrng_sdrng_instances();
	u32 node;

	if (lrng_sdrng) {
		for_each_online_node(node) {
			struct lrng_sdrng *sdrng = lrng_sdrng[node];

			if (sdrng && !sdrng->fully_seeded) {
				_lrng_sdrng_seed_work(sdrng, node);
				goto out;
			}
		}
		lrng_pool_all_numa_nodes_seeded();
	} else {
		if (!lrng_sdrng_init.fully_seeded)
			_lrng_sdrng_seed_work(&lrng_sdrng_init, 0);
	}

out:
	/* Allow the seeding operation to be called again */
	lrng_pool_unlock();
}

/* Force all secondary DRNGs to reseed before next generation */
void lrng_sdrng_force_reseed(void)
{
	struct lrng_sdrng **lrng_sdrng = lrng_sdrng_instances();
	u32 node;

	if (!lrng_sdrng) {
		lrng_sdrng_init.force_reseed = true;
		pr_debug("force reseed of initial secondary DRNG\n");
		return;
	}
	for_each_online_node(node) {
		struct lrng_sdrng *sdrng = lrng_sdrng[node];

		if (!sdrng)
			continue;

		sdrng->force_reseed = true;
		pr_debug("force reseed of secondary DRNG on node %u\n", node);
	}
	lrng_sdrng_atomic.force_reseed = true;
}

/**
 * Get random data out of the secondary DRNG which is reseeded frequently.
 *
 * @outbuf: buffer for storing random data
 * @outbuflen: length of outbuf
 * @return: < 0 in error case (DRNG generation or update failed)
 *	    >=0 returning the returned number of bytes
 */
static int lrng_sdrng_get(struct lrng_sdrng *sdrng, u8 *outbuf, u32 outbuflen)
{
	unsigned long flags = 0;
	u32 processed = 0;

	if (!outbuf || !outbuflen)
		return 0;

	outbuflen = min_t(size_t, outbuflen, INT_MAX);

	lrng_drngs_init_cc20();

	while (outbuflen) {
		u32 todo = min_t(u32, outbuflen, LRNG_DRNG_MAX_REQSIZE);
		int ret;

		/* All but the atomic DRNG are seeded during generation */
		if (atomic_dec_and_test(&sdrng->requests) ||
		    sdrng->force_reseed ||
		    time_after(jiffies, sdrng->last_seeded +
			       lrng_sdrng_reseed_max_time * HZ)) {
			if (likely(sdrng != &lrng_sdrng_atomic)) {
				if (lrng_pool_trylock())
					atomic_set(&sdrng->requests, 1);
				else
					lrng_sdrng_seed(sdrng);
			}
		}

		lrng_sdrng_lock(sdrng, &flags);
		ret = sdrng->crypto_cb->lrng_drng_generate_helper(
					sdrng->sdrng, outbuf + processed, todo);
		lrng_sdrng_unlock(sdrng, &flags);
		if (ret <= 0) {
			pr_warn("getting random data from secondary DRNG "
				"failed (%d)\n", ret);
			return -EFAULT;
		}
		processed += ret;
		outbuflen -= ret;
	}

	return processed;
}

int lrng_sdrng_get_atomic(u8 *outbuf, u32 outbuflen)
{
	return lrng_sdrng_get(&lrng_sdrng_atomic, outbuf, outbuflen);
}

int lrng_sdrng_get_sleep(u8 *outbuf, u32 outbuflen)
{
	struct lrng_sdrng **lrng_sdrng = lrng_sdrng_instances();
	struct lrng_sdrng *sdrng = &lrng_sdrng_init;
	int node = numa_node_id();

	might_sleep();

	if (lrng_sdrng && lrng_sdrng[node] && lrng_sdrng[node]->fully_seeded)
		sdrng = lrng_sdrng[node];

	return lrng_sdrng_get(sdrng, outbuf, outbuflen);
}

/* Initialize the default DRNG during boot */
void lrng_drngs_init_cc20(void)
{
	unsigned long flags = 0;

	if (lrng_get_available())
		return;

	lrng_sdrng_lock(&lrng_sdrng_init, &flags);
	if (lrng_get_available()) {
		lrng_sdrng_unlock(&lrng_sdrng_init, &flags);
		return;
	}

	lrng_sdrng_reset(&lrng_sdrng_init);
	lrng_cc20_init_state(&secondary_chacha20);
	lrng_state_init_seed_work();
	lrng_sdrng_unlock(&lrng_sdrng_init, &flags);

	lrng_sdrng_lock(&lrng_sdrng_atomic, &flags);
	lrng_sdrng_reset(&lrng_sdrng_atomic);
	/*
	 * We do not initialize the state of the atomic DRNG as it is identical
	 * to the secondary DRNG at this point.
	 */
	lrng_sdrng_unlock(&lrng_sdrng_atomic, &flags);

	lrng_trng_init();

	lrng_set_available();
}

/* Reset LRNG such that all existing entropy is gone */
static void _lrng_reset(struct work_struct *work)
{
	struct lrng_sdrng **lrng_sdrng = lrng_sdrng_instances();
	unsigned long flags = 0;

	lrng_reset_state();
	lrng_trng_reset();

	if (!lrng_sdrng) {
		lrng_sdrng_lock(&lrng_sdrng_init, &flags);
		lrng_sdrng_reset(&lrng_sdrng_init);
		lrng_sdrng_unlock(&lrng_sdrng_init, &flags);
	} else {
		u32 node;

		for_each_online_node(node) {
			struct lrng_sdrng *sdrng = lrng_sdrng[node];

			if (!sdrng)
				continue;
			lrng_sdrng_lock(sdrng, &flags);
			lrng_sdrng_reset(sdrng);
			lrng_sdrng_unlock(sdrng, &flags);
		}
	}
	lrng_set_entropy_thresh(LRNG_INIT_ENTROPY_BITS +
				LRNG_CONDITIONING_ENTROPY_LOSS);
}

static DECLARE_WORK(lrng_reset_work, _lrng_reset);

void lrng_reset(void)
{
	schedule_work(&lrng_reset_work);
}

/***************************** Initialize LRNG *******************************/

static int __init lrng_init(void)
{
	lrng_drngs_init_cc20();

	lrng_drngs_numa_alloc();
	return 0;
}

late_initcall(lrng_init);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Linux Random Number Generator");
