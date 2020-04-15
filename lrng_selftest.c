// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG power-on and on-demand self-test
 *
 * Copyright (C) 2016 - 2020, Stephan Mueller <smueller@chronox.de>
 */

/*
 * In addition to the self-tests below, the following LRNG components
 * are covered with self-tests during regular operation:
 *
 * * power-on self-test: SP800-90A DRBG provided by the Linux kernel crypto API
 * * power-on self-test: PRNG provided by the Linux kernel crypto API
 * * runtime test: Raw noise source data testing including SP800-90B compliant
 *		   tests when enabling CONFIG_LRNG_HEALTH_TESTS
 *
 * Additional developer tests present with LRNG code:
 * * SP800-90B APT and RCT test enforcement validation when enabling
 *   CONFIG_LRNG_APT_BROKEN or CONFIG_LRNG_RCT_BROKEN.
 * * Collection of raw entropy from the interrupt noise source when enabling
 *   CONFIG_LRNG_TESTING and pulling the data from the kernel with the provided
 *   interface.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/lrng.h>
#include <linux/slab.h>

#include "lrng_chacha20.h"
#include "lrng_internal.h"
#include "lrng_lfsr.h"
#include "lrng_sw_noise.h"

#define LRNG_SELFTEST_PASSED		0
#define LRNG_SEFLTEST_ERROR_TIME	(1 << 0)
#define LRNG_SEFLTEST_ERROR_LFSR	(1 << 1)
#define LRNG_SEFLTEST_ERROR_CHACHA20	(1 << 2)
#define LRNG_SEFLTEST_ERROR_HASHDF	(1 << 3)
#define LRNG_SELFTEST_NOT_EXECUTED	0xffffffff

static u32 lrng_time_selftest[LRNG_TIME_ARRAY_SIZE];

static unsigned int lrng_selftest_status = LRNG_SELFTEST_NOT_EXECUTED;

static inline void lrng_selftest_bswap32(u32 *ptr, u32 words)
{
	u32 i;

	/* Byte-swap data which is an LE representation */
	for (i = 0; i < words; i++) {
		*ptr = cpu_to_le32(*ptr);
		ptr++;
	}
}

static inline void lrng_time_process_selftest_insert(u32 time)
{
	static u32 lrng_time_selftest_ptr = 0;
	u32 ptr = lrng_time_selftest_ptr++ & LRNG_TIME_WORD_MASK;

	lrng_time_selftest[lrng_time_idx2array(ptr)] |=
		lrng_time_slot_val(time & LRNG_TIME_SLOTSIZE_MASK,
				   lrng_time_idx2slot(ptr));
}

static unsigned int lrng_time_process_selftest(void)
{
	u32 time;
	u32 idx_zero_compare = (0 << 0) | (1 << 8) | (2 << 16) | (3 << 24);
	u32 idx_one_compare  = (4 << 0) | (5 << 8) | (6 << 16) | (7 << 24);
	u32 idx_last_compare = ((LRNG_TIME_NUM_VALUES - 4) << 0)  |
			       ((LRNG_TIME_NUM_VALUES - 3) << 8)  |
			       ((LRNG_TIME_NUM_VALUES - 2) << 16) |
			       ((LRNG_TIME_NUM_VALUES - 1) << 24);

	(void)idx_one_compare;

	for (time = 0; time < LRNG_TIME_NUM_VALUES; time++)
		lrng_time_process_selftest_insert(time);

	if ((lrng_time_selftest[0] != idx_zero_compare) ||
#if (LRNG_TIME_ARRAY_SIZE > 1)
	    (lrng_time_selftest[1] != idx_one_compare)  ||
#endif
	    (lrng_time_selftest[LRNG_TIME_ARRAY_SIZE - 1] != idx_last_compare))
	{
		pr_err("LRNG time array self-test FAILED\n");
		return LRNG_SEFLTEST_ERROR_TIME;
	}

	return LRNG_SELFTEST_PASSED;
}

/*
 * The test vectors are generated with the lfsr_testvector_generation tool
 * provided as part of the test tool set of the LRNG.
 */
