// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG True Random Number Generator (TRNG) processing
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lrng.h>

#include "lrng_internal.h"

/* TRNG state handle */
struct lrng_trng {
	void *trng;				/* TRNG handle */
	void *hash;				/* Hash handle */
	u32 trng_entropy_bits;			/* TRNG entropy level */
	const struct lrng_crypto_cb *crypto_cb;	/* Crypto callbacks */
	struct mutex lock;
};

/* TRNG for /dev/random and seed source for the secondary DRNG(s) */
static struct lrng_trng lrng_trng = {
	.trng		= &primary_chacha20,
	.crypto_cb	= &lrng_cc20_crypto_cb,
	.lock		= __MUTEX_INITIALIZER(lrng_trng.lock)
};

/********************************** Helper ************************************/

void lrng_trng_reset(void)
{
	lrng_trng.trng_entropy_bits = 0;
	pr_debug("reset TRNG\n");
}

void lrng_trng_init(void)
{
	mutex_lock(&lrng_trng.lock);
	lrng_trng_reset();
	lrng_cc20_init_state(&primary_chacha20);
	mutex_unlock(&lrng_trng.lock);
}

/************************* Random Number Generation ***************************/

/* Caller must hold lrng_trng.lock */
static int lrng_trng_generate(u8 *outbuf, u32 outbuflen)
{
	struct lrng_trng *trng = &lrng_trng;
	const struct lrng_crypto_cb *crypto_cb = trng->crypto_cb;
	int ret;

	/*
	 * Only deliver as many bytes as the DRNG is seeded with except during
	 * initialization to provide a first seed to the secondary DRNG.
	 */
	if (lrng_state_min_seeded())
		outbuflen = min_t(u32, outbuflen, trng->trng_entropy_bits>>3);
	else
		outbuflen = min_t(u32, outbuflen,
				  LRNG_MIN_SEED_ENTROPY_BITS>>3);
	if (!outbuflen)
		return 0;

	ret = crypto_cb->lrng_drng_generate_helper_full(trng->trng, outbuf,
							outbuflen);
	if (ret != outbuflen) {
		pr_warn("getting random data from TRNG failed (%d)\n",
			ret);
		return ret;
	}

	if (trng->trng_entropy_bits > (u32)(ret<<3))
		trng->trng_entropy_bits -= ret<<3;
	else
		trng->trng_entropy_bits = 0;
	pr_debug("obtained %d bytes of random data from TRNG\n", ret);
	pr_debug("TRNG entropy level at %u bits\n",
		 trng->trng_entropy_bits);

	return ret;
}

/**
 * Inject data into the TRNG with a given entropy value. The function calls
 * the DRNG's update function. This function also generates random data if
 * requested by caller. The caller is only returned the amount of random data
 * that is at most equal to the amount of entropy that just seeded the DRNG.
 *
 * Note, this function seeds the TRNG and generates data in an atomic operation.
 *
 * @inbuf: buffer to inject
 * @inbuflen: length of inbuf
 * @entropy_bits: entropy value of the data in inbuf in bits
 * @outbuf: buffer to fill immediately after seeding to get full entropy
 * @outbuflen: length of outbuf
 * @return: number of bytes written to outbuf, 0 if outbuf is not supplied,
 *	    or < 0 in case of error
 */
static int lrng_trng_inject(const u8 *inbuf, u32 inbuflen, u32 entropy_bits,
			    u8 *outbuf, u32 outbuflen)
{
	struct lrng_trng *trng = &lrng_trng;
	int ret;

	/* cap the maximum entropy value to the provided data length */
	entropy_bits = min_t(u32, entropy_bits, inbuflen<<3);

	mutex_lock(&trng->lock);
	ret = trng->crypto_cb->lrng_drng_seed_helper(trng->trng, inbuf,
						      inbuflen);
	if (ret < 0) {
		pr_warn("(re)seeding of TRNG failed\n");
		goto unlock;
	}
	pr_debug("inject %u bytes with %u bits of entropy into TRNG\n",
		 inbuflen, entropy_bits);

	/* Adjust the fill level indicator to at most the DRNG sec strength */
	trng->trng_entropy_bits =
		min_t(u32, trng->trng_entropy_bits + entropy_bits,
		      LRNG_DRNG_SECURITY_STRENGTH_BITS);
	lrng_init_ops(trng->trng_entropy_bits);

	if (outbuf && outbuflen)
		ret = lrng_trng_generate(outbuf, outbuflen);

unlock:
	mutex_unlock(&trng->lock);
	lrng_reader_wakeup();

	return ret;
}

/**
 * Seed the TRNG from the internal noise sources and generate random data. The
 * seeding and the generation of random data is an atomic operation.
 *
 * lrng_pool_trylock() must be invoked successfully by caller.
 */
