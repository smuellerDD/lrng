/* Generator of Hash-DF self test vectors for the LRNG
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
 *	gcc -Wall -Wextra -O2 -o hash_df_testvector_generation hash_df_testvector_generation.c
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
#define CONFIG_LRNG_POOL_SIZE	8

/* Use SHA-256 - if not defined, use SHA-1 */
#define SHA256

enum { false, true };
typedef _Bool bool;

typedef uint32_t	u32;
typedef uint32_t	__be32;
typedef uint32_t	atomic_t;
typedef uint8_t		u8;
typedef uint32_t	spinlock_t;

#define LRNG_POOL_SIZE			(16 << CONFIG_LRNG_POOL_SIZE)
#define LRNG_POOL_WORD_BYTES		(sizeof(atomic_t))
#define LRNG_POOL_SIZE_BYTES		(LRNG_POOL_SIZE * LRNG_POOL_WORD_BYTES)

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
			atomic_t pool[LRNG_POOL_SIZE];
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

/****************
 * Rotate the 32 bit unsigned integer X by N bits left/right
 */
static inline uint32_t rol(uint32_t x, int n)
{
	return ( (x << (n&(32-1))) | (x >> ((32-n)&(32-1))) );
}

static inline uint32_t ror(uint32_t x, int n)
{
	return ( (x >> (n&(32-1))) | (x << ((32-n)&(32-1))) );
}

static inline uint32_t _bswap32(uint32_t x)
{
	return ((rol(x, 8) & 0x00ff00ffL) | (ror(x, 8) & 0xff00ff00L));
}

#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)
#if GCC_VERSION >= 40400
# define __HAVE_BUILTIN_BSWAP32__
#endif

#ifdef __HAVE_BUILTIN_BSWAP32__
# define _swap32(x) (uint32_t)__builtin_bswap32((uint32_t)(x))
#else
# define _swap32(x) _bswap32(x)
#endif

/* Endian dependent byte swap operations.  */
/* Endian dependent byte swap operations.  */
#if __BYTE_ORDER__ ==  __ORDER_BIG_ENDIAN__
# define le_bswap32(x) _swap32(x)
# define be_bswap32(x) ((uint32_t)(x))
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define le_bswap32(x) ((uint32_t)(x))
# define be_bswap32(x) _swap32(x)
#else
# error "Endianess not defined"
#endif

/*****************************************************************************
 * Hashing code
 *****************************************************************************/

