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

/*
 * Return the pointers for the previous and current units to inject a u32 into.
 * Also return the mask which which the u32 word is to be processed.
 */
static inline void lrng_pcpu_split_u32(uint32_t *ptr, uint32_t *pre_ptr,
				       uint32_t *mask)
{
	/* ptr to previous unit */
	*pre_ptr = (*ptr - LRNG_DATA_SLOTS_PER_UINT) & LRNG_DATA_WORD_MASK;
	*ptr &= LRNG_DATA_WORD_MASK;

	/* mask to split data into the two parts for the two units */
	*mask = ((1 << (*pre_ptr & (LRNG_DATA_SLOTS_PER_UINT - 1)) *
			LRNG_DATA_SLOTSIZE_BITS)) - 1;
}

static inline void lrng_data_process(uint32_t time)
{
	uint32_t ptr = lrng_data_ptr++ & LRNG_DATA_WORD_MASK;
	unsigned int array = lrng_data_idx2array(ptr);
	unsigned int slot = lrng_data_idx2slot(ptr);

	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS % LRNG_DATA_SLOTSIZE_BITS);
	/* zeroization of slot to ensure the following OR adds the data */
	lrng_data[array] &=
		~(lrng_data_slot_val(0xffffffff & LRNG_DATA_SLOTSIZE_MASK,
				     slot));
	lrng_data[array] |= lrng_data_slot_val(time & LRNG_DATA_SLOTSIZE_MASK,
					       slot);

	if (ptr < LRNG_DATA_WORD_MASK)
		return;

	printf("read\n");
}

static inline void lrng_data_process_u32(uint32_t data)
{
	uint32_t pre_ptr, ptr, mask;
	unsigned int pre_array;

	/*
	 * This function injects a unit into the array - guarantee that
	 * array unit size is equal to data type of input data.
	 */
	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS != (sizeof(data) << 3));

	/* Increment pointer by number of slots taken for input value */
	lrng_data_ptr += LRNG_DATA_SLOTS_PER_UINT;
	ptr = lrng_data_ptr;

	lrng_pcpu_split_u32(&ptr, &pre_ptr, &mask);

	/* MSB of data go into previous unit */
	pre_array = lrng_data_idx2array(pre_ptr);
	/* zeroization of slot to ensure the following OR adds the data */
	lrng_data[pre_array] &= ~(0xffffffff &~ mask);
	lrng_data[pre_array] |= data &~ mask;

	/* LSB of data go into current unit */
	lrng_data[lrng_data_idx2array(ptr)] = data & mask;
}

static void check_res(uint32_t actual1, uint32_t exp1,
		      uint32_t actual2, uint32_t exp2)
{
	if (actual1 == exp1) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED 1: expected %u - received %u\n",
		       exp1, actual1);
	}

	if (actual2 == exp2) {
		printf("Test PASSED\n");
	} else {
		printf("Test FAILED 2: expected %u - received %u\n",
		       exp2, actual2);
	}
}

