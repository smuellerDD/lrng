/* Demonstration of LFSR weakness in Linux /dev/random
 *
 * Copyright (C) 2017, Stephan Mueller <smueller@chronox.de>
 *
 * This test shall demonstrate the behavior of the LFSR to produce white noise.
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

static uint8_t hex_char(unsigned int bin, int u)
{
	uint8_t hex_char_map_l[] = { '0', '1', '2', '3', '4', '5', '6', '7',
				     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
	uint8_t hex_char_map_u[] = { '0', '1', '2', '3', '4', '5', '6', '7',
				     '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
	if (bin < sizeof(hex_char_map_l))
		return (u) ? hex_char_map_u[bin] : hex_char_map_l[bin];
	return 'X';
}

/**
 * Convert binary string into hex representation
 * @bin [in] input buffer with binary data
 * @binlen [in] length of bin
 * @hex [out] output buffer to store hex data
 * @hexlen [in] length of already allocated hex buffer (should be at least
 *	   twice binlen -- if not, only a fraction of binlen is converted)
 * @u [in] case of hex characters (0=>lower case, 1=>upper case)
 */
static void bin2hex(const uint8_t *bin, uint32_t binlen,
		    char *hex, uint32_t hexlen, int u)
{
	uint32_t i = 0;
	uint32_t chars = (binlen > (hexlen / 2)) ? (hexlen / 2) : binlen;

	for (i = 0; i < chars; i++) {
		hex[(i*2)] = hex_char((bin[i] >> 4), u);
		hex[((i*2)+1)] = hex_char((bin[i] & 0x0f), u);
	}
}

/**
 * Allocate sufficient space for hex representation of bin
 * and convert bin into hex
 *
 * Caller must free hex
 * @bin [in] input buffer with bin representation
 * @binlen [in] length of bin
 * @hex [out] return value holding the pointer to the newly allocated buffer
 * @hexlen [out] return value holding the allocated size of hex
 *
 * return: 0 on success, !0 otherwise
 */
int bin2hex_alloc(const uint8_t *bin, uint32_t binlen,
		  char **hex, uint32_t *hexlen)
{
	char *out = NULL;
	uint32_t outlen = 0;

	if (!binlen)
		return -EINVAL;

	outlen = (binlen) * 2;

	out = calloc(1, outlen + 1);
	if (!out)
		return -errno;

	bin2hex(bin, binlen, out, outlen, 0);
	*hex = out;
	*hexlen = outlen;
	return 0;
}

static void bin2print(const uint8_t *bin, uint32_t binlen,
		      const char *explanation)
{
	char *hex;
	uint32_t hexlen = binlen * 2 + 1;

	hex = calloc(1, hexlen);
	if (!hex)
		return;
	bin2hex(bin, binlen, hex, hexlen - 1 , 0);
	fprintf(stdout, "%s: %s\n", explanation, hex);
	free(hex);
}

static int get_random(uint8_t *buf, uint32_t buflen, unsigned int flags)
{
	int ret;

	if (buflen > INT_MAX)
		return 1;

	do {
		ret = syscall(__NR_getrandom, buf, buflen, flags);
		if (0 < ret) {
			buflen -= ret;
			buf += ret;
		}
	} while ((0 < ret || EINTR == errno || ERESTART == errno)
		 && buflen > 0);

	if (buflen == 0)
		return 0;

	return 1;
}

typedef uint32_t	u32;
typedef uint8_t		u8;

static inline u32 rol32(u32 word, unsigned int shift)
{
	return (word << shift) | (word >> ((-shift) & 31));
}

/*
 * Copy from lfsr_base.c
 */
static u32 const lrng_lfsr_polynomial[] =
	{ 127, 28, 26, 1 };			/* 128 words by Stahnke */
	/* { 255, 253, 250, 245 }; */		/* 256 words */
	/* { 511, 509, 506, 503 }; */		/* 512 words */
	/* { 1023, 1014, 1001, 1000 }; */	/* 1024 words */
	/* { 2047, 2034, 2033, 2028 }; */	/* 2048 words */
	/* { 4095, 4094, 4080, 4068 }; */	/* 4096 words */