int lrng_trng_seed(u8 *outbuf, u32 outbuflen, bool drain)
{
	struct entropy_buf entropy_buf __aligned(LRNG_KCAPI_ALIGN);
	struct lrng_trng *trng = &lrng_trng;
	u32 total_entropy_bits;
	int ret = 0, retrieved = 0;

	/* Get available entropy in primary DRNG */
	if (trng->trng_entropy_bits>>3) {
		mutex_lock(&trng->lock);
		ret = lrng_trng_generate(outbuf, outbuflen);
		mutex_unlock(&trng->lock);
		if (ret > 0) {
			retrieved += ret;
			if (ret == outbuflen)
				goto out;

			outbuf += ret;
			outbuflen -= ret;
		}
		/* Disregard error code as another generate request is below. */
	}

	mutex_lock(&trng->lock);
	total_entropy_bits = lrng_fill_seed_buffer(trng->crypto_cb, trng->hash,
						   &entropy_buf, drain);
	mutex_unlock(&trng->lock);

	pr_debug("reseed TRNG from internal noise sources with %u bits "
		 "of entropy\n", total_entropy_bits);

	ret = lrng_trng_inject((u8 *)&entropy_buf, sizeof(entropy_buf),
				total_entropy_bits,
				outbuf, outbuflen);

	memzero_explicit(&entropy_buf, sizeof(entropy_buf));

	if (ret > 0)
		retrieved += ret;

out:
	/* Allow the seeding operation to be called again */
	lrng_pool_unlock();

	return (ret >= 0) ? retrieved : ret;
}

/**
 * Obtain random data from TRNG with information theoretical entropy by
 * triggering a reseed. The TRNG will only return as many random bytes as it
 * was seeded with.
 *
 * @outbuf: buffer to store the random data in
 * @outbuflen: length of outbuf
 * @return: < 0 on error
 *	    >= 0 the number of bytes that were obtained
 */
int lrng_trng_get(u8 *outbuf, u32 outbuflen)
{
	int ret;

	if (!outbuf || !outbuflen)
		return 0;

	lrng_drngs_init_cc20();

	if (lrng_pool_trylock())
		return -EINPROGRESS;
	ret = lrng_trng_seed(outbuf, outbuflen, true);
	if (ret >= 0) {
		pr_debug("read %d bytes of full entropy data from TRNG\n", ret);
	} else {
		/* This is no error, but we have not generated anything */
		if (ret == -EINPROGRESS)
			return 0;
		pr_debug("reading data from TRNG failed: %d\n", ret);
	}

	return ret;
}

#ifdef CONFIG_LRNG_DRNG_SWITCH
int lrng_trng_switch(const struct lrng_crypto_cb *cb)
{
	int ret;
	u8 seed[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	void *trng, *hash;

	trng = cb->lrng_drng_alloc(LRNG_DRNG_SECURITY_STRENGTH_BYTES);
	if (IS_ERR(trng))
		return PTR_ERR(trng);

	hash = cb->lrng_hash_alloc(seed, sizeof(seed));
	if (IS_ERR(hash)) {
		pr_warn("could not allocate new LRNG pool hash (%ld)\n",
			PTR_ERR(hash));
		cb->lrng_drng_dealloc(trng);
		return PTR_ERR(hash);
	}

	/* Update primary DRNG */
	mutex_lock(&lrng_trng.lock);
	/* pull from existing DRNG to seed new DRNG */
	ret = lrng_trng.crypto_cb->lrng_drng_generate_helper_full(
					lrng_trng.trng, seed, sizeof(seed));
	if (ret < 0) {
		lrng_trng_reset();
		pr_warn("getting random data from TRNG failed (%d)\n", ret);
	} else {
		/*
		 * No change of the seed status as the old and new DRNG have
		 * same security strength.
		 */
		ret = cb->lrng_drng_seed_helper(trng, seed, ret);
		if (ret < 0) {
			lrng_trng_reset();
			pr_warn("seeding of new TRNG failed (%d)\n", ret);
		} else {
			pr_debug("seeded new TRNG instance from old TRNG "
				 "instance\n");
		}
	}
	memzero_explicit(seed, sizeof(seed));

	if (!lrng_get_available())
		lrng_trng_reset();
	lrng_trng.crypto_cb->lrng_drng_dealloc(lrng_trng.trng);
	lrng_trng.trng = trng;

	lrng_trng.crypto_cb->lrng_hash_dealloc(lrng_trng.hash);
	lrng_trng.hash = hash;

	lrng_trng.crypto_cb = cb;

	mutex_unlock(&lrng_trng.lock);

	pr_info("TRNG allocated\n");

	return ret;
}
#endif	/* CONFIG_LRNG_DRNG_SWITCH */
