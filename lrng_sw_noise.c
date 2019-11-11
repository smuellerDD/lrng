// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG Slow Noise Source: Interrupt data collection
 *
 * Copyright (C) 2016 - 2019, Stephan Mueller <smueller@chronox.de>
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

#include <asm/irq_regs.h>
#include <linux/random.h>

#include "lrng_internal.h"

/*
 * To limit the impact on the interrupt handling, the LRNG concatenates
 * entropic LSB parts of the time stamps in a per-CPU array and only
 * injects them into the entropy pool when the array is full.
 */
/* Number of time values to store in the array */
#define LRNG_TIME_NUM_VALUES		(64)
/* Mask of LSB of time stamp to store */
#define LRNG_TIME_WORD_MASK		(LRNG_TIME_NUM_VALUES - 1)

/* Store multiple integers in one u32 */
#define LRNG_TIME_SLOTSIZE_BITS		(8)
#define LRNG_TIME_SLOTSIZE_MASK		((1 << LRNG_TIME_SLOTSIZE_BITS) - 1)
#define LRNG_TIME_ARRAY_MEMBER_BITS	(sizeof(u32) << 3)
#define LRNG_TIME_SLOTS_PER_UINT	(LRNG_TIME_ARRAY_MEMBER_BITS / \
					 LRNG_TIME_SLOTSIZE_BITS)
#define LRNG_TIME_SLOTS_MASK		(LRNG_TIME_SLOTS_PER_UINT - 1)
#define LRNG_TIME_ARRAY_SIZE		(LRNG_TIME_NUM_VALUES /	\
					 LRNG_TIME_SLOTS_PER_UINT)

/* Holder of time stamps before mixing them into the entropy pool */
static DEFINE_PER_CPU(u32 [LRNG_TIME_ARRAY_SIZE], lrng_time);
static DEFINE_PER_CPU(u32, lrng_time_ptr) = 0;
static DEFINE_PER_CPU(u8, lrng_time_irqs) = 0;

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
	return idx & LRNG_TIME_SLOTS_MASK;
}

/* Convert value into slot value */
static inline unsigned int lrng_time_slot_val(unsigned int val,
					      unsigned int slot)
{
	return val << lrng_time_slot2bitindex(slot);
}

/**
 * Batching up of entropy in per-CPU array before injecting into entropy pool.
 */
static inline void lrng_time_process(void)
{
	u32 i, ptr, now_time = random_get_entropy() &
			       (likely(lrng_state_fully_seeded()) ?
				LRNG_TIME_SLOTSIZE_MASK : (u32)-1);
	enum lrng_health_res health_test;

	/* Ensure sufficient space in lrng_time_irqs */
	BUILD_BUG_ON(LRNG_TIME_NUM_VALUES >= (1 << (sizeof(u8) << 3)));
	BUILD_BUG_ON(LRNG_TIME_ARRAY_MEMBER_BITS % LRNG_TIME_SLOTSIZE_BITS);

	if (lrng_raw_entropy_store(now_time))
		return;

	health_test = lrng_health_test(now_time);
	if (health_test > lrng_health_fail_use)
		return;

	/* During boot time, we mix the full time stamp directly into LFSR */
	if (unlikely(!lrng_state_fully_seeded())) {
		lrng_pool_lfsr_u32(now_time);
		if (health_test == lrng_health_pass)
			lrng_pool_add_irq(1);
		return;
	}

	ptr = this_cpu_inc_return(lrng_time_ptr) & LRNG_TIME_WORD_MASK;
	this_cpu_or(lrng_time[lrng_time_idx2array(ptr)],
		    lrng_time_slot_val(now_time & LRNG_TIME_SLOTSIZE_MASK,
				       lrng_time_idx2slot(ptr)));

	/* Interrupt delivers entropy if health test passes */
	if (health_test == lrng_health_pass)
		this_cpu_inc(lrng_time_irqs);

	/* Only mix the buffer of time stamps into LFSR when wrapping */
	if (ptr < LRNG_TIME_WORD_MASK)
		return;

	for (i = 0; i < LRNG_TIME_ARRAY_SIZE; i++) {
		lrng_pool_lfsr_u32(this_cpu_read(lrng_time[i]));
		this_cpu_write(lrng_time[i], 0);
	}
	lrng_pool_add_irq(this_cpu_read(lrng_time_irqs));
	this_cpu_write(lrng_time_irqs, 0);
}

/**
 * Hot code path - Callback for interrupt handler
 */
void add_interrupt_randomness(int irq, int irq_flags)
{
	lrng_time_process();

	if (!lrng_pool_highres_timer()) {
		struct pt_regs *regs = get_irq_regs();
		static atomic_t reg_idx = ATOMIC_INIT(0);
		u64 ip;

		lrng_pool_lfsr_u32(jiffies);
		lrng_pool_lfsr_u32(irq);
		lrng_pool_lfsr_u32(irq_flags);

		if (regs) {
			u32 *ptr = (u32 *)regs;
			int reg_ptr = atomic_add_return_relaxed(1, &reg_idx);
			size_t n = (sizeof(struct pt_regs) / sizeof(u32));

			ip = instruction_pointer(regs);
			lrng_pool_lfsr_u32(*(ptr + (reg_ptr % n)));
		} else
			ip = _RET_IP_;

		lrng_pool_lfsr_u32(ip >> 32);
		lrng_pool_lfsr_u32(ip);
	}
}
EXPORT_SYMBOL(add_interrupt_randomness);