#define LRNG_POOL_SIZE 128

uint32_t stats[LRNG_POOL_SIZE] = { 0 };

struct lrng_pool {
#define LRNG_POOL_SIZE 128
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
	u32 ptr;

	/*
	 * Add 7 bits of rotation to the pool. At the beginning of the
	 * pool, add an extra 7 bits rotation, so that successive passes
	 * spread the input bits across the pool evenly.
	 */
	u32 input_rotate = lrng_pool.input_rotate;
	u32 word = rol32(value, input_rotate);

	lrng_pool.pool_ptr += 67;
	ptr = lrng_pool.pool_ptr & (LRNG_POOL_SIZE - 1);
	stats[ptr]++;

	if (ptr)
		lrng_pool.input_rotate += 7;
	else
		lrng_pool.input_rotate += 14;
	lrng_pool.input_rotate &= 31;

	word ^= lrng_pool.pool[ptr];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[0]) & (LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[1]) & (LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[2]) & (LRNG_POOL_SIZE - 1)];
	word ^= lrng_pool.pool[
		(ptr + lrng_lfsr_polynomial[3]) & (LRNG_POOL_SIZE - 1)];

	word = (word >> 3) ^ lrng_twist_table[word & 7];
	lrng_pool.pool[ptr] = word;
}

/* invoke function with buffer aligned to 4 bytes */
static inline void lrng_pool_lfsr(const u8 *buf, u32 buflen)
{
	u32 *p_buf = (u32 *)buf;

	for (; buflen >= 4; buflen -= 4)
		lrng_pool_lfsr_u32(*p_buf++);

	buf = (u8 *)p_buf;
	while (buflen--)
		lrng_pool_lfsr_u32(*buf++);
}

static inline void lrng_pool_lfsr_nonalinged(const u8 *buf, u32 buflen)
{
	if (!((unsigned long)buf & (sizeof(u32) - 1)))
		lrng_pool_lfsr(buf, buflen);
	else {
		while (buflen--)
			lrng_pool_lfsr_u32(*buf++);
	}
}

int main(int argc, char *argv[])
{
	u32 i, j, compare, imbalance = 0;

	(void)argc;
	(void)argv;

	/*
	 * init pool
	 */
	//get_random((u8 *)lrng_pool.pool, sizeof(lrng_pool.pool), 0);
	memset(lrng_pool.pool, 0, sizeof(lrng_pool.pool));

	/* print the initial content of the pool */
//	bin2print((uint8_t *)lrng_pool.pool, sizeof(lrng_pool.pool), "Initial pool");

	for (j = 0; j < 100000; j++) {
		for (i = 1; i <= (LRNG_POOL_SIZE); i++) {
			lrng_pool_lfsr_u32(i);
//			printf("Pool state after %u LFSR ", i);
//			bin2print((uint8_t *)lrng_pool.pool, sizeof(lrng_pool.pool), "rounds");
		}
		fwrite(&lrng_pool.pool, sizeof(lrng_pool.pool), 1, stdout);
	}

	/* print out the final content of the pool -- it is now zero */
//	bin2print((uint8_t *)lrng_pool.pool, sizeof(lrng_pool.pool), "Pool after final LFSR round");

	for (i = 0; i < LRNG_POOL_SIZE; i++) {
		if (!i) {
			compare = stats[i];
			continue;
		}

		if (stats[i] != compare) {
			imbalance = 1;
			fprintf(stderr, "Imbalance in LFSR detected\n");
			break;
		}
	}

	if (imbalance) {
		for (i = 0; i < LRNG_POOL_SIZE; i++) {
			fprintf(stderr, "LFSR slot %u:\t%u\n", i, stats[i]);
		}
	} else {
		fprintf(stderr, "Balanced LFSR\n");
	}

	return 0;
}