static unsigned int lrng_pool_lfsr_selftest(void)
{
	/*
	 * First, 67th and last entry of entropy pool.
	 *
	 * The 67th entry is picked because this one is the first to receive
	 * an entry. As we start with 1 to inject into the LFSR, the
	 * 67th entry should be equal to rol(1, 7) >> 3 considering that
	 * all other values of the LFSR are zero and the the twist value of 0
	 * is applied.
	 */
	static const u32 lrng_lfsr_selftest_result[][3] = {
		{ 0xf56df24a, 0x00000010, 0x0e014939 },
		{ 0x4b130726, 0x00000010, 0x2802f509 },
		{ 0x87279152, 0x00000010, 0x00150000 },
		{ 0x0b67f997, 0x00000010, 0x00150000 },
		{ 0x4fea174f, 0x00000010, 0xcbf4a6ae },
		{ 0x77149108, 0x00000010, 0x77bfadf2 },
		{ 0x1e96037e, 0x00000010, 0x18017e79 },
		{ 0xc84acef2, 0x00000010, 0x6345f7a8 },
		{ 0x6a2eb6df, 0x00000010, 0x03950000 },
	};
	struct lrng_pool *lrng_pool, *lrng_pool_aligned;
	u32 i, ret = LRNG_SELFTEST_PASSED;

	BUILD_BUG_ON(ARRAY_SIZE(lrng_lfsr_selftest_result) <
							CONFIG_LRNG_POOL_SIZE);

	lrng_pool = kzalloc(sizeof(struct lrng_pool) + LRNG_KCAPI_ALIGN,
			    GFP_KERNEL);
	if (!lrng_pool)
		return LRNG_SEFLTEST_ERROR_LFSR;
	lrng_pool_aligned = PTR_ALIGN(lrng_pool, sizeof(u32));

	for (i = 1; i <= LRNG_POOL_SIZE; i++)
		_lrng_pool_lfsr_u32(lrng_pool_aligned, i);

	if ((atomic_read_u32(&lrng_pool_aligned->pool[0]) !=
	     lrng_lfsr_selftest_result[CONFIG_LRNG_POOL_SIZE][0]) ||
	    (atomic_read_u32(&lrng_pool_aligned->pool[67 &
						      (LRNG_POOL_SIZE - 1)]) !=
	     lrng_lfsr_selftest_result[CONFIG_LRNG_POOL_SIZE][1]) ||
	    (atomic_read_u32(&lrng_pool_aligned->pool[LRNG_POOL_SIZE - 1]) !=
	     lrng_lfsr_selftest_result[CONFIG_LRNG_POOL_SIZE][2])) {
		pr_err("LRNG LFSR self-test FAILED\n");
		ret = LRNG_SEFLTEST_ERROR_LFSR;
	}

	kfree(lrng_pool);
	return ret;
}

/*
 * The test vectors are generated with the hash_df_testvector_generation tool
 * provided as part of the test tool set of the LRNG.
 */
