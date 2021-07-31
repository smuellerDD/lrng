// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG greatest common divisor (GCD) calculation of time stamps
 *
 * This code calculates the GCD for the time stamps handed in. The idea is that
 * the GCD value is to be applied to the time stamp considering that only
 * the 8 LSB of the time stamp is used. Thus, static lower bits should be
 * removed to ensure the LRNG uses bits that are moving.
 *
 * Copyright (C) 2021, Stephan Mueller <smueller@chronox.de>
 * Copyright (C) 2021, Joshua E. Hill <josh@keypair.us>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "lrng_internal.h"

/* Number of time stamps analyzed to calculate a GCD */
#define LRNG_GCD_WINDOW_SIZE	100
static u32 lrng_gcd_history[LRNG_GCD_WINDOW_SIZE];
static atomic_t lrng_gcd_history_ptr = ATOMIC_INIT(-1);

/* A straight forward implementation of the Euclidean algorithm for GCD. */
static inline u32 lrng_gcd32(u32 a, u32 b)
{
	/* Make a greater a than or equal b. */
	if (a < b) {
		u32 c = a;

		a = b;
		b = c;
	}

	/* Now perform the standard inner-loop for this algorithm.*/
	while (b != 0) {
		u32 r;

		r = a % b;
		a = b;
		b = r;
	}

	return a;
}

u32 lrng_gcd_analyze(u32 *history, size_t nelem)
{
	u32 running_gcd = 0;
	size_t i;

	/* Now perform the analysis on the accumulated time data. */
	for (i = 0; i < nelem; i++) {
		/*
		 * NOTE: this would be the place to add more analysis on the
		 * appropriateness of the timer like checking the presence
		 * of sufficient variations in the timer.
		 */

		/*
		 * This calculates the gcd of all the time values. that is
		 * gcd(time_1, time_2, ..., time_nelem)
		 *
		 * Some timers increment by a fixed (non-1) amount each step.
		 * This code checks for such increments, and allows the library
		 * to output the number of such changes have occurred.
		 */
		running_gcd = lrng_gcd32(history[i], running_gcd);

		/* Zeroize data */
		history[i] = 0;
	}

	return running_gcd;
}

void jent_gcd_add_value(u32 time)
{
	u32 ptr = (u32)atomic_inc_return_relaxed(&lrng_gcd_history_ptr);

	if (ptr < LRNG_GCD_WINDOW_SIZE) {
		lrng_gcd_history[ptr] = time;
	} else if (ptr == LRNG_GCD_WINDOW_SIZE) {
		u32 gcd = lrng_gcd_analyze(lrng_gcd_history,
					   LRNG_GCD_WINDOW_SIZE);

		/*
		 * Ensure that we have variations in the time stamp below the
		 * given value. This is just a safety measure to prevent the GCD
		 * becoming too large.
		 */
		if (gcd >= 1000) {
			pr_warn("calculated GCD is larger than expected: %u\n",
				gcd);
			gcd = 1000;
		}

		/*  Adjust all deltas by the observed (small) common factor. */
		lrng_gcd_set(gcd);
		atomic_set(&lrng_gcd_history_ptr, 0);
	}
}
