/* Demonstration of LFSR of LRNG
 *
 * Copyright (C) 2020, Stephan Mueller <smueller@chronox.de>
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

/*
 * Test approach:
 *
 * 1. Ensure lrng_lfsr.h from your tested kernel is included into this
 *    file.
 *
 * 2. Compile:
 *	gcc -Wall -Wextra -O2 -I../ -o lfsr_demonstration lfsr_demonstration.c
 *
 * Expected result:
 *	stdout generates a bit-stream that can be analyzed with dieharder, ent
 *	or the NIST SP800-90B tool set (IID case) to show that we have white
 *	noise.
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

#if 0
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
#endif

/******************************* Kernel aliases ******************************/
typedef uint32_t	u32;
typedef uint8_t		u8;
struct shash_desc { uint8_t unused; };

static inline u8 rol8(u8 word, unsigned int shift)
{
	return (word << (shift & 7)) | (word >> ((-shift) & 7));
}

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

/******************************** LRNG LFSR **********************************/

# include "lrng_lfsr.h"

/**************************************************************************/

int main(int argc, char *argv[])
{
	struct lrng_lfsr_ctx lfsr;
	u32 i, j;

	(void)argc;
	(void)argv;

	/* lrng_lfsr_init */
	memset(&lfsr, 0, sizeof(lfsr));

	for (j = 0; j < 100000; j++) {
		/*
		 * Inject 256 "event values" assuming that the LRNG
		 * uses one 8-bit value per event. To get full entropy,
		 * the LRNG must inject 256 events.
		 */
		for (i = 0; i < 256; i++) {
			lrng_lfsr_u8(&lfsr, (u8)(i & 0xff));
//			printf("Pool state after %u LFSR ", i);
//			bin2print(lfsr.pool, sizeof(lfsr.pool), "rounds");
		}
		fwrite(&lfsr.pool, sizeof(lfsr.pool), 1, stderr);
	}

	/* print out the final content of the pool -- it is now zero */
//	bin2print(lfsr.pool, sizeof(lfsr.pool), "Pool after final LFSR round");

	return 0;
}
