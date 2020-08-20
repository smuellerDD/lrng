/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * LRNG Linear Feedback Shift Register
 *
 * Copyright (C) 2016 - 2020, Stephan Mueller <smueller@chronox.de>
 */

/* Status information about IRQ noise source */
struct lrng_irq_info {
	atomic_t num_events;	/* Number of healthy IRQs since last read */
	atomic_t num_events_thresh;	/* Reseed threshold */
	atomic_t reseed_in_progress;	/* Flag for on executing reseed */
	bool irq_highres_timer;	/* Is high-resolution timer available? */
	u32 irq_entropy_bits;	/* LRNG_IRQ_ENTROPY_BITS? */
};

/*
 * This is the entropy pool used by the slow noise source. Its size should
 * be at least as large as LRNG_DRNG_SECURITY_STRENGTH_BITS.
 *
 * The pool array is aligned to 8 bytes to comfort the kernel crypto API cipher
 * implementations of the hash functions used to read the pool: for some
 * accelerated implementations, we need an alignment to avoid a realignment
 * which involves memcpy(). The alignment to 8 bytes should satisfy all crypto
 * implementations.
 *
 * LRNG_POOL_SIZE is allowed to be changed only if the taps of the polynomial
 * used for the LFSR are changed as well. The size must be in powers of 2 due
 * to the mask handling in lrng_pool_lfsr_u32 which uses AND instead of modulo.
 */
struct lrng_pool {
	union {
		struct {
			/*
			 * hash_df implementation: counter, requested_bits and
			 * pool form a linear buffer that is used in the
			 * hash_df function specified in SP800-90A section
			 * 10.3.1
			 */
			unsigned char counter;
			__be32 requested_bits;

			/* Pool */
			atomic_t pool[LRNG_POOL_SIZE] __latent_entropy;
			/* Ptr into pool for next IRQ word injection */
			atomic_t pool_ptr;
			/* rotate for LFSR */
			atomic_t input_rotate;
			/* All NUMA DRNGs seeded? */
			bool all_online_numa_node_seeded;
			/* IRQ noise source status info */
			struct lrng_irq_info irq_info;
			/* Serialize read of entropy pool */
			spinlock_t lock;
		};
		/*
		 * Static SHA-1 implementation in lrng_cc20_hash_buffer
		 * processes data 64-byte-wise. Hence, ensure proper size
		 * of LRNG entropy pool data structure.
		 */
		u8 hash_input_buf[LRNG_POOL_SIZE_BYTES + 64];
	};
};

/*
 * Implement a (modified) twisted Generalized Feedback Shift Register. (See M.
 * Matsumoto & Y. Kurita, 1992.  Twisted GFSR generators. ACM Transactions on
 * Modeling and Computer Simulation 2(3):179-194.  Also see M. Matsumoto & Y.
 * Kurita, 1994.  Twisted GFSR generators II.  ACM Transactions on Modeling and
 * Computer Simulation 4:254-266).
 */
static u32 const lrng_twist_table[8] = {
	0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
	0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278 };

/*
 * The polynomials for the LFSR are taken from the document "Table of Linear
 * Feedback Shift Registers" by Roy Ward, Tim Molteno, October 26, 2007.
 * The first polynomial is from "Primitive Binary Polynomials" by Wayne
 * Stahnke (1973) and is primitive as well as irreducible.
 *
 * Note, the tap values are smaller by one compared to the documentation because
 * they are used as an index into an array where the index starts by zero.
 *
 * All polynomials were also checked to be primitive and irreducible with magma
 * which ensures that the key property of the LFSR providing a compression
 * function for entropy is guaranteed.
 */
static u32 const lrng_lfsr_polynomial[][4] = {
	{ 15, 13, 12, 10 },			/* 16 words */
	{ 31, 29, 25, 24 },			/* 32 words */
	{ 63, 62, 60, 59 },			/* 64 words */
	{ 127, 28, 26, 1 },			/* 128 words by Stahnke */
	{ 255, 253, 250, 245 },			/* 256 words */
	{ 511, 509, 506, 503 },			/* 512 words */
	{ 1023, 1014, 1001, 1000 },		/* 1024 words */
	{ 2047, 2034, 2033, 2028 },		/* 2048 words */
	{ 4095, 4094, 4080, 4068 },		/* 4096 words */
};

static inline void _lrng_pool_lfsr_u32(struct lrng_pool *pool, u32 value)
{
	/*
	 * Process the LFSR by altering not adjacent words but rather
	 * more spaced apart words. Using a prime number ensures that all words
	 * are processed evenly. As some the LFSR polynomials taps are close
	 * together, processing adjacent words with the LSFR taps may be
	 * inappropriate as the data just mixed-in at these taps may be not
	 * independent from the current data to be mixed in.
	 */
	u32 ptr = (u32)atomic_add_return_relaxed(67, &pool->pool_ptr) &
							(LRNG_POOL_SIZE - 1);
	/*
	 * Add 7 bits of rotation to the pool. At the beginning of the
	 * pool, add an extra 7 bits rotation, so that successive passes
	 * spread the input bits across the pool evenly.
	 *
	 * Note, there is a race between getting ptr and calculating
	 * input_rotate when ptr is obtained on two or more CPUs at the
	 * same time. This race is irrelevant as it may only come into effect
	 * if 3 or more CPUs race at the same time which is very unlikely. If
	 * the race happens, it applies to one event only. As this rolling
	 * supports the LFSR without being strictly needed, we accept this
	 * race.
	 */
	u32 input_rotate = (u32)atomic_add_return_relaxed((ptr ? 7 : 14),
					&pool->input_rotate) & 31;
	u32 word = rol32(value, input_rotate);

	BUILD_BUG_ON(LRNG_POOL_WORD_BYTES != sizeof(pool->pool[0]));
	BUILD_BUG_ON(LRNG_POOL_SIZE - 1 !=
		     lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][0]);
	word ^= atomic_read_u32(&pool->pool[ptr]);
	word ^= atomic_read_u32(&pool->pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][0]) &
		       (LRNG_POOL_SIZE - 1)]);
	word ^= atomic_read_u32(&pool->pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][1]) &
		       (LRNG_POOL_SIZE - 1)]);
	word ^= atomic_read_u32(&pool->pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][2]) &
		       (LRNG_POOL_SIZE - 1)]);
	word ^= atomic_read_u32(&pool->pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][3]) &
		       (LRNG_POOL_SIZE - 1)]);

	word = (word >> 3) ^ lrng_twist_table[word & 7];
	atomic_set(&pool->pool[ptr], word);
}

u32 __lrng_pool_hash_df(const struct lrng_crypto_cb *crypto_cb, void *hash,
			struct lrng_pool *pool, u8 *outbuf, u32 requested_bits);
