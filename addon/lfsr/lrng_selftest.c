#include "lrng_lfsr.h"

/*
 * The test vectors are generated with the lfsr_testvector_generation tool
 * provided as part of the test tool set of the LRNG.
 */
static unsigned int lrng_pool_lfsr_selftest(void)
{
	/*
	 * LFSR state after 256 values
	 */
	static const u8 lrng_lfsr_selftest_result[] = {
		0xd3, 0x2a, 0x2f, 0xe4, 0x9e, 0x61, 0x84, 0xb5,
		0x8d, 0x9e, 0x1b, 0x2e, 0xca, 0x36, 0x1b, 0x33,
		0x4e, 0x74, 0xdd, 0x5a, 0xa6, 0x56, 0xe9, 0x66,
		0xe3, 0x69, 0x76, 0xbe, 0xb5, 0x1b, 0xaf, 0xd9,
	};
	SHASH_DESC_ON_STACK(shash, NULL);
	u8 digest[32];
	u32 i, ret;

	BUILD_BUG_ON(sizeof(digest) != sizeof(lrng_lfsr_selftest_result));

	ret = lrng_lfsr_init(shash, NULL);
	if (ret)
		return ret;

	for (i = 1; i <= 256; i++) {
		u8 tmp = i & 0xff;

		ret = lrng_lfsr_update(shash, &tmp, 1);
		if (ret)
			return ret;
	}

	ret = lrng_lfsr_final(shash, digest);
	if (ret)
		return ret;

	if (memcmp(digest, lrng_lfsr_selftest_result, sizeof(digest))) {
		pr_err("LRNG LFSR self-test FAILED\n");
		ret = LRNG_SEFLTEST_ERROR_LFSR;
	} else {
		ret = LRNG_SELFTEST_PASSED;
	}

	return ret;
}
