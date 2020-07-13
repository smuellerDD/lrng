// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG Slow Noise Source: Interrupt data collection
 *
 * Copyright (C) 2016 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <linux/random.h>

#include "lrng_internal.h"
#include "lrng_sw_noise.h"

/* Holder of time stamps before mixing them into the entropy pool */
static DEFINE_PER_CPU(u32 [LRNG_TIME_ARRAY_SIZE], lrng_time);
static DEFINE_PER_CPU(u32, lrng_time_ptr) = 0;
static DEFINE_PER_CPU(u8, lrng_time_irqs) = 0;

/*
 * Batching up of entropy in per-CPU array before injecting into entropy pool.
 */
static inline void lrng_time_process(void)
{
	u32 i, ptr, now_time = random_get_entropy();
	u32 now_time_masked = now_time & LRNG_TIME_SLOTSIZE_MASK;
	enum lrng_health_res health_test;

	/* Ensure sufficient space in lrng_time_irqs */
	BUILD_BUG_ON(LRNG_TIME_NUM_VALUES >= (1 << (sizeof(u8) << 3)));
	BUILD_BUG_ON(LRNG_TIME_ARRAY_MEMBER_BITS % LRNG_TIME_SLOTSIZE_BITS);
	/* Ensure consistency of values */
	BUILD_BUG_ON(LRNG_TIME_ARRAY_MEMBER_BITS != sizeof(lrng_time[0]) << 3);

	/* During boot time, we mix the full time stamp directly into LFSR */
	if (unlikely(!lrng_state_fully_seeded())) {
		if (lrng_raw_entropy_store(now_time))
			goto out;

		health_test = lrng_health_test(now_time);
		if (health_test > lrng_health_fail_use)
			goto out;

		lrng_pool_lfsr_u32(now_time);
		if (health_test == lrng_health_pass)
			lrng_pool_add_irq(1);
		goto out;
	}

	/* Runtime operation */
	if (lrng_raw_entropy_store(now_time_masked))
		goto out;

	health_test = lrng_health_test(now_time_masked);
	if (health_test > lrng_health_fail_use)
		goto out;

	ptr = this_cpu_inc_return(lrng_time_ptr) & LRNG_TIME_WORD_MASK;
	this_cpu_or(lrng_time[lrng_time_idx2array(ptr)],
		    lrng_time_slot_val(now_time_masked,
				       lrng_time_idx2slot(ptr)));

	/* Interrupt delivers entropy if health test passes */
	if (health_test == lrng_health_pass)
		this_cpu_inc(lrng_time_irqs);

	/* Only mix the buffer of time stamps into LFSR when wrapping */
	if (ptr < LRNG_TIME_WORD_MASK)
		goto out;

	for (i = 0; i < LRNG_TIME_ARRAY_SIZE; i++) {
		if (lrng_raw_array_entropy_store(this_cpu_read(lrng_time[i]))) {
			/*
			 * If we fed even a part of the array to external
			 * analysis, we mark that the entire array has no
			 * entropy. This is due to the non-IID property of
			 * the data as we do not fully know whether the
			 * existing dependencies diminish the entropy beyond
			 * to what we expect it has.
			 */
			this_cpu_write(lrng_time_irqs, 0);
		} else {
			lrng_pool_lfsr_u32(this_cpu_read(lrng_time[i]));
		}

		this_cpu_write(lrng_time[i], 0);
	}
	lrng_pool_add_irq(this_cpu_read(lrng_time_irqs));
	this_cpu_write(lrng_time_irqs, 0);

out:
	lrng_perf_time(now_time);
}

/*
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
