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
#define LRNG_DATA_NUM_VALUES		(64)
/* Mask of LSB of time stamp to store */
#define LRNG_DATA_WORD_MASK		(LRNG_DATA_NUM_VALUES - 1)

/* Store multiple integers in one uint32_t */
#define LRNG_DATA_SLOTSIZE_BITS		(8)
#define LRNG_DATA_SLOTSIZE_MASK		((1 << LRNG_DATA_SLOTSIZE_BITS) - 1)
#define LRNG_DATA_ARRAY_MEMBER_BITS	(sizeof(uint32_t) << 3)
#define LRNG_DATA_SLOTS_PER_UINT	(LRNG_DATA_ARRAY_MEMBER_BITS / \
					 LRNG_DATA_SLOTSIZE_BITS)
#define LRNG_DATA_ARRAY_SIZE		(LRNG_DATA_NUM_VALUES /	\
					 LRNG_DATA_SLOTS_PER_UINT)

static uint32_t lrng_data[LRNG_DATA_ARRAY_SIZE];
static uint32_t lrng_data_ptr = 0;

/* Starting bit index of slot */
static inline unsigned int lrng_data_slot2bitindex(unsigned int slot)
{
	return (LRNG_DATA_SLOTSIZE_BITS * slot);
}

/* Convert index into the array index */
static inline unsigned int lrng_data_idx2array(unsigned int idx)
{
	return idx / LRNG_DATA_SLOTS_PER_UINT;
}

/* Convert index into the slot of a given array index */
static inline unsigned int lrng_data_idx2slot(unsigned int idx)
{
	return idx & (LRNG_DATA_SLOTS_PER_UINT - 1);
}

/* Insert integer to the slot */
static inline unsigned int lrng_data_slot_val(unsigned int val,
					      unsigned int slot)
{
	return val << lrng_data_slot2bitindex(slot);
}

static inline void lrng_data_process(uint32_t time)
{
	uint32_t ptr = lrng_data_ptr++ & LRNG_DATA_WORD_MASK;

	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS % LRNG_DATA_SLOTSIZE_BITS);

	lrng_data[lrng_data_idx2array(ptr)] |=
		lrng_data_slot_val(time & LRNG_DATA_SLOTSIZE_MASK,
				   lrng_data_idx2slot(ptr));

	if (ptr < LRNG_DATA_WORD_MASK)
		return;

	printf("read\n");
}

static inline void lrng_data_process_u32(uint32_t data)
{
	uint32_t pre_ptr, ptr, mask;

	/*
	 * This function injects a unit into the array - guarantee that
	 * array unit size is equal to data type of input data.
	 */
	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS != (sizeof(data) << 3));

	/* Increment pointer by number of slots taken for input value */
	lrng_data_ptr += LRNG_DATA_SLOTS_PER_UINT;

	/* ptr to current unit */
	ptr = lrng_data_ptr;
	/* ptr to previous unit */
	pre_ptr = (lrng_data_ptr - LRNG_DATA_SLOTS_PER_UINT) &
		  LRNG_DATA_WORD_MASK;
	ptr &= LRNG_DATA_WORD_MASK;

	/* mask to split data into the two parts for the two units */
	mask = ((1 << (pre_ptr & (LRNG_DATA_SLOTS_PER_UINT - 1)) *
		       LRNG_DATA_SLOTSIZE_BITS )) - 1;

	/* MSB of data go into previous unit */
	lrng_data[lrng_data_idx2array(pre_ptr)] |= data &~ mask;

	/* LSB of data go into current unit */
	lrng_data[lrng_data_idx2array(ptr)] = data & mask;
}

static void check_res(uint32_t actual1, uint32_t exp1,
		      uint32_t actual2, uint32_t exp2)
{
	if (actual1 == exp1) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       exp1, actual1);
	}

	if (actual2 == exp2) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       exp2, actual2);
	}
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

	if (LRNG_DATA_SLOTSIZE_BITS == 4) {
		idx_zero_compare = idx_zero_compare_4;
		idx_one_compare = idx_one_compare_4;
	} else if (LRNG_DATA_SLOTSIZE_BITS == 8) {
		idx_zero_compare = idx_zero_compare_8;
		idx_one_compare = idx_one_compare_8;
	} else {
		printf("No comparison\n");
		return 1;
	}

	/*
	 * Note, when using lrng_data_process_u32() on unaligned ptr,
	 * the first slots will go into next word, and the last slots go
	 * into the previous word.
	 */

	/* aligned writing of 2 32 words */
	lrng_data_process_u32((0 << 0) | (1 << 8) | (2 << 16) | (3 << 24));
	lrng_data_process_u32((4 << 0) | (5 << 8) | (6 << 16) | (7 << 24));
	check_res(lrng_data[0], idx_zero_compare, lrng_data[1], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/* non-aligned writing of on 32 word shifted by one slot */
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[2], idx_zero_compare, lrng_data[3], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/* non-aligned writing of on 32 word shifted by two slots */
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process_u32( (4 << 0) | (5 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[4], idx_zero_compare, lrng_data[5], idx_one_compare);
//	lrng_data[1] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/* non-aligned writing of on 32 word shifted by three slots */
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process(2);
	lrng_data_process_u32( (4 << 0) | (5 << 8) | (6 << 16) | (3 << 24) );
	lrng_data_process(7);
	check_res(lrng_data[6], idx_zero_compare, lrng_data[7], idx_one_compare);

	/* The following tests simply are used to advance the ptr */
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[8], idx_zero_compare, lrng_data[9], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[10], idx_zero_compare, lrng_data[11], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[12], idx_zero_compare, lrng_data[13], idx_one_compare);

	/*
	 * Clear the first words without resetting ptr to check for proper wrap.
	 */
	lrng_data[0] = 0;
	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	/*
	 * Test proper handling of ptr wrap where one slot of the following
	 * insertion goes into first slot of lrng_data[0].
	 */
	lrng_data_process_u32( (0 << 0) | (5 << 8) | (6 << 16) | (7 << 24) );

	/* We modify lrng_data[0] and lrng_data[1] */
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[14], idx_zero_compare, lrng_data[15], idx_one_compare);
	check_res(lrng_data[0], idx_zero_compare, lrng_data[1], idx_one_compare);

	lrng_data[0] = 0;
	lrng_data[1] = 0;
	lrng_data_ptr = 0;

	/* Individual writing of slot */
	for (time = 0; time < 64; time++) {
		lrng_data_process(time);
	}

	printf("Debug output storage: ");
	for (time = 0; time < LRNG_DATA_ARRAY_SIZE; time++) {
		printf("%u ", lrng_data[time]);
	}
	printf("\n");

	if (lrng_data[0] == idx_zero_compare) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       idx_zero_compare, lrng_data[0]);
	}

	if (lrng_data[1] == idx_one_compare) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED: expected %u - received %u\n",
		       idx_one_compare, lrng_data[1]);
	}
}
