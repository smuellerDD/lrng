// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG Entropy pool management
 *
 * Copyright (C) 2016 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/irq_regs.h>
#include <linux/lrng.h>
#include <linux/percpu.h>
#include <linux/random.h>
#include <linux/utsname.h>
#include <linux/workqueue.h>

#include "lrng_internal.h"
#include "lrng_lfsr.h"

struct lrng_state {
	bool lrng_operational;		/* Is DRNG operational? */
	bool lrng_fully_seeded;		/* Is DRNG fully seeded? */
	bool lrng_min_seeded;		/* Is DRNG minimally seeded? */

	/*
	 * To ensure that external entropy providers cannot dominate the
	 * internal noise sources but yet cannot be dominated by internal
	 * noise sources, the following booleans are intended to allow
	 * external to provide seed once when a DRNG reseed occurs. This
	 * triggering of external noise source is performed even when the
	 * entropy pool has sufficient entropy.
	 */
	bool lrng_seed_hw;		/* Allow HW to provide seed */
	bool lrng_seed_user;		/* Allow user space to provide seed */

	struct work_struct lrng_seed_work;	/* (re)seed work queue */
};

static struct lrng_pool lrng_pool __aligned(LRNG_KCAPI_ALIGN) = {
	.irq_info	= {
		.irq_entropy_bits	= LRNG_IRQ_ENTROPY_BITS,
		.num_events_thresh	= ATOMIC_INIT(LRNG_INIT_ENTROPY_BITS +
						LRNG_CONDITIONING_ENTROPY_LOSS),
		/* Sample IRQ pointer data at least during boot */
		.irq_highres_timer	= false },
	.lock		= __SPIN_LOCK_UNLOCKED(lrng_pool.lock)
};

static struct lrng_state lrng_state = { false, false, false, true, true };

/********************************** Helper ***********************************/

/* External entropy provider is allowed to provide seed data */
bool lrng_state_exseed_allow(enum lrng_external_noise_source source)
{
	if (source == lrng_noise_source_hw)
		return lrng_state.lrng_seed_hw;
	return lrng_state.lrng_seed_user;
}

/* Enable / disable external entropy provider to furnish seed */
void lrng_state_exseed_set(enum lrng_external_noise_source source, bool type)
{
	if (source == lrng_noise_source_hw)
		lrng_state.lrng_seed_hw = type;
	else
		lrng_state.lrng_seed_user = type;
}

static inline void lrng_state_exseed_allow_all(void)
{
	lrng_state_exseed_set(lrng_noise_source_hw, true);
	lrng_state_exseed_set(lrng_noise_source_user, true);
}

void lrng_state_init_seed_work(void)
{
	INIT_WORK(&lrng_state.lrng_seed_work, lrng_drng_seed_work);
}

static inline u32 lrng_entropy_to_data(u32 entropy_bits)
{
	return ((entropy_bits * lrng_pool.irq_info.irq_entropy_bits) /
		LRNG_DRNG_SECURITY_STRENGTH_BITS);
}

static inline u32 lrng_data_to_entropy(u32 irqnum)
{
	return ((irqnum * LRNG_DRNG_SECURITY_STRENGTH_BITS) /
		lrng_pool.irq_info.irq_entropy_bits);
}

u32 lrng_avail_entropy(void)
{
	return min_t(u32, LRNG_POOL_SIZE_BITS, lrng_data_to_entropy(
			atomic_read_u32(&lrng_pool.irq_info.num_events)));
}

void lrng_set_entropy_thresh(u32 new)
{
	atomic_set(&lrng_pool.irq_info.num_events_thresh,
		   lrng_entropy_to_data(new));
}

/*
 * Reading of the LRNG pool is only allowed by one caller. The reading is
 * only performed to (re)seed DRNGs. Thus, if this "lock" is already taken,
 * the reseeding operation is in progress. The caller is not intended to wait
 * but continue with its other operation.
 */
int lrng_pool_trylock(void)
{
	return atomic_cmpxchg(&lrng_pool.irq_info.reseed_in_progress, 0, 1);
}

void lrng_pool_unlock(void)
{
	atomic_set(&lrng_pool.irq_info.reseed_in_progress, 0);
}

