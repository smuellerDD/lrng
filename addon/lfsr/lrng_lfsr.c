/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * LRNG Linear Feedback Shift Register
 *
 * Copyright (C) 2020, Stephan Mueller <smueller@chronox.de>
 */

#include <crypto/hash.h>
#include <linux/module.h>

#include "lrng_lfsr.h"

const char *lrng_lfsr_name(void)
{
	static const char lfsr_name[] = "LFSR";
	return lfsr_name;
}
EXPORT_SYMBOL(lrng_lfsr_name);

void *lrng_lfsr_alloc(void)
{
	pr_info("LFSR allocated\n");
	return NULL;
}
EXPORT_SYMBOL(lrng_lfsr_alloc);

u32 lrng_lfsr_digestsize(void *hash)
{
	return lfsr_statesize();
}
EXPORT_SYMBOL(lrng_lfsr_digestsize);

void lrng_lfsr_dealloc(void *hash)
{
	pr_info("LFSR deallocated\n");
}
EXPORT_SYMBOL(lrng_lfsr_dealloc);

int lrng_lfsr_init(struct shash_desc *shash, void *hash)
{
	struct lrng_lfsr_ctx *lfsr = shash_desc_ctx(shash);

	memset(lfsr, 0, sizeof(*lfsr));
	return 0;
}
EXPORT_SYMBOL(lrng_lfsr_init);

int lrng_lfsr_update(struct shash_desc *shash, const u8 *inbuf, u32 inbuflen)
{
	struct lrng_lfsr_ctx *lfsr = shash_desc_ctx(shash);

	while (inbuflen) {
		lrng_lfsr_u8(lfsr, *(inbuf++));
		inbuflen--;
	}

	return 0;
}
EXPORT_SYMBOL(lrng_lfsr_update);

int lrng_lfsr_final(struct shash_desc *shash, u8 *digest)
{
	struct lrng_lfsr_ctx *lfsr = shash_desc_ctx(shash);

	memcpy(digest, lfsr->pool, lfsr_statesize());
	return 0;
}
EXPORT_SYMBOL(lrng_lfsr_final);
