// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Backend for providing the hash primitive using the leancrypto Linux kernel
 * support.
 *
 * Copyright (C) 2023, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>
#include <linux/init.h>
#include <linux/module.h>

#include <leancrypto.h>

#include "lrng_hash_lc.h"

static u32 lrng_lc_hash_digestsize(void *hash)
{
	return LC_SHA3_512_SIZE_DIGEST;
}

static int lrng_lc_hash_init(struct shash_desc *shash, void *hash)
{
	struct lc_hash_ctx *sha3 = shash_desc_ctx(shash);

	LC_SHA3_512_CTX(sha3);
	lc_hash_init(sha3);
	return 0;
}

static int lrng_lc_hash_update(struct shash_desc *shash,
			       const u8 *inbuf, u32 inbuflen)
{
	struct lc_hash_ctx *sha3 = shash_desc_ctx(shash);

	lc_hash_update(sha3, inbuf, inbuflen);
	return 0;
}

static int lrng_lc_hash_final(struct shash_desc *shash, u8 *digest)
{
	struct lc_hash_ctx *sha3 = shash_desc_ctx(shash);

	lc_hash_final(sha3, digest);
	return 0;
}

static const char *lrng_lc_hash_name(void)
{
	return "Leancrypto SHA3-512";
}

static void lrng_lc_hash_desc_zero(struct shash_desc *shash)
{
	struct lc_hash_ctx *sha3 = shash_desc_ctx(shash);

	lc_hash_zero(sha3);
}

static void *lrng_lc_hash_alloc(void)
{
	/* This only works with larger SHA-3 implementations */
	BUILD_BUG_ON(LC_SHA3_512_CTX_SIZE > HASH_MAX_DESCSIZE);

	pr_info("Hash %s allocated\n", lrng_lc_hash_name());
	return NULL;
}

static void lrng_lc_hash_dealloc(void *hash) { }

const struct lrng_hash_cb lrng_lc_hash_cb = {
	.hash_name		= lrng_lc_hash_name,
	.hash_alloc		= lrng_lc_hash_alloc,
	.hash_dealloc		= lrng_lc_hash_dealloc,
	.hash_digestsize	= lrng_lc_hash_digestsize,
	.hash_init		= lrng_lc_hash_init,
	.hash_update		= lrng_lc_hash_update,
	.hash_final		= lrng_lc_hash_final,
	.hash_desc_zero		= lrng_lc_hash_desc_zero,
};

static int __init lrng_lc_init(void)
{
	return lrng_set_hash_cb(&lrng_lc_hash_cb);
}

static void __exit lrng_lc_exit(void)
{
	lrng_set_hash_cb(NULL);
}

late_initcall(lrng_lc_init);
module_exit(lrng_lc_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Entropy Source and DRNG Manager - Leancrypto hash backend");