void lrng_reset_state(void)
{
	struct lrng_irq_info *irq_info = &lrng_pool.irq_info;

	atomic_set(&irq_info->num_events, 0);
	lrng_state.lrng_operational = false;
	lrng_state.lrng_fully_seeded = false;
	lrng_state.lrng_min_seeded = false;
	lrng_pool.all_online_numa_node_seeded = false;
	pr_debug("reset LRNG\n");
}

void lrng_pool_all_numa_nodes_seeded(void)
{
	lrng_pool.all_online_numa_node_seeded = true;
}

bool lrng_state_min_seeded(void)
{
	return lrng_state.lrng_min_seeded;
}

bool lrng_state_fully_seeded(void)
{
	return lrng_state.lrng_fully_seeded;
}

bool lrng_state_operational(void)
{
	return lrng_state.lrng_operational;
}

bool lrng_pool_highres_timer(void)
{
	return lrng_pool.irq_info.irq_highres_timer;
}

void lrng_pool_set_entropy(u32 entropy_bits)
{
	atomic_set(&lrng_pool.irq_info.num_events,
		   lrng_entropy_to_data(entropy_bits));
}

static void lrng_pool_configure(bool highres_timer, u32 irq_entropy_bits)
{
	struct lrng_irq_info *irq_info = &lrng_pool.irq_info;

	irq_info->irq_highres_timer = highres_timer;
	if (irq_info->irq_entropy_bits != irq_entropy_bits) {
		irq_info->irq_entropy_bits = irq_entropy_bits;
		/* Reset the threshold based on new oversampling factor. */
		lrng_set_entropy_thresh(atomic_read_u32(
						&irq_info->num_events_thresh));
	}
}

static int __init lrng_init_time_source(void)
{
	if (random_get_entropy() || random_get_entropy()) {
		/*
		 * As the highres timer is identified here, previous interrupts
		 * obtained during boot time are treated like a lowres-timer
		 * would have been present.
		 */
		lrng_pool_configure(true, LRNG_IRQ_ENTROPY_BITS);
	} else {
		lrng_health_disable();
		lrng_pool_configure(false, LRNG_IRQ_ENTROPY_BITS *
					   LRNG_IRQ_OVERSAMPLING_FACTOR);
		pr_warn("operating without high-resolution timer and applying IRQ oversampling factor %u\n",
			LRNG_IRQ_OVERSAMPLING_FACTOR);
	}

	return 0;
}

core_initcall(lrng_init_time_source);

/* invoke function with buffer aligned to 4 bytes */
void lrng_pool_lfsr(const u8 *buf, u32 buflen)
{
	u32 *p_buf = (u32 *)buf;

	for (; buflen >= 4; buflen -= 4)
		lrng_pool_lfsr_u32(*p_buf++);

	buf = (u8 *)p_buf;
	while (buflen--)
		lrng_pool_lfsr_u32(*buf++);
}

void lrng_pool_lfsr_nonaligned(const u8 *buf, u32 buflen)
{
	while (buflen) {
		if (!((unsigned long)buf & (sizeof(u32) - 1))) {
			lrng_pool_lfsr(buf, buflen);
			return;
		}

		lrng_pool_lfsr_u32(*buf++);
		buflen--;
	}
}

/**************************** Interrupt processing ****************************/

/*
 * Hot code path - inject data into entropy pool using LFSR
 */
void lrng_pool_lfsr_u32(u32 value)
{
	_lrng_pool_lfsr_u32(&lrng_pool, value);
}

/*
 * Hot code path - mix data into entropy pool
 */
void lrng_pool_add_irq(u32 irq_num)
{
	struct lrng_irq_info *irq_info = &lrng_pool.irq_info;

	atomic_add(irq_num, &irq_info->num_events);

	/*
	 * Once all DRNGs are fully seeded, the interrupt noise
	 * sources will not trigger any reseeding any more.
	 */
	if (likely(lrng_pool.all_online_numa_node_seeded))
		return;

	/* Only try to reseed if the DRNG is alive. */
	if (!lrng_get_available())
		return;

	/* Only trigger the DRNG reseed if we have collected enough IRQs. */
	if (atomic_read_u32(&lrng_pool.irq_info.num_events) <
	    atomic_read_u32(&lrng_pool.irq_info.num_events_thresh))
		return;

	/* Ensure that the seeding only occurs once at any given time. */
	if (lrng_pool_trylock())
		return;

	/* Seed the DRNG with IRQ noise. */
	schedule_work(&lrng_state.lrng_seed_work);
}