static unsigned int lrng_hash_df_selftest(void)
{
	const struct lrng_crypto_cb *crypto_cb = &lrng_cc20_crypto_cb;

	/*
	 * The size of 44 bytes is chosen arbitrarily. Yet, this size should
	 * ensure that we have at least two hash blocks plus some fraction
	 * of a hash block generated.
	 */
	static const u8 lrng_hash_df_selftest_result[][44] = {
		{
			0x65, 0x48, 0xc4, 0xb3, 0x4d, 0x9c, 0xec, 0xd7,
			0x69, 0x72, 0xf7, 0x8b, 0x35, 0x23, 0xa8, 0x9a,
			0xb2, 0xe8, 0x83, 0xf8, 0xba, 0x32, 0x76, 0xae,
			0xed, 0xe2, 0x94, 0x6a, 0x93, 0x99, 0x6e, 0xce,
			0xd5, 0xb5, 0xc5, 0x16, 0xa7, 0x8d, 0xc8, 0xd3,
			0xe9, 0xdd, 0x4f, 0xca,
		}, {
			0x50, 0xcc, 0x6f, 0xe9, 0x40, 0x20, 0x40, 0x3e,
			0xce, 0x42, 0x3e, 0x30, 0x87, 0xf1, 0x3d, 0x60,
			0x75, 0xdd, 0x4f, 0x33, 0x06, 0x75, 0xbf, 0x5e,
			0x4c, 0x88, 0xc0, 0x60, 0x0f, 0x9d, 0xf9, 0xa5,
			0x63, 0xb1, 0xac, 0xc7, 0x32, 0x22, 0x60, 0xea,
			0x88, 0xe7, 0x61, 0x8b,
		}, {
			0x09, 0x96, 0xbe, 0x89, 0x16, 0x5e, 0x41, 0x82,
			0xf3, 0xab, 0xf6, 0x11, 0xef, 0x45, 0x0e, 0x87,
			0x72, 0x38, 0x40, 0xe4, 0x21, 0x0b, 0x1c, 0x45,
			0x25, 0x9c, 0x26, 0x34, 0x7e, 0xad, 0x25, 0x33,
			0xf2, 0xb0, 0xc5, 0xa7, 0x0b, 0x38, 0xd9, 0x89,
			0x02, 0x08, 0xa2, 0x5b,
		}, {
			0x10, 0x5b, 0xf4, 0x5b, 0xa9, 0xfc, 0x83, 0x2d,
			0x82, 0xf8, 0xa1, 0x17, 0x34, 0xe2, 0x67, 0xb7,
			0x95, 0xe2, 0x63, 0x2d, 0x1b, 0xf6, 0x59, 0x05,
			0x49, 0x9a, 0x3f, 0xa1, 0x16, 0xf7, 0x42, 0xd1,
			0x9c, 0x29, 0x5e, 0x31, 0xc9, 0x42, 0xf8, 0x9d,
			0x9b, 0x35, 0xd2, 0x30,
		}, {
			0x1e, 0x43, 0xfe, 0x8a, 0x66, 0x53, 0x2d, 0x94,
			0x68, 0xbe, 0xfc, 0xc6, 0xfa, 0x95, 0x4a, 0xca,
			0xa7, 0x54, 0xcd, 0x92, 0xc9, 0xca, 0xcc, 0x4f,
			0xb2, 0xc5, 0xc5, 0xb6, 0x17, 0xd7, 0xb5, 0x41,
			0xa0, 0x8e, 0xef, 0x75, 0x00, 0x96, 0x8e, 0x13,
			0x8c, 0x9f, 0xd6, 0xce,
		}, {
			0x70, 0x14, 0x94, 0x45, 0xa3, 0xb6, 0xac, 0xef,
			0x22, 0xe3, 0xe4, 0x2a, 0x38, 0x8c, 0x0e, 0x45,
			0x17, 0x61, 0x4e, 0x1d, 0xb3, 0xaf, 0xc1, 0xee,
			0x60, 0x31, 0x4d, 0xdc, 0xe1, 0x83, 0x8b, 0x85,
			0x97, 0x27, 0x30, 0x24, 0x57, 0xc2, 0xfd, 0xc0,
			0x99, 0x4b, 0xad, 0xb1,
		}, {
			0x12, 0x87, 0x51, 0x68, 0x28, 0xab, 0xa9, 0xd1,
			0x91, 0x64, 0x5e, 0x38, 0x7f, 0xf3, 0xaf, 0xd5,
			0x93, 0xbc, 0x31, 0xfd, 0xae, 0x19, 0x45, 0xd7,
			0x1f, 0xe8, 0x0c, 0x24, 0xa6, 0x6d, 0x09, 0x0b,
			0x17, 0x44, 0xdb, 0xce, 0x1c, 0x0a, 0xdb, 0x73,
			0x7a, 0x91, 0x33, 0x4c,
		}, {
			0x14, 0x81, 0x76, 0x37, 0x27, 0x19, 0x8d, 0x71,
			0xcc, 0x2e, 0xa3, 0x71, 0x92, 0x46, 0x6e, 0x3a,
			0xac, 0x87, 0xd6, 0x1e, 0xa7, 0xa9, 0x2e, 0x1e,
			0xd9, 0x6c, 0xea, 0xbe, 0x1a, 0x2e, 0xe9, 0x8a,
			0x96, 0x2a, 0xe3, 0xee, 0xd2, 0x25, 0xb2, 0xae,
			0xc6, 0xba, 0xe7, 0xef,
		}, {
			0x58, 0x78, 0xce, 0xcb, 0xcf, 0x61, 0xc2, 0x3d,
			0x00, 0x80, 0x74, 0x57, 0x56, 0x44, 0xc7, 0xe2,
			0x9a, 0xed, 0x30, 0x02, 0x3f, 0x9a, 0xf5, 0xcc,
			0xf7, 0x7b, 0x40, 0xf7, 0x10, 0x97, 0x8d, 0x8f,
			0x58, 0xa4, 0x80, 0x88, 0x87, 0x30, 0x87, 0x7b,
			0xac, 0x2e, 0xce, 0x0d,
		}
	};
	struct lrng_pool *lrng_pool, *lrng_pool_aligned;
	u8 hash_df[sizeof(lrng_hash_df_selftest_result[0])]
							__aligned(sizeof(u32));
	u32 generated;
	int ret = 0;

	BUILD_BUG_ON(ARRAY_SIZE(lrng_hash_df_selftest_result) <
							CONFIG_LRNG_POOL_SIZE);
	/* Calculated data needs to be byte-swapped */
	BUILD_BUG_ON(sizeof(lrng_hash_df_selftest_result[0]) % sizeof(32));

	lrng_pool = kzalloc(sizeof(struct lrng_pool) + LRNG_KCAPI_ALIGN,
			    GFP_KERNEL);
	if (!lrng_pool)
		return LRNG_SEFLTEST_ERROR_HASHDF;
	lrng_pool_aligned = PTR_ALIGN(lrng_pool, sizeof(u32));

	generated = __lrng_pool_hash_df(crypto_cb, NULL, lrng_pool_aligned,
					hash_df, sizeof(hash_df) << 3);

	lrng_selftest_bswap32((u32 *)hash_df,
			sizeof(lrng_hash_df_selftest_result[0]) / sizeof(u32));

	if ((generated >> 3) != sizeof(hash_df) ||
	    memcmp(hash_df, lrng_hash_df_selftest_result[CONFIG_LRNG_POOL_SIZE],
		   sizeof(hash_df))) {
		pr_err("LRNG Hash DF self-test FAILED\n");
		ret = LRNG_SEFLTEST_ERROR_HASHDF;
	}

	kfree(lrng_pool);
	return ret;
}