/*
 * FreeOTP
 *
 * Authors: Nathaniel McCallum <npmccallum@redhat.com>
 *
 * Copyright (C) 2014  Nathaniel McCallum, Red Hat
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef SHA256

#define SHA256_SIZE_BLOCK 64
#define SHA256_SIZE_HASH 32

struct hash_ctx {
  uint64_t len;
  uint32_t h[8];
  uint8_t buf[SHA256_SIZE_BLOCK];
};

#define Ch(x,y,z)  (z ^ (x & (y ^ z)))
#define Maj(x,y,z) ((x & y) | (z & (x | y)))
#define S0(x)      (ror(x,2) ^ ror(x,13) ^ ror(x,22))
#define S1(x)      (ror(x,6) ^ ror(x,11) ^ ror(x,25))
#define R0(x)      (ror(x,7) ^ ror(x,18) ^ (x>>3))
#define R1(x)      (ror(x,17) ^ ror(x,19) ^ (x>>10))

static const uint32_t K[] = {
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
  0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
  0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
  0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
  0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
  0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
  0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void
processblock(struct hash_ctx *ctx, const uint8_t *buf)
{
  uint32_t W[64], t1, t2, a, b, c, d, e, f, g, h;
  int i;

  for (i = 0; i < 16; i++) {
    W[i]  = (uint32_t) buf[4 * i + 0] << 24;
    W[i] |= (uint32_t) buf[4 * i + 1] << 16;
    W[i] |= (uint32_t) buf[4 * i + 2] << 8;
    W[i] |= buf[4 * i + 3];
  }

  for (; i < 64; i++)
    W[i] = R1(W[i-2]) + W[i - 7] + R0(W[i - 15]) + W[i - 16];

  a = ctx->h[0];
  b = ctx->h[1];
  c = ctx->h[2];
  d = ctx->h[3];
  e = ctx->h[4];
  f = ctx->h[5];
  g = ctx->h[6];
  h = ctx->h[7];

  #define ROUND(a,b,c,d,e,f,g,h,i) \
		t1 = h + S1(e) + Ch(e,f,g) + K[i] + W[i]; \
		t2 = S0(a) + Maj(a,b,c); \
		d += t1; \
		h = t1 + t2;
  for (i = 0; i < 64;) {
    ROUND(a, b, c, d, e, f, g, h, i); i++;
    ROUND(h, a, b, c, d, e, f, g, i); i++;
    ROUND(g, h, a, b, c, d, e, f, i); i++;
    ROUND(f, g, h, a, b, c, d, e, i); i++;
    ROUND(e, f, g, h, a, b, c, d, i); i++;
    ROUND(d, e, f, g, h, a, b, c, i); i++;
    ROUND(c, d, e, f, g, h, a, b, i); i++;
    ROUND(b, c, d, e, f, g, h, a, i); i++;
  }

  ctx->h[0] += a;
  ctx->h[1] += b;
  ctx->h[2] += c;
  ctx->h[3] += d;
  ctx->h[4] += e;
  ctx->h[5] += f;
  ctx->h[6] += g;
  ctx->h[7] += h;
}

static void
pad(struct hash_ctx *ctx)
{
  unsigned r = ctx->len % 64;

  ctx->buf[r++] = 0x80;
  if (r > 56) {
    memset(ctx->buf + r, 0, 64 - r); r = 0;
    processblock(ctx, ctx->buf);
  }

  memset(ctx->buf + r, 0, 56 - r);

  ctx->len *= 8;
  ctx->buf[56] = ctx->len >> 56;
  ctx->buf[57] = ctx->len >> 48;
  ctx->buf[58] = ctx->len >> 40;
  ctx->buf[59] = ctx->len >> 32;
  ctx->buf[60] = ctx->len >> 24;
  ctx->buf[61] = ctx->len >> 16;
  ctx->buf[62] = ctx->len >> 8;
  ctx->buf[63] = ctx->len;

  processblock(ctx, ctx->buf);
}

void
sha256_init(struct hash_ctx *ctx)
{
  ctx->len = 0;
  ctx->h[0] = 0x6a09e667;
  ctx->h[1] = 0xbb67ae85;
  ctx->h[2] = 0x3c6ef372;
  ctx->h[3] = 0xa54ff53a;
  ctx->h[4] = 0x510e527f;
  ctx->h[5] = 0x9b05688c;
  ctx->h[6] = 0x1f83d9ab;
  ctx->h[7] = 0x5be0cd19;
}

void
sha256_update(struct hash_ctx *ctx, const void *buf, size_t len)
{
  const uint8_t *p = buf;
  unsigned r = ctx->len % 64;

  ctx->len += len;
  if (r) {
    if (len < 64 - r) {
      memcpy(ctx->buf + r, p, len);
      return;
    }

    memcpy(ctx->buf + r, p, 64 - r);

    len -= 64 - r;
    p += 64 - r;

    processblock(ctx, ctx->buf);
  }

  for (; len >= 64; len -= 64, p += 64)
    processblock(ctx, p);

  memcpy(ctx->buf, p, len);
}

void
sha256_finish(struct hash_ctx *ctx, uint8_t *hash)
{
  int i;

  pad(ctx);
  for (i = 0; i < 8; i++) {
    hash[4 * i + 0] = ctx->h[i] >> 24;
    hash[4 * i + 1] = ctx->h[i] >> 16;
    hash[4 * i + 2] = ctx->h[i] >> 8;
    hash[4 * i + 3] = ctx->h[i];
  }
}

#else /* SHA256 */

#define F0(b,c,d) (d ^ (b & (c ^ d)))
#define F1(b,c,d) (b ^ c ^ d)
#define F2(b,c,d) ((b & c) | (d & (b | c)))
#define F3(b,c,d) (b ^ c ^ d)
#define G0(a,b,c,d,e,i) e += rol(a,5)+F0(b,c,d)+W[i]+0x5A827999; b = rol(b,30)
#define G1(a,b,c,d,e,i) e += rol(a,5)+F1(b,c,d)+W[i]+0x6ED9EBA1; b = rol(b,30)
#define G2(a,b,c,d,e,i) e += rol(a,5)+F2(b,c,d)+W[i]+0x8F1BBCDC; b = rol(b,30)
#define G3(a,b,c,d,e,i) e += rol(a,5)+F3(b,c,d)+W[i]+0xCA62C1D6; b = rol(b,30)

#define SHA1_SIZE_BLOCK 64
#define SHA1_SIZE_HASH  20
struct hash_ctx {
  uint64_t len;
  uint32_t h[5];
  uint8_t buf[SHA1_SIZE_BLOCK];
};

