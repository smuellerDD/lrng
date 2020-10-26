/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * LRNG Linear Feedback Shift Register
 *
 * Copyright (C) 2020, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _LRNG_LFSR_H
#define _LRNG_LFSR_H

/*
 * The output of the LFSR shall not be truncated. Thus, use a state
 * size where the state can be copied into the seed buffer unaltered.
 */
#define LRNG_LFSR_STATESIZE	32 /* LRNG_DRNG_SECURITY_STRENGTH_BYTES */

struct lrng_lfsr_ctx {
	u32 ptr;
	u32 input_rotate;
	u8 pool[LRNG_LFSR_STATESIZE];
};

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
static u32 const lfsr_polynomial[] =
//	{ 15, 13, 12, 10 };			/* 16 words */
	{ 31, 29, 25, 24 };			/* 32 words */
//	{ 63, 62, 60, 59 };			/* 64 words */
//	{ 127, 28, 26, 1 };			/* 128 words by Stahnke */
//	{ 255, 253, 250, 245 };			/* 256 words */
//	{ 511, 509, 506, 503 };			/* 512 words */
//	{ 1023, 1014, 1001, 1000 };		/* 1024 words */
//	{ 2047, 2034, 2033, 2028 };		/* 2048 words */
//	{ 4095, 4094, 4080, 4068 };		/* 4096 words */

static inline u32 lfsr_statesize(void)
{
	BUILD_BUG_ON(LRNG_LFSR_STATESIZE != (lfsr_polynomial[0] + 1));

	return (LRNG_LFSR_STATESIZE);
}

static inline void lrng_lfsr_u8(struct lrng_lfsr_ctx *lfsr, u8 value)
{
	u8 word;
	u32 ptr;

	/*
	 * Process the LFSR by altering not adjacent words but rather
	 * more spaced apart words. Using a prime number ensures that all words
	 * are processed evenly. As some the LFSR polynomials taps are close
	 * together, processing adjacent words with the LSFR taps may be
	 * inappropriate as the data just mixed-in at these taps may be not
	 * independent from the current data to be mixed in.
	 */
	ptr = (lfsr->ptr + 13) & lfsr_polynomial[0];
	lfsr->ptr = ptr;

	/*
	 * Add 3 bits of rotation to the pool. At the beginning of the
	 * pool, add an extra 3 bits rotation, so that successive passes
	 * spread the input bits across the pool evenly.
	 */
	lfsr->input_rotate = (lfsr->input_rotate + (ptr ? 3 : 6)) & 7;
	word = rol8(value, lfsr->input_rotate);

	word ^= lfsr->pool[ptr];
	word ^= lfsr->pool[(ptr + lfsr_polynomial[0]) & lfsr_polynomial[0]];
	word ^= lfsr->pool[(ptr + lfsr_polynomial[1]) & lfsr_polynomial[0]];
	word ^= lfsr->pool[(ptr + lfsr_polynomial[2]) & lfsr_polynomial[0]];
	word ^= lfsr->pool[(ptr + lfsr_polynomial[3]) & lfsr_polynomial[0]];

	lfsr->pool[ptr] = word;
}

const char *lrng_lfsr_name(void);
void *lrng_lfsr_alloc(void);
u32 lrng_lfsr_digestsize(void *hash);
void lrng_lfsr_dealloc(void *hash);
int lrng_lfsr_init(struct shash_desc *shash, void *hash);
int lrng_lfsr_update(struct shash_desc *shash, const u8 *inbuf, u32 inbuflen);
int lrng_lfsr_final(struct shash_desc *shash, u8 *digest);

#endif /* _LRNG_LFSR_H */
