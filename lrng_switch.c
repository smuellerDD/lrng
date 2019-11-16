// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG DRNG switching support
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>

#include "lrng_internal.h"

static void lrng_sdrng_switch(struct lrng_sdrng *sdrng_store,
			      const struct lrng_crypto_cb *cb, int node)
{
	const struct lrng_crypto_cb *old_cb;
	unsigned long flags = 0;
	int ret;
	u8 seed[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	void *new_sdrng =
			cb->lrng_drng_alloc(LRNG_DRNG_SECURITY_STRENGTH_BYTES);
	void *old_sdrng, *new_hash = NULL, *old_hash = NULL;
	bool sl = false, reset_sdrng = !lrng_get_available();

	if (IS_ERR(new_sdrng)) {
		pr_warn("could not allocate new secondary DRNG for NUMA node "
			"%d (%ld)\n", node, PTR_ERR(new_sdrng));
		return;
	}

#ifndef CONFIG_LRNG_TRNG_SUPPORT
	new_hash = cb->lrng_hash_alloc(seed, sizeof(seed));
#endif	/* CONFIG_LRNG_TRNG_SUPPORT */
	if (IS_ERR(new_hash)) {
		pr_warn("could not allocate new LRNG pool hash (%ld)\n",
			PTR_ERR(new_hash));
		cb->lrng_drng_dealloc(new_sdrng);
		return;
	}

	lrng_sdrng_lock(sdrng_store, &flags);

	/*
	 * Pull from existing DRNG to seed new DRNG regardless of seed status
	 * of old DRNG -- the entropy state for the secondary DRNG is left
	 * unchanged which implies that als the new DRNG is reseeded when deemed
	 * necessary. This seeding of the new DRNG shall only ensure that the
	 * new DRNG has the same entropy as the old DRNG.
	 */
	ret = sdrng_store->crypto_cb->lrng_drng_generate_helper(
				sdrng_store->sdrng, seed, sizeof(seed));
	lrng_sdrng_unlock(sdrng_store, &flags);

	if (ret < 0) {
		reset_sdrng = true;
		pr_warn("getting random data from secondary DRNG failed for "
			"NUMA node %d (%d)\n", node, ret);
	} else {
		/* seed new DRNG with data */
		ret = cb->lrng_drng_seed_helper(new_sdrng, seed, ret);
		if (ret < 0) {
			reset_sdrng = true;
			pr_warn("seeding of new secondary DRNG failed for NUMA "
				"node %d (%d)\n", node, ret);
		} else {
			pr_debug("seeded new secondary DRNG of NUMA node %d "
				 "instance from old secondary DRNG instance\n",
				 node);
		}
	}

	mutex_lock(&sdrng_store->lock);
	/*
	 * If we switch the secondary DRNG from the initial ChaCha20 DRNG to
	 * something else, there is a lock transition from spin lock to mutex
	 * (see lrng_sdrng_is_atomic and how the lock is taken in
	 * lrng_sdrng_lock). Thus, we need to take both locks during the
	 * transition phase.
	 */
	if (lrng_sdrng_is_atomic(sdrng_store)) {
		spin_lock_irqsave(&sdrng_store->spin_lock, flags);
		sl = true;
	}

	if (reset_sdrng)
		lrng_sdrng_reset(sdrng_store);

	old_sdrng = sdrng_store->sdrng;
	old_cb = sdrng_store->crypto_cb;
	sdrng_store->sdrng = new_sdrng;
	sdrng_store->crypto_cb = cb;

	if (new_hash) {
		old_hash = sdrng_store->hash;
		sdrng_store->hash = new_hash;
		pr_info("Entropy pool read-hash allocated for DRNG for NUMA "
			"node %d\n", node);
	}

	if (sl)
		spin_unlock_irqrestore(&sdrng_store->spin_lock, flags);
	mutex_unlock(&sdrng_store->lock);

	/* Secondary ChaCha20 serves as atomic instance left untouched. */
	if (old_sdrng != &secondary_chacha20) {
		old_cb->lrng_drng_dealloc(old_sdrng);
		if (old_hash)
			old_cb->lrng_hash_dealloc(old_hash);
	}

	pr_info("secondary DRNG of NUMA node %d switched\n", node);
}

/**
 * Switch the existing DRNG instances with new using the new crypto callbacks.
 * The caller must hold the lrng_crypto_cb_update lock.
 */
static int lrng_drngs_switch(const struct lrng_crypto_cb *cb)
{
	struct lrng_sdrng **lrng_sdrng = lrng_sdrng_instances();
	struct lrng_sdrng *lrng_sdrng_init = lrng_sdrng_init_instance();
	int ret = lrng_trng_switch(cb);

	if (ret)
		return ret;

	/* Update secondary DRNG */
	if (lrng_sdrng) {
		u32 node;

		for_each_online_node(node) {
			if (lrng_sdrng[node])
				lrng_sdrng_switch(lrng_sdrng[node], cb, node);
		}
	} else
		lrng_sdrng_switch(lrng_sdrng_init, cb, 0);

	lrng_set_available();

	return 0;
}

/**
 * lrng_set_drng_cb - Register new cryptographic callback functions for DRNG
 * The registering implies that all old DRNG states are replaced with new
 * DRNG states.
 * @cb: Callback functions to be registered -- if NULL, use the default
 *	callbacks pointing to the ChaCha20 DRNG.
 * @return: 0 on success, < 0 on error
 */
int lrng_set_drng_cb(const struct lrng_crypto_cb *cb)
{
	struct lrng_sdrng *lrng_sdrng_init = lrng_sdrng_init_instance();
	int ret;

	if (!cb)
		cb = &lrng_cc20_crypto_cb;

	mutex_lock(&lrng_crypto_cb_update);

	/*
	 * If a callback other than the default is set, allow it only to be
	 * set back to the default callback. This ensures that multiple
	 * different callbacks can be registered at the same time. If a
	 * callback different from the current callback and the default
	 * callback shall be set, the current callback must be deregistered
	 * (e.g. the kernel module providing it must be unloaded) and the new
	 * implementation can be registered.
	 */
	if ((cb != &lrng_cc20_crypto_cb) &&
	    (lrng_sdrng_init->crypto_cb != &lrng_cc20_crypto_cb)) {
		pr_warn("disallow setting new cipher callbacks, unload the old "
			"callbacks first!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = lrng_drngs_switch(cb);

out:
	mutex_unlock(&lrng_crypto_cb_update);
	return ret;
}
EXPORT_SYMBOL(lrng_set_drng_cb);