static void processblock(struct hash_ctx *ctx, const uint8_t *buf)
{
  uint32_t W[80], a, b, c, d, e;
  int i;

  for (i = 0; i < 16; i++) {
    W[i]  = (uint32_t) buf[4 * i + 0] << 24;
    W[i] |= (uint32_t) buf[4 * i + 1] << 16;
    W[i] |= (uint32_t) buf[4 * i + 2] << 8;
    W[i] |= buf[4 * i + 3];
  }

  for (; i < 80; i++)
    W[i] = rol(W[i - 3] ^ W[i - 8] ^ W[i - 14] ^ W[i - 16], 1);

  a = ctx->h[0];
  b = ctx->h[1];
  c = ctx->h[2];
  d = ctx->h[3];
  e = ctx->h[4];

  for (i = 0; i < 20;) {
    G0(a, b, c, d, e, i++);
    G0(e, a, b, c, d, i++);
    G0(d, e, a, b, c, i++);
    G0(c, d, e, a, b, i++);
    G0(b, c, d, e, a, i++);
  }

  for (; i < 40;) {
    G1(a, b, c, d, e, i++);
    G1(e, a, b, c, d, i++);
    G1(d, e, a, b, c, i++);
    G1(c, d, e, a, b, i++);
    G1(b, c, d, e, a, i++);
  }

  for (; i < 60;) {
    G2(a, b, c, d, e, i++);
    G2(e, a, b, c, d, i++);
    G2(d, e, a, b, c, i++);
    G2(c, d, e, a, b, i++);
    G2(b, c, d, e, a, i++);
  }

  for (; i < 80;) {
    G3(a, b, c, d, e, i++);
    G3(e, a, b, c, d, i++);
    G3(d, e, a, b, c, i++);
    G3(c, d, e, a, b, i++);
    G3(b, c, d, e, a, i++);
  }

  ctx->h[0] += a;
  ctx->h[1] += b;
  ctx->h[2] += c;
  ctx->h[3] += d;
  ctx->h[4] += e;
}

static void sha1_init(struct hash_ctx *ctx)
{
  ctx->len = 0;
  ctx->h[0] = 0x67452301;
  ctx->h[1] = 0xEFCDAB89;
  ctx->h[2] = 0x98BADCFE;
  ctx->h[3] = 0x10325476;
  ctx->h[4] = 0xC3D2E1F0;
}

#endif /* SHA256 */

/**************************************************************************/

int main(int argc, char *argv[])
{
	struct lrng_pool lrng_pool;
	struct hash_ctx ctx;
	uint8_t hash_df[44];
	uint32_t *hash_df_ptr32;
	uint8_t *hash_df_ptr = hash_df;
	unsigned int hash_df_len = sizeof(hash_df), digestsize, i;
	int ret = 0;

	(void)argc;
	(void)argv;

	/* clear pool */
	memset(&lrng_pool, 0, sizeof(lrng_pool));

	/* initialize the hash_df operation */
	lrng_pool.counter = 1;
	lrng_pool.requested_bits = be_bswap32((uint32_t)(sizeof(hash_df) << 3));

#ifdef SHA256
	digestsize = SHA256_SIZE_HASH;
#else
	digestsize = 20;
#endif

	while (hash_df_len) {
		unsigned int todo = (hash_df_len < digestsize) ? hash_df_len :
								 digestsize;

#ifdef SHA256
		uint8_t digest[SHA256_SIZE_HASH];

		sha256_init(&ctx);
		sha256_update(&ctx, &lrng_pool, (LRNG_POOL_SIZE_BYTES + 64));
		sha256_finish(&ctx, digest);
		memcpy(hash_df_ptr, digest, todo);

#else /* SHA256 */

		sha1_init(&ctx);

		for (i = 0; i < (LRNG_POOL_SIZE_BYTES + 64);
		     i += (SHA1_SIZE_BLOCK))
			processblock(&ctx, (uint8_t *)&lrng_pool + i);

		/*
		 * The hash_df operation in the kernel for SHA-1 skips the
		 * SHA-1 padding mechanism. This is ok as we always have full
		 * blocks for SHA-1.
		 */

		memcpy(hash_df_ptr, ctx.h, todo);
#endif /* SHA256 */

		hash_df_len -= todo;
		hash_df_ptr += todo;
		lrng_pool.counter++;
	}

	hash_df_ptr32 = (uint32_t *)hash_df;
	for (i = 0; i < sizeof(hash_df) / sizeof(uint32_t); i++) {
		*hash_df_ptr32 = le_bswap32(*hash_df_ptr32);
		hash_df_ptr32++;
	}

	printf("static u8 const expected[] = {\n\t");
	for (i = 0; i < sizeof(hash_df); i++) {
		if (i && !(i % 8))
			printf("\n\t");

		printf("0x%.2x, ", hash_df[i]);
	}
	printf("\n};\n");

	return ret;
}