/*
 * The test vectors were generated using the ChaCha20 DRNG from
 * https://www.chronox.de/chacha20.html
 */
static unsigned int lrng_chacha20_drng_selftest(void)
{
	const struct lrng_crypto_cb *crypto_cb = &lrng_cc20_crypto_cb;
	u8 seed[CHACHA_KEY_SIZE * 2] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
		0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
		0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
		0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
		0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
		0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	};
	struct chacha20_block chacha20;
	int ret;
	u8 outbuf[CHACHA_KEY_SIZE * 2] __aligned(sizeof(u32));

	/*
	 * Expected result when ChaCha20 DRNG state is zero:
	 *	* constants are set to "expand 32-byte k"
	 *	* remaining state is 0
	 * and pulling one half ChaCha20 DRNG block.
	 */
	static const u8 expected_halfblock[CHACHA_KEY_SIZE] = {
		0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90,
		0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28,
		0xbd, 0xd2, 0x19, 0xb8, 0xa0, 0x8d, 0xed, 0x1a,
		0xa8, 0x36, 0xef, 0xcc, 0x8b, 0x77, 0x0d, 0xc7 };

	/*
	 * Expected result when ChaCha20 DRNG state is zero:
	 *	* constants are set to "expand 32-byte k"
	 *	* remaining state is 0
	 * followed by a reseed with two keyblocks
	 *	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	 *	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	 *	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	 *	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	 *	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	 *	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	 *	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	 *	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
	 * and pulling one ChaCha20 DRNG block.
	 */
	static const u8 expected_oneblock[CHACHA_KEY_SIZE * 2] = {
		0xf5, 0xb4, 0xb6, 0x5a, 0xec, 0xcd, 0x5a, 0x65,
		0x87, 0x56, 0xe3, 0x86, 0x51, 0x54, 0xfc, 0x90,
		0x56, 0xff, 0x5e, 0xae, 0x58, 0xf2, 0x01, 0x88,
		0xb1, 0x7e, 0xb8, 0x2e, 0x17, 0x9a, 0x27, 0xe6,
		0x86, 0xb3, 0xed, 0x33, 0xf7, 0xb9, 0x06, 0x05,
		0x8a, 0x2d, 0x1a, 0x93, 0xc9, 0x0b, 0x80, 0x04,
		0x03, 0xaa, 0x60, 0xaf, 0xd5, 0x36, 0x40, 0x11,
		0x67, 0x89, 0xb1, 0x66, 0xd5, 0x88, 0x62, 0x6d };

	/*
	 * Expected result when ChaCha20 DRNG state is zero:
	 *	* constants are set to "expand 32-byte k"
	 *	* remaining state is 0
	 * followed by a reseed with one key block plus one byte
	 *	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	 *	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	 *	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	 *	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	 *	0x20
	 * and pulling less than one ChaCha20 DRNG block.
	 */
	static const u8 expected_block_nonalinged[CHACHA_KEY_SIZE + 4] = {
		0x9d, 0xdd, 0x4f, 0xbe, 0x97, 0xcd, 0x8e, 0x15,
		0xb3, 0xc4, 0x1a, 0x17, 0x49, 0x29, 0x32, 0x7c,
		0xb3, 0x84, 0xa4, 0x9b, 0xa7, 0x14, 0xb3, 0xc1,
		0x5b, 0x3b, 0xfb, 0xa1, 0xe4, 0x23, 0x42, 0x8e,
		0x08, 0x1f, 0x53, 0xa2 };

	BUILD_BUG_ON(sizeof(seed) % sizeof(u32));

	memset(&chacha20, 0, sizeof(chacha20));
	lrng_cc20_init_rfc7539(&chacha20);
	lrng_selftest_bswap32((u32 *)seed, sizeof(seed) / sizeof(u32));

	/* Generate with zero state */
	ret = crypto_cb->lrng_drng_generate_helper(&chacha20, outbuf,
						   sizeof(expected_halfblock));
	if (ret != sizeof(expected_halfblock))
		goto err;
	if (memcmp(outbuf, expected_halfblock, sizeof(expected_halfblock)))
		goto err;

	/* Clear state of DRNG */
	memset(&chacha20.key.u[0], 0, 48);

	/* Reseed with 2 key blocks */
	ret = crypto_cb->lrng_drng_seed_helper(&chacha20, seed,
					       sizeof(expected_oneblock));
	if (ret < 0)
		goto err;
	ret = crypto_cb->lrng_drng_generate_helper(&chacha20, outbuf,
						   sizeof(expected_oneblock));
	if (ret != sizeof(expected_oneblock))
		goto err;
	if (memcmp(outbuf, expected_oneblock, sizeof(expected_oneblock)))
		goto err;

	/* Clear state of DRNG */
	memset(&chacha20.key.u[0], 0, 48);

	/* Reseed with 1 key block and one byte */
	ret = crypto_cb->lrng_drng_seed_helper(&chacha20, seed,
					sizeof(expected_block_nonalinged));
	if (ret < 0)
		goto err;
	ret = crypto_cb->lrng_drng_generate_helper(&chacha20, outbuf,
					sizeof(expected_block_nonalinged));
	if (ret != sizeof(expected_block_nonalinged))
		goto err;
	if (memcmp(outbuf, expected_block_nonalinged,
		   sizeof(expected_block_nonalinged)))
		goto err;

	return LRNG_SELFTEST_PASSED;