int main(int argc, char *argv[])
{
	uint32_t time;

#if (LRNG_DATA_SLOTSIZE_BITS == 4)
	uint32_t idx_zero_compare =
		(0 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
		(4 << 16) | (5 << 20) | (6 << 24) | (7 << 28);
	uint32_t idx_one_compare =
		(8 << 0)   | (9 << 4)   | (10 << 8)  | (11 << 12) |
		(12 << 16) | (13 << 20) | (14 << 24) | (15 << 28);
#elif (LRNG_DATA_SLOTSIZE_BITS == 8)
	uint32_t idx_zero_compare =
		(0 << 0) | (1 << 8) | (2 << 16) | (3 << 24);
	uint32_t idx_one_compare =
		(4 << 0) | (5 << 8) | (6 << 16) | (7 << 24);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	uint32_t idx_zero_compare =
		(0 << 0) | (1 << 16);
	uint32_t idx_one_compare =
		(2 << 0) | (3 << 16);
#else
# error "Unknown LRNG_DATA_SLOTSIZE_BITS"
#endif

	(void)argc;
	(void)argv;

	/*
	 * Note, when using lrng_data_process_u32() on unaligned ptr,
	 * the first slots will go into next word, and the last slots go
	 * into the previous word.
	 */

	/* aligned writing of 2 32 words including the check of zeroization */
	lrng_data[0] = 0xffffffff;
	lrng_data[1] = 0xffffffff;
#if (LRNG_DATA_SLOTSIZE_BITS == 4)
	lrng_data_process_u32((0 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
			      (4 << 16) | (5 << 20) | (6 << 24) | (7 << 28));
	lrng_data_process_u32((8 << 0)   | (9 << 4)   | (10 << 8)  | (11 << 12) |
			      (12 << 16) | (13 << 20) | (14 << 24) | (15 << 28));
#elif (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process_u32((0 << 0) | (1 << 8) | (2 << 16) | (3 << 24));
	lrng_data_process_u32((4 << 0) | (5 << 8) | (6 << 16) | (7 << 24));
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process_u32((0 << 0) | (1 << 16));
	lrng_data_process_u32((2 << 0) | (3 << 16));
#endif
	check_res(lrng_data[0], idx_zero_compare, lrng_data[1], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/*
	 * non-aligned writing of on 32 word shifted by one slot including the
	 * check of zeroization
	 */
	lrng_data[2] = 0xffffffff;
	lrng_data[3] = 0xffffffff;
#if (LRNG_DATA_SLOTSIZE_BITS == 4)
	lrng_data_process(0);
	lrng_data_process_u32((8 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
			      (4 << 16) | (5 << 20) | (6 << 24) | (7 << 28) );
	lrng_data_process(9);
	lrng_data_process(10);
	lrng_data_process(11);
	lrng_data_process(12);
	lrng_data_process(13);
	lrng_data_process(14);
	lrng_data_process(15);
#elif (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
#endif
	check_res(lrng_data[2], idx_zero_compare, lrng_data[3], idx_one_compare);
//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/* non-aligned writing of on 32 word shifted by two slots */
#if (LRNG_DATA_SLOTSIZE_BITS == 4)
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process_u32((8 << 0)  | (9 << 4)  | (2 << 8)  | (3 << 12) |
			      (4 << 16) | (5 << 20) | (6 << 24) | (7 << 28));
	lrng_data_process(10);
	lrng_data_process(11);
	lrng_data_process(12);
	lrng_data_process(13);
	lrng_data_process(14);
	lrng_data_process(15);
#elif (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process_u32( (4 << 0) | (5 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(6);
	lrng_data_process(7);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process_u32((2 << 0) | (3 << 16));
#endif
	check_res(lrng_data[4], idx_zero_compare, lrng_data[5], idx_one_compare);
//	lrng_data[1] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

	/* non-aligned writing of on 32 word shifted by three slots */
#if (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process(2);
	lrng_data_process_u32( (4 << 0) | (5 << 8) | (6 << 16) | (3 << 24) );
	lrng_data_process(7);
	check_res(lrng_data[6], idx_zero_compare, lrng_data[7], idx_one_compare);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process(1);
	lrng_data_process_u32((2 << 0) | (3 << 16));
	check_res(lrng_data[6], idx_zero_compare, lrng_data[7], idx_one_compare);
#endif

	/* The following tests simply are used to advance the ptr */
#if (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[8], idx_zero_compare, lrng_data[9], idx_one_compare);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[8], idx_zero_compare, lrng_data[9], idx_one_compare);
#endif

//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

#if (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[10], idx_zero_compare, lrng_data[11], idx_one_compare);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[10], idx_zero_compare, lrng_data[11], idx_one_compare);
#endif

//	lrng_data[0] = 0;
//	lrng_data[1] = 0;
//	lrng_data_ptr = 0;

#if (LRNG_DATA_SLOTSIZE_BITS == 8)
	lrng_data_process(0);
	lrng_data_process_u32( (4 << 0) | (1 << 8) | (2 << 16) | (3 << 24) );
	lrng_data_process(5);
	lrng_data_process(6);
	lrng_data_process(7);
	check_res(lrng_data[12], idx_zero_compare, lrng_data[13], idx_one_compare);
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[12], idx_zero_compare, lrng_data[13], idx_one_compare);
#endif

#if (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[14], idx_zero_compare, lrng_data[15], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[16], idx_zero_compare, lrng_data[17], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[18], idx_zero_compare, lrng_data[19], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[20], idx_zero_compare, lrng_data[21], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[22], idx_zero_compare, lrng_data[23], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[24], idx_zero_compare, lrng_data[25], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[26], idx_zero_compare, lrng_data[27], idx_one_compare);

	lrng_data_process(0);
	lrng_data_process_u32( (2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[28], idx_zero_compare, lrng_data[29], idx_one_compare);
#endif

	/*
	 * Clear the first words without resetting ptr to check for proper wrap.
	 */
	lrng_data[0] = 0;
	lrng_data[1] = 0;
//	lrng_data_ptr = 0;
#if (LRNG_DATA_SLOTSIZE_BITS == 4)
	lrng_data_process(0);
	lrng_data_process_u32((8 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
			      (4 << 16) | (5 << 20) | (6 << 24) | (7 << 28) );
	/*
	 * Test proper handling of ptr wrap where one slot of the following
	 * insertion goes into first slot of lrng_data[0].
	 */
	lrng_data_process_u32((0 << 0) | (9 << 4) | (10 << 8) | (11 << 12) |
			      (12 << 16) | (13 << 20) | (14 << 24) | (15 << 28));

	/* We modify lrng_data[0] and lrng_data[1] */
	lrng_data_process_u32((8 << 0)  | (1 << 4)  | (2 << 8)  | (3 << 12) |
			      (4 << 16) | (5 << 20) | (6 << 24) | (7 << 28));
	lrng_data_process(9);
	lrng_data_process(10);
	lrng_data_process(11);
	lrng_data_process(12);
	lrng_data_process(13);
	lrng_data_process(14);
	lrng_data_process(15);
	check_res(lrng_data[6], idx_zero_compare, lrng_data[7], idx_one_compare);
#elif (LRNG_DATA_SLOTSIZE_BITS == 8)
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
#elif (LRNG_DATA_SLOTSIZE_BITS == 16)
	lrng_data_process(0);
	lrng_data_process_u32((2 << 0) | (1 << 16));
	/*
	 * Test proper handling of ptr wrap where one slot of the following
	 * insertion goes into first slot of lrng_data[0].
	 */
	lrng_data_process_u32((0 << 0) | (3 << 16));

	/* We modify lrng_data[0] and lrng_data[1] */
	lrng_data_process_u32((2 << 0) | (1 << 16));
	lrng_data_process(3);
	check_res(lrng_data[30], idx_zero_compare, lrng_data[31], idx_one_compare);
#endif
	check_res(lrng_data[0], idx_zero_compare, lrng_data[1], idx_one_compare);

	lrng_data[0] = 0xffffffff;
	lrng_data[1] = 0xffffffff;
	lrng_data_ptr = 0;

	/* Individual writing of slot */
	for (time = 0; time < LRNG_DATA_NUM_VALUES; time++) {
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