void lrng_pool_add_entropy(u32 entropy_bits)
{
	lrng_pool_add_irq(lrng_entropy_to_data(entropy_bits));
}

/*
 * Generate a hashed output of pool using the SP800-90A section 10.3.1 hash_df
 * function
 */
u32 __lrng_pool_hash_df(const struct lrng_crypto_cb *crypto_cb, void *hash,
			struct lrng_pool *pool, u8 *outbuf, u32 requested_bits)
{
	u32 digestsize, requested_bytes = requested_bits >> 3,
	    generated_bytes = 0;
	u8 digest[64] __aligned(LRNG_KCAPI_ALIGN);

	digestsize = crypto_cb->lrng_hash_digestsize(hash);
	if (digestsize > sizeof(digest)) {
		pr_err("Digest buffer too small\n");
		return 0;
	}

	pool->counter = 1;
	pool->requested_bits = cpu_to_be32(requested_bytes << 3);

	while (requested_bytes) {
		u32 tocopy = min_t(u32, requested_bytes, digestsize);

		/* The counter must not wrap */
		if (pool->counter == 0)
			goto out;

		if (crypto_cb->lrng_hash_buffer(hash, (u8 *)pool,
						LRNG_POOL_SIZE_BYTES + 64,
						digest))
			goto out;

		/* Copy the data out to the caller */
		memcpy(outbuf + generated_bytes, digest, tocopy);
		requested_bytes -= tocopy;
		generated_bytes += tocopy;
		pool->counter++;
	}

out:
	/* Mix read data back into pool for backtracking resistance */
	if (generated_bytes)
		lrng_pool_lfsr(outbuf, generated_bytes);
	memzero_explicit(digest, digestsize);
	return (generated_bytes<<3);
}

static inline u32 lrng_pool_hash_df(const struct lrng_crypto_cb *crypto_cb,
				    void *hash, u8 *outbuf, u32 requested_bits)
{
	return __lrng_pool_hash_df(crypto_cb, hash, &lrng_pool, outbuf,
				   requested_bits);
}

/**
 * lrng_get_pool() - Read the entropy pool out for use.
 *
 * This function handles the translation from the number of received interrupts
 * into an entropy statement. The conversion depends on LRNG_IRQ_ENTROPY_BITS
 * which defines how many interrupts must be received to obtain 256 bits of
 * entropy. With this value, the function lrng_data_to_entropy converts a given
 * data size (received interrupts, requested amount of data, etc.) into an
 * entropy statement. lrng_entropy_to_data does the reverse.
 *
 * Both functions are agnostic about the type of data: when the number of
 * interrupts is processed by these functions, the resulting entropy value is in
 * bits as we assume the entropy of interrupts is measured in bits. When data is
 * processed, the entropy value is in bytes as the data is measured in bytes.
 *
 * @outbuf: buffer to store data in with size LRNG_DRNG_SECURITY_STRENGTH_BYTES
 * @requested_entropy_bits: requested bits of entropy -- the function will
 *			    return at least this amount of entropy if available
 * @entropy_retain: amount of entropy in bits that should be left in the pool
 *
 * Return: estimated entropy from the IRQs that was obtained
 */