err:
	pr_err("LRNG ChaCha20 DRNG self-test FAILED\n");
	return LRNG_SEFLTEST_ERROR_CHACHA20;
}

static int lrng_selftest(void)
{
	unsigned int ret = lrng_time_process_selftest();

	ret |= lrng_pool_lfsr_selftest();
	ret |= lrng_chacha20_drng_selftest();
	ret |= lrng_hash_df_selftest();

	if (ret) {
		if (IS_ENABLED(CONFIG_LRNG_SELFTEST_PANIC))
			panic("LRNG self-tests failed: %u\n", ret);
	} else {
		pr_info("LRNG self-tests passed\n");
	}

	lrng_selftest_status = ret;

	if (lrng_selftest_status)
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_SYSFS
/* Re-perform self-test when any value is written to the sysfs file. */
static int lrng_selftest_sysfs_set(const char *val,
				   const struct kernel_param *kp)
{
	return lrng_selftest();
}

static const struct kernel_param_ops lrng_selftest_sysfs = {
	.set = lrng_selftest_sysfs_set,
	.get = param_get_uint,
};
module_param_cb(selftest_status, &lrng_selftest_sysfs, &lrng_selftest_status,
		0644);
#endif	/* CONFIG_SYSFS */

static int __init lrng_selftest_init(void)
{
	return lrng_selftest();
}

module_init(lrng_selftest_init);
