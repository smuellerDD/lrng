/*
 * Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
 *
 * License: see LICENSE file in root directory
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

#include <stdio.h>
#include <stdint.h>

#define BUILD_BUG_ON(condition) ((void)sizeof(char[1 - 2*!!(condition)]))

/*****************************************************************************/

/* Number of time values to store */
#define LRNG_TIME_NUM_VALUES		(64)
/* Mask of LSB of time stamp to store */
#define LRNG_TIME_WORD_MASK		(LRNG_TIME_NUM_VALUES - 1)

/* Store multiple integers in one uint32_t */
#define LRNG_TIME_SLOTSIZE_BITS		(8)
#define LRNG_TIME_SLOTSIZE_MASK		((1 << LRNG_TIME_SLOTSIZE_BITS) - 1)
#define LRNG_TIME_ARRAY_MEMBER_BITS	(sizeof(uint32_t) << 3)
#define LRNG_TIME_SLOTS_PER_UINT	(LRNG_TIME_ARRAY_MEMBER_BITS / \
					 LRNG_TIME_SLOTSIZE_BITS)
#define LRNG_TIME_ARRAY_SIZE		(LRNG_TIME_NUM_VALUES /	\
					 LRNG_TIME_SLOTS_PER_UINT)

static uint32_t lrng_time[LRNG_TIME_ARRAY_SIZE];
static uint32_t lrng_time_ptr = 0;

/* Starting bit index of slot */
static inline unsigned int lrng_time_slot2bitindex(unsigned int slot)
{
	return (LRNG_TIME_SLOTSIZE_BITS * slot);
}

/* Convert index into the array index */
static inline unsigned int lrng_time_idx2array(unsigned int idx)
{
	return idx / LRNG_TIME_SLOTS_PER_UINT;
}

/* Convert index into the slot of a given array index */
static inline unsigned int lrng_time_idx2slot(unsigned int idx)
{
	return idx & (LRNG_TIME_SLOTS_PER_UINT - 1);
}

/* Insert integer to the slot */
static inline unsigned int lrng_time_slot_val(unsigned int val,
					      unsigned int slot)
{
	return val << lrng_time_slot2bitindex(slot);
}

static inline void lrng_time_process(uint32_t time)
{
	uint32_t ptr = lrng_time_ptr++ & LRNG_TIME_WORD_MASK;

	BUILD_BUG_ON(LRNG_TIME_NUM_VALUES >= (1 << (sizeof(uint8_t) << 3)));
	BUILD_BUG_ON(LRNG_TIME_ARRAY_MEMBER_BITS % LRNG_TIME_SLOTSIZE_BITS);

	lrng_time[lrng_time_idx2array(ptr)] |=
		lrng_time_slot_val(time & LRNG_TIME_SLOTSIZE_MASK,
				   lrng_time_idx2slot(ptr));

	if (ptr < LRNG_TIME_WORD_MASK)
		return;

	printf("read\n");
}

int main(int argc, char *argv[])
{
	uint32_t time;
	uint32_t idx_zero_compare_4 =
		(0 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
		(4 << 16) | (5 << 20) | (6 << 24) | (7 << 28);
	uint32_t idx_one_compare_4 =
		(8 << 0)   | (9 << 4)   | (10 << 8)  | (11 << 12) |
		(12 << 16) | (13 << 20) | (14 << 24) | (15 << 28);
	uint32_t idx_zero_compare_8 =
		(0 << 0) | (1 << 8) | (2 << 16) | (3 << 24);
	uint32_t idx_one_compare_8 =
		(4 << 0) | (5 << 8) | (6 << 16) | (7 << 24);

	uint32_t idx_zero_compare, idx_one_compare;

	(void)argc;
	(void)argv;

	if (LRNG_TIME_SLOTSIZE_BITS == 4) {
		idx_zero_compare = idx_zero_compare_4;
		idx_one_compare = idx_one_compare_4;
	} else if (LRNG_TIME_SLOTSIZE_BITS == 8) {
		idx_zero_compare = idx_zero_compare_8;
		idx_one_compare = idx_one_compare_8;
	} else {
		printf("No comparison\n");
		return 1;
	}

	for (time = 0; time < 64; time++) {
		lrng_time_process(time);
	}

	printf("storage: ");
	for (time = 0; time < LRNG_TIME_ARRAY_SIZE; time++) {
		printf("%u ", lrng_time[time]);
	}
	printf("\n");

	if (lrng_time[0] == idx_zero_compare) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       idx_zero_compare, lrng_time[0]);
	}

	if (lrng_time[1] == idx_one_compare) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       idx_one_compare, lrng_time[1]);
	}
}
