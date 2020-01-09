/* Generator of LFSR self test vectors for the LRNG
 *
 * Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
 *
 * License: GPLv2
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * Test vector generation:
 *
 * 1. Set CONFIG_LRNG_POOL_SIZE to the chosen entropy pool size compliant to
 *    the Linux kernel Kconfig value.
 *
 * 2. Compile:
 *	gcc -Wall -Wextra -O2 -o lfsr_testvector_generation lfsr_testvector_generation.c
 *
 * 3. Execute and obtain the test vector for the chosen entropy pool size.
 */

#include <linux/random.h>
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

/* Set the configuration value */
#define CONFIG_LRNG_POOL_SIZE	0

typedef uint32_t	u32;
typedef uint8_t		u8;

static inline u32 rol32(u32 word, unsigned int shift)
{
	return (word << shift) | (word >> ((-shift) & 31));
}

/*************************** LRNG LFSR *****************************/

/*
 * Copy from lfsr_base.c
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

#define LRNG_POOL_SIZE		(16 << CONFIG_LRNG_POOL_SIZE)

struct lrng_pool {
	u32 pool[LRNG_POOL_SIZE];	/* Pool */
	u32 pool_ptr;		/* Ptr into pool for next IRQ word injection */
	u32 input_rotate;	/* rotate for LFSR */
};

static struct lrng_pool lrng_pool;

static u32 const lrng_twist_table[8] = {
	0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
	0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278 };

static void lrng_pool_lfsr_u32(u32 value)
{
	u32 ptr, input_rotate, word;

	lrng_pool.pool_ptr += 67;
	ptr = lrng_pool.pool_ptr & (LRNG_POOL_SIZE - 1);

	if (ptr)
		lrng_pool.input_rotate += 7;
	else
		lrng_pool.input_rotate += 14;
	input_rotate = lrng_pool.input_rotate & 31;

	word = rol32(value, input_rotate);

	word ^= lrng_pool.pool[ptr];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][0]) &
			(LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][1]) &
			(LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][2]) &
			(LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[CONFIG_LRNG_POOL_SIZE][3]) &
			(LRNG_POOL_SIZE - 1)];

	word = (word >> 3) ^ lrng_twist_table[word & 7];
	lrng_pool.pool[ptr] = word;
}

/**************************************************************************/

int main(int argc, char *argv[])
{
	u32 i;

	(void)argc;
	(void)argv;

	/* clear pool */
	memset(&lrng_pool, 0, sizeof(lrng_pool));

	/* Fill LFSR so that all words have been modified once */
	for (i = 1; i <= LRNG_POOL_SIZE; i++)
		lrng_pool_lfsr_u32(i);

	for (i = 0; i < LRNG_POOL_SIZE; i++)
		printf("%uth u32 value: 0x%.8x\n", i, lrng_pool.pool[i]);

	return 0;
}
