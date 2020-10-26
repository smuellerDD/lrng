/* Generator of LFSR self test vectors for the LRNG
 *
 * Copyright (C) 2020, Stephan Mueller <smueller@chronox.de>
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
 * Compile:
 *	gcc -Wall -Wextra -O2 -I../ -o lfsr_testvector_generation lfsr_testvector_generation.c
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
	u32 i;

	(void)argc;
	(void)argv;

	/* lrng_lfsr_init */
	memset(&lfsr, 0, sizeof(lfsr));

	/* Fill LFSR so that all words have been modified once */
	for (i = 1; i <= 256; i++)
		lrng_lfsr_u8(&lfsr, (u8)(i & 0xff));

	for (i = 0; i < lfsr_statesize(); i++) {
		if (i == 0)
			printf("static const u8 lrng_lfsr_selftest_result[] = {\n\t");
		else if (!(i % 8))
			printf("\n\t");
		printf("0x%.2x, " , lfsr.pool[i]);
	}
	printf("\n};\n");

	return 0;
}
