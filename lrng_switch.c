// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG DRNG switching support
 *
 * Copyright (C) 2016 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>

#include "lrng_internal.h"

static int lrng_drng_switch(struct lrng_drng *drng_store,
			    const struct lrng_crypto_cb *cb, int node)
{
	const struct lrng_crypto_cb *old_cb;
	unsigned long flags = 0;
	int ret;
	u8 seed[LRNG_DRNG_SECURITY_STRENGTH_BYTES] __latent_entropy;
	void *new_drng = cb->lrng_drng_alloc(LRNG_DRNG_SECURITY_STRENGTH_BYTES);
	void *old_drng, *new_hash, *old_hash;
	bool sl = false, reset_drng = !lrng_get_available();

	if (IS_ERR(new_drng)) {
		pr_warn("could not allocate new DRNG for NUMA node %d (%ld)\n",
			node, PTR_ERR(new_drng));
		return PTR_ERR(new_drng);
	}

	/*
	 * The seed potentially used as MAC key is undefined to add some
	 * variation. Yet, the security of the MAC does not rely on the key
	 * being secret. The key is only there to turn a MAC into a hash.
	 * The intention is to allow the specification of CMAC(AES) as "hash"
	 * to limit the dependency to AES when using the CTR DRBG.
	 */
	new_hash = cb->lrng_hash_alloc(seed, sizeof(seed));
	if (IS_ERR(new_hash)) {
		pr_warn("could not allocate new LRNG pool hash (%ld)\n",
			PTR_ERR(new_hash));
		cb->lrng_drng_dealloc(new_drng);
		return PTR_ERR(new_hash);
	}

	lrng_drng_lock(drng_store, &flags);

	/*
	 * Pull from existing DRNG to seed new DRNG regardless of seed status
	 * of old DRNG -- the entropy state for the DRNG is left unchanged which
	 * implies that als the new DRNG is reseeded when deemed necessary. This
	 * seeding of the new DRNG shall only ensure that the new DRNG has the
	 * same entropy as the old DRNG.
	 */
	ret = drng_store->crypto_cb->lrng_drng_generate_helper(
				drng_store->drng, seed, sizeof(seed));
	lrng_drng_unlock(drng_store, &flags);

	if (ret < 0) {
		reset_drng = true;
		pr_warn("getting random data from DRNG failed for NUMA node %d (%d)\n",
			node, ret);
	} else {
		/* seed new DRNG with data */
		ret = cb->lrng_drng_seed_helper(new_drng, seed, ret);
		if (ret < 0) {
			reset_drng = true;
			pr_warn("seeding of new DRNG failed for NUMA node %d (%d)\n",
				node, ret);
		} else {
			pr_debug("seeded new DRNG of NUMA node %d instance from old DRNG instance\n",
				 node);
		}
	}

	mutex_lock(&drng_store->lock);
	/*
	 * If we switch the DRNG from the initial ChaCha20 DRNG to something
	 * else, there is a lock transition from spin lock to mutex (see
	 * lrng_drng_is_atomic and how the lock is taken in lrng_drng_lock).
	 * Thus, we need to take both locks during the transition phase.
	 */
	if (lrng_drng_is_atomic(drng_store)) {
		spin_lock_irqsave(&drng_store->spin_lock, flags);
		sl = true;
	} else {
		__acquire(&drng_store->spin_lock);
	}

	if (reset_drng)
		lrng_drng_reset(drng_store);

	old_drng = drng_store->drng;
	old_cb = drng_store->crypto_cb;
	drng_store->drng = new_drng;
	drng_store->crypto_cb = cb;

	old_hash = drng_store->hash;
	drng_store->hash = new_hash;
	pr_info("Entropy pool read-hash allocated for DRNG for NUMA node %d\n",
		node);

	if (sl)
		spin_unlock_irqrestore(&drng_store->spin_lock, flags);
	else
		__release(&drng_store->spin_lock);
	mutex_unlock(&drng_store->lock);

	/* ChaCha20 serves as atomic instance left untouched. */
	if (old_drng != &chacha20) {
		old_cb->lrng_drng_dealloc(old_drng);
		old_cb->lrng_hash_dealloc(old_hash);
	}

	pr_info("DRNG of NUMA node %d switched\n", node);

	return 0;
}

/*
 * Switch the existing DRNG instances with new using the new crypto callbacks.
 * The caller must hold the lrng_crypto_cb_update lock.
 */
static int lrng_drngs_switch(const struct lrng_crypto_cb *cb)
{
	struct lrng_drng **lrng_drng = lrng_drng_instances();
	struct lrng_drng *lrng_drng_init = lrng_drng_init_instance();
	int ret = 0;

	/* Update DRNG */
	if (lrng_drng) {
		u32 node;

		for_each_online_node(node) {
			if (lrng_drng[node])
				ret = lrng_drng_switch(lrng_drng[node], cb,
						       node);
		}
	} else {
		ret = lrng_drng_switch(lrng_drng_init, cb, 0);
	}

	if (!ret)
		lrng_set_available();

	return 0;
}

/**
 * lrng_set_drng_cb - Register new cryptographic callback functions for DRNG
 * The registering implies that all old DRNG states are replaced with new
 * DRNG states.
 *
 * @cb: Callback functions to be registered -- if NULL, use the default
 *	callbacks pointing to the ChaCha20 DRNG.
 *
 * Return:
 * * 0 on success
 * * < 0 on error
 */
int lrng_set_drng_cb(const struct lrng_crypto_cb *cb)
{
	struct lrng_drng *lrng_drng_init = lrng_drng_init_instance();
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
	    (lrng_drng_init->crypto_cb != &lrng_cc20_crypto_cb)) {
		pr_warn("disallow setting new cipher callbacks, unload the old callbacks first!\n");
		ret = -EINVAL;
		goto out;
	}

	ret = lrng_drngs_switch(cb);

out:
	mutex_unlock(&lrng_crypto_cb_update);
	return ret;
}
EXPORT_SYMBOL(lrng_set_drng_cb);
