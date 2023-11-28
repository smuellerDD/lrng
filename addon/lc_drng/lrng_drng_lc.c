// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Backend for the LRNG providing the cryptographic primitives using the
 * Leancrypto kernel module.
 *
 * Copyright (C) 2023, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>
#include <linux/init.h>
#include <linux/module.h>

#include <leancrypto.h>

#include "lrng_drng_lc.h"

struct lrng_drng_info {
	struct lc_rng_ctx *lc_ctx;
};

// XDRBG 256 DRNG
#define LC_DRNG_NAME "Leancrypto XDRBG 256"
#define LC_DRNG_ALLOC lc_xdrbg256_drng_alloc

// KMAC 256 DRNG
//#define LC_DRNG_NAME "Leancrypto KMAC 256 DRNG"
//#define LC_DRNG_ALLOC lc_kmac256_drng_alloc

// cSHAKE 256 DRNG
//#define LC_DRNG_NAME "Leancrypto cSHAKE 256 DRNG"
//#define LC_DRNG_ALLOC lc_cshake256_drng_alloc

static int lrng_lc_drng_seed_helper(void *drng, const u8 *inbuf,
				       u32 inbuflen)
{
	struct lrng_drng_info *lrng_drng_info = (struct lrng_drng_info *)drng;
	struct lc_rng_ctx *lc_ctx_local = lrng_drng_info->lc_ctx;
	u32 time = random_get_entropy();
	int ret = lc_rng_seed(lc_ctx_local,
			      inbuf, inbuflen,
			      (u8 *)(&time), sizeof(time));

	if (ret)
		return ret;

	return 0;
}

static int lrng_lc_drng_generate_helper(void *drng, u8 *outbuf,
					   u32 outbuflen)
{
	struct lrng_drng_info *lrng_drng_info = (struct lrng_drng_info *)drng;
	struct lc_rng_ctx *lc_ctx_local = lrng_drng_info->lc_ctx;
	int ret = lc_rng_generate(lc_ctx_local, NULL, 0, outbuf, outbuflen);

	if (ret < 0)
		return ret;

	return outbuflen;
}

static void *lrng_lc_drng_alloc(u32 sec_strength)
{
	struct lrng_drng_info *lrng_drng_info;
	struct lc_rng_ctx *lc_ctx_local;
	u32 time = random_get_entropy();
	int ret;

	lrng_drng_info = kzalloc(sizeof(*lrng_drng_info), GFP_KERNEL);
	if (!lrng_drng_info)
		return ERR_PTR(-ENOMEM);

	ret = LC_DRNG_ALLOC(&lc_ctx_local);
	if (ret) {
		pr_err(LC_DRNG_NAME " cannot be allocated\n");
		goto free;
	}

	lrng_drng_info->lc_ctx = lc_ctx_local;

	ret = lrng_lc_drng_seed_helper(lrng_drng_info, (u8 *)(&time),
					 sizeof(time));
	if (ret)
		goto dealloc;

	pr_info(LC_DRNG_NAME " allocated\n");

	return lrng_drng_info;

dealloc:
	lc_rng_zero_free(lc_ctx_local);
free:
	kfree(lrng_drng_info);
	return ERR_PTR(ret);
}

static void lrng_lc_drng_dealloc(void *drng)
{
	struct lrng_drng_info *lrng_drng_info = (struct lrng_drng_info *)drng;
	struct lc_rng_ctx *lc_ctx_local = lrng_drng_info->lc_ctx;

	lc_rng_zero_free(lc_ctx_local);
	kfree(lrng_drng_info);
	pr_info(LC_DRNG_NAME " deallocated\n");
}

static const char *lrng_lc_drng_name(void)
{
	return LC_DRNG_NAME;
}

const struct lrng_drng_cb lrng_lc_drng_cb = {
	.drng_name	= lrng_lc_drng_name,
	.drng_alloc	= lrng_lc_drng_alloc,
	.drng_dealloc	= lrng_lc_drng_dealloc,
	.drng_seed	= lrng_lc_drng_seed_helper,
	.drng_generate	= lrng_lc_drng_generate_helper,
};

#ifndef CONFIG_LRNG_DFLT_DRNG_LC
static int __init lrng_lc_init(void)
{
	return lrng_set_drng_cb(&lrng_lc_drng_cb);
}
static void __exit lrng_lc_exit(void)
{
	lrng_set_drng_cb(NULL);
}

late_initcall(lrng_lc_init);
module_exit(lrng_lc_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Entropy Source and DRNG Manager - " LC_DRNG_NAME " backend");
#endif /* CONFIG_LRNG_DFLT_DRNG_LC */