static u32 lrng_get_pool(const struct lrng_crypto_cb *crypto_cb, void *hash,
			 u8 *outbuf, u32 requested_entropy_bits,
			 u32 entropy_retain)
{
	struct lrng_pool *pool = &lrng_pool;
	struct lrng_state *state = &lrng_state;
	struct lrng_irq_info *irq_info = &pool->irq_info;
	unsigned long flags;

	u32 irq_num_events_used, irq_num_events, avail_entropy_bits;

	/* This get_pool operation must only be called once at a given time! */
	spin_lock_irqsave(&pool->lock, flags);

	/* How many unused interrupts are in entropy pool? */
	irq_num_events = atomic_read_u32(&irq_info->num_events);
	/* Convert available interrupts into entropy statement */
	avail_entropy_bits = lrng_data_to_entropy(irq_num_events);

	/* Cap available entropy to pool size */
	avail_entropy_bits =
			min_t(u32, avail_entropy_bits, LRNG_POOL_SIZE_BITS);

	/* How much entropy we need to and can we use? */
	if (unlikely(!state->lrng_fully_seeded)) {
		/*
		 * During boot time, we read 256 bits data with
		 * avail_entropy_bits entropy. In case our conservative
		 * entropy estimate underestimates the available entropy
		 * we can transport as much available entropy as
		 * possible. The entropy pool does not operate compliant to
		 * the German AIS 21/31 NTG.1 yet.
		 */
		requested_entropy_bits =
				LRNG_DRNG_SECURITY_STRENGTH_BITS;
	} else {
		/* Provide all entropy above retaining level */
		if (avail_entropy_bits < entropy_retain) {
			requested_entropy_bits = 0;
			goto out;
		}
		avail_entropy_bits -= entropy_retain;
		requested_entropy_bits = min_t(u32, avail_entropy_bits,
					       requested_entropy_bits);
	}

	/* Hash is a compression function: we generate entropy amount of data */
	requested_entropy_bits = round_down(requested_entropy_bits, 8);

	requested_entropy_bits = lrng_pool_hash_df(crypto_cb, hash, outbuf,
						   requested_entropy_bits);

	/* Boot time: After getting the full buffer adjust the entropy value. */
	requested_entropy_bits = min_t(u32, avail_entropy_bits,
				       requested_entropy_bits);

out:
	/* Convert used entropy into interrupt number for subtraction */
	irq_num_events_used = lrng_entropy_to_data(requested_entropy_bits);

	/*
	 * The hash_df operation entropy assessment shows that the output
	 * entropy is one bit smaller than the input entropy. Therefore we
	 * account for this one bit of entropy here: if we have sufficient
	 * entropy in the LFSR, we say we used one bit of entropy more.
	 * Otherwise we reduce the amount of entropy we say we generated with
	 * the hash_df.
	 */
	if (irq_num_events_used) {
		if ((irq_num_events_used + LRNG_CONDITIONING_ENTROPY_LOSS) <=
		    lrng_entropy_to_data(avail_entropy_bits)) {
			irq_num_events_used += LRNG_CONDITIONING_ENTROPY_LOSS;
		} else {
			if (unlikely(requested_entropy_bits <
				     LRNG_CONDITIONING_ENTROPY_LOSS))
				requested_entropy_bits = 0;
			else
				requested_entropy_bits -=
						LRNG_CONDITIONING_ENTROPY_LOSS;
		}
	}

	/*
	 * New events might have arrived in the meanwhile and we don't
	 * want to throw them away unconditionally. On the other hand,
	 * these new events might have been mixed in before
	 * lrng_hash_df_pool() had been able to draw any entropy
	 * from the pool and thus, the pool capacity might have been
	 * exceeded at some point. Note that in theory, some events
	 * might get lost inbetween the atomic_read() and
	 * atomic_set() below. But that's fine, because it's no real
	 * concern while code preventing this would come at the cost of
	 * additional complexity. Likewise, some events which arrived
	 * after full or partial completion of the __lrng_hash_df_pool()
	 * above might get unnecessarily thrown away by the min()
	 * operation below; the same argument applies there.
	 */
	irq_num_events = atomic_read_u32(&irq_info->num_events);
	irq_num_events = min_t(u32, irq_num_events,
			       lrng_entropy_to_data(LRNG_POOL_SIZE_BITS));
	irq_num_events -= irq_num_events_used;
	atomic_set(&irq_info->num_events, irq_num_events);

	spin_unlock_irqrestore(&pool->lock, flags);

	/* Obtain entropy statement in bits from the used entropy */
	pr_debug("obtained %u bits of entropy from %u newly collected interrupts - not using %u interrupts\n",
		 requested_entropy_bits, irq_num_events_used,
		 irq_num_events);

	return requested_entropy_bits;
}

/* Fill the seed buffer with data from the noise sources */
int lrng_fill_seed_buffer(const struct lrng_crypto_cb *crypto_cb, void *hash,
			  struct entropy_buf *entropy_buf, u32 entropy_retain)
{
	struct lrng_state *state = &lrng_state;
	u32 total_entropy_bits = 0;

	/* Require at least 128 bits of entropy for any reseed. */
	if (state->lrng_fully_seeded &&
	    (lrng_avail_entropy() <
	     lrng_slow_noise_req_entropy(LRNG_MIN_SEED_ENTROPY_BITS +
					 LRNG_CONDITIONING_ENTROPY_LOSS) +
	      entropy_retain))
		goto wakeup;

