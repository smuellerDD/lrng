// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG NUMA support
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>
#include <linux/slab.h>

#include "lrng_internal.h"

static struct lrng_sdrng **lrng_sdrng __read_mostly = NULL;

struct lrng_sdrng **lrng_sdrng_instances(void)
{
	return lrng_sdrng;
}

/* Allocate the data structures for the per-NUMA node DRNGs */
static void _lrng_drngs_numa_alloc(struct work_struct *work)
{
	struct lrng_sdrng **sdrngs;
	struct lrng_sdrng *lrng_sdrng_init = lrng_sdrng_init_instance();
	u32 node;
	bool init_sdrng_used = false;

	mutex_lock(&lrng_crypto_cb_update);

	/* per-NUMA-node DRNGs are already present */
	if (lrng_sdrng)
		goto unlock;

	sdrngs = kcalloc(nr_node_ids, sizeof(void *), GFP_KERNEL|__GFP_NOFAIL);
	for_each_online_node(node) {
		struct lrng_sdrng *sdrng;

		if (!init_sdrng_used) {
			sdrngs[node] = lrng_sdrng_init;
			init_sdrng_used = true;
			continue;
		}

		sdrng = kmalloc_node(sizeof(struct lrng_sdrng),
				     GFP_KERNEL|__GFP_NOFAIL, node);
		memset(sdrng, 0, sizeof(lrng_sdrng));

		sdrng->crypto_cb = lrng_sdrng_init->crypto_cb;
		sdrng->sdrng = sdrng->crypto_cb->lrng_drng_alloc(
					LRNG_DRNG_SECURITY_STRENGTH_BYTES);
		if (IS_ERR(sdrng->sdrng)) {
			kfree(sdrng);
			goto err;
		}

		mutex_init(&sdrng->lock);
		spin_lock_init(&sdrng->spin_lock);

		/*
		 * No reseeding of NUMA DRNGs from previous DRNGs as this
		 * would complicate the code. Let it simply reseed.
		 */
		lrng_sdrng_reset(sdrng);
		sdrngs[node] = sdrng;

		lrng_pool_inc_numa_node();
		pr_info("secondary DRNG for NUMA node %d allocated\n", node);
	}

	/* Ensure that all NUMA nodes receive changed memory here. */
	mb();

	if (!cmpxchg(&lrng_sdrng, NULL, sdrngs))
		goto unlock;

err:
	for_each_online_node(node) {
		struct lrng_sdrng *sdrng = sdrngs[node];

		if (sdrng == lrng_sdrng_init)
			continue;

		if (sdrng) {
			sdrng->crypto_cb->lrng_drng_dealloc(sdrng->sdrng);
			kfree(sdrng);
		}
	}
	kfree(sdrngs);

unlock:
	mutex_unlock(&lrng_crypto_cb_update);
}

static DECLARE_WORK(lrng_drngs_numa_alloc_work, _lrng_drngs_numa_alloc);

void lrng_drngs_numa_alloc(void)
{
	schedule_work(&lrng_drngs_numa_alloc_work);
}