	/*
	 * Concatenate the output of the noise sources. This would be the
	 * spot to add an entropy extractor logic if desired. Note, this
	 * has the ability to collect entropy equal or larger than the DRNG
	 * strength.
	 */
	total_entropy_bits = lrng_get_pool(crypto_cb, hash, entropy_buf->a,
					   LRNG_DRNG_SECURITY_STRENGTH_BITS,
					   entropy_retain);
	total_entropy_bits += lrng_get_arch(entropy_buf->b);
	total_entropy_bits += lrng_get_jent(entropy_buf->c,
					    LRNG_DRNG_SECURITY_STRENGTH_BYTES);

	/* also reseed the DRNG with the current time stamp */
	entropy_buf->now = random_get_entropy();

	/* allow external entropy provider to provide seed */
	lrng_state_exseed_allow_all();

wakeup:
	/*
	 * Shall we wake up user space writers? This location covers
	 * ensures that the user space provider does not dominate the internal
	 * noise sources since in case the first call of this function finds
	 * sufficient entropy in the entropy pool, it will not trigger the
	 * wakeup. This implies that when the next /dev/urandom read happens,
	 * the entropy pool is drained.
	 */
	lrng_writer_wakeup();

	return total_entropy_bits;
}

/**
 * lrng_init_ops() - Set seed stages of LRNG
 *
 * Set the slow noise source reseed trigger threshold. The initial threshold
 * is set to the minimum data size that can be read from the pool: a word. Upon
 * reaching this value, the next seed threshold of 128 bits is set followed
 * by 256 bits.
 *
 * @entropy_bits: size of entropy currently injected into DRNG
 */
void lrng_init_ops(u32 seed_bits)
{
	struct lrng_state *state = &lrng_state;

	if (state->lrng_operational)
		return;

	/* DRNG is seeded with full security strength */
	if (state->lrng_fully_seeded) {
		state->lrng_operational = lrng_sp80090b_startup_complete();
		lrng_process_ready_list();
		lrng_init_wakeup();
	} else if (seed_bits >= LRNG_FULL_SEED_ENTROPY_BITS) {
		invalidate_batched_entropy();
		state->lrng_fully_seeded = true;
		state->lrng_operational = lrng_sp80090b_startup_complete();
		state->lrng_min_seeded = true;
		pr_info("LRNG fully seeded with %u bits of entropy\n",
			seed_bits);
		lrng_set_entropy_thresh(LRNG_FULL_SEED_ENTROPY_BITS +
					LRNG_CONDITIONING_ENTROPY_LOSS);
		lrng_process_ready_list();
		lrng_init_wakeup();

	} else if (!state->lrng_min_seeded) {

		/* DRNG is seeded with at least 128 bits of entropy */
		if (seed_bits >= LRNG_MIN_SEED_ENTROPY_BITS) {
			invalidate_batched_entropy();
			state->lrng_min_seeded = true;
			pr_info("LRNG minimally seeded with %u bits of entropy\n",
				seed_bits);
			lrng_set_entropy_thresh(
				lrng_slow_noise_req_entropy(
					LRNG_FULL_SEED_ENTROPY_BITS +
					LRNG_CONDITIONING_ENTROPY_LOSS));
			lrng_process_ready_list();
			lrng_init_wakeup();

		/* DRNG is seeded with at least LRNG_INIT_ENTROPY_BITS bits */
		} else if (seed_bits >= LRNG_INIT_ENTROPY_BITS) {
			pr_info("LRNG initial entropy level %u bits of entropy\n",
				seed_bits);
			lrng_set_entropy_thresh(
				lrng_slow_noise_req_entropy(
					LRNG_MIN_SEED_ENTROPY_BITS +
					LRNG_CONDITIONING_ENTROPY_LOSS));
		}
	}
}

int __init rand_initialize(void)
{
	ktime_t now_time = ktime_get_real();
	unsigned long rand;
	unsigned int i;

	lrng_drng_init_early();

	lrng_pool_lfsr_u32(now_time);
	for (i = 0; i < LRNG_POOL_SIZE; i++) {
		if (!arch_get_random_seed_long_early(&rand) &&
		    !arch_get_random_long_early(&rand))
			rand = random_get_entropy();
		lrng_pool_lfsr((u8 *)&rand, sizeof(rand));
	}
	lrng_pool_lfsr_nonaligned((u8 *)utsname(), sizeof(*(utsname())));

	return 0;
}
