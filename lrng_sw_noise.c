// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * LRNG Slow Noise Source: Interrupt data collection and random data generation
 *
 * Copyright (C) 2016 - 2021, Stephan Mueller <smueller@chronox.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm/irq_regs.h>
#include <asm/ptrace.h>
#include <crypto/hash.h>
#include <linux/lrng.h>
#include <linux/random.h>

#include "lrng_internal.h"
#include "lrng_sw_noise.h"

/* Per-CPU array holding concatenated entropy events */
static DEFINE_PER_CPU(u32 [LRNG_DATA_ARRAY_SIZE], lrng_pcpu_array)
						__aligned(LRNG_KCAPI_ALIGN);
static DEFINE_PER_CPU(u32, lrng_pcpu_array_ptr) = 0;
static DEFINE_PER_CPU(atomic_t, lrng_pcpu_array_irqs) = ATOMIC_INIT(0);

/*
 * The entropy collection is performed by executing the following steps:
 * 1. fill up the per-CPU array holding the time stamps
 * 2. once the per-CPU array is full, a compression of the data into
 *    the auxiliary pool is performed - this happens in interrupt context
 *
 * If step 2 is not desired in interrupt context, the following boolean
 * needs to be set to false. This implies that old entropy data in the
 * per-CPU array collected since the last DRNG reseed is overwritten with
 * new entropy data instead of retaining the entropy with the compression
 * operation.
 *
 * Impact on entropy:
 *
 * If continuous compression is enabled, the maximum entropy that is collected
 * per CPU between DRNG reseeds is equal to the digest size of the used hash.
 *
 * If continuous compression is disabled, the maximum number of entropy events
 * that can be collected per CPU is equal to LRNG_DATA_ARRAY_SIZE. This amount
 * of events is converted into an entropy statement which then represents the
 * maximum amount of entropy collectible per CPU between DRNG reseeds.
 */
static bool lrng_pcpu_continuous_compression __read_mostly =
			IS_ENABLED(CONFIG_LRNG_ENABLE_CONTINUOUS_COMPRESSION);

#ifdef CONFIG_LRNG_SWITCHABLE_CONTINUOUS_COMPRESSION
module_param(lrng_pcpu_continuous_compression, bool, 0444);
MODULE_PARM_DESC(lrng_pcpu_continuous_compression,
		 "Perform entropy compression if per-CPU entropy data array is full\n");
#endif

/*
 * Per-CPU entropy pool with compressed entropy event
 *
 * The per-CPU entropy pool is defined as the hash state. New data is simply
 * inserted into the entropy pool by performing a hash update operation.
 * To read the entropy pool, a hash final must be invoked. However, before
 * the entropy pool is released again after a hash final, the hash init must
 * be performed.
 *
 * This definition must provide a buffer that is equal to SHASH_DESC_ON_STACK
 * as it will be casted into a struct shash_desc.
 */
#define LRNG_PCPU_POOL_SIZE	(sizeof(struct shash_desc) + HASH_MAX_DESCSIZE)
static DEFINE_PER_CPU(u8 [LRNG_PCPU_POOL_SIZE], lrng_pcpu_pool)
						__aligned(LRNG_KCAPI_ALIGN);
/*
 * Lock to allow other CPUs to read the pool - as this is only done during
 * reseed which is infrequent, this lock is hardly contended.
 */
static DEFINE_PER_CPU(spinlock_t, lrng_pcpu_lock);
static DEFINE_PER_CPU(bool, lrng_pcpu_lock_init) = false;

static inline bool lrng_pcpu_pool_online(int cpu)
{
	return per_cpu(lrng_pcpu_lock_init, cpu);
}

bool lrng_pcpu_continuous_compression_state(void)
{
	return lrng_pcpu_continuous_compression;
}

void lrng_pcpu_check_compression_state(void)
{
	/* One pool must hold sufficient entropy for disabled compression */
	if (!lrng_pcpu_continuous_compression) {
		u32 max_ent = min_t(u32, lrng_get_digestsize(),
				    lrng_data_to_entropy(LRNG_DATA_NUM_VALUES));
		if (max_ent < LRNG_DRNG_SECURITY_STRENGTH_BITS) {
			pr_warn("Force continuous compression operation to ensure LRNG can hold enough entropy\n");
			lrng_pcpu_continuous_compression = true;
		}
	}
}

/*
 * Reset all per-CPU pools - reset entropy estimator but leave the pool data
 * that may or may not have entropy unchanged.
 */
void lrng_pcpu_reset(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		atomic_set(per_cpu_ptr(&lrng_pcpu_array_irqs, cpu), 0);
}

u32 lrng_pcpu_avail_pool_size(void)
{
	u32 max_size = 0, max_pool = lrng_get_digestsize();
	int cpu;

	if (!lrng_pcpu_continuous_compression)
		max_pool = min_t(u32, max_pool, LRNG_DATA_NUM_VALUES);

	for_each_online_cpu(cpu) {
		if (lrng_pcpu_pool_online(cpu))
			max_size += max_pool;
	}

	return max_size;
}

/* Return number of unused IRQs present in all per-CPU pools. */
u32 lrng_pcpu_avail_irqs(void)
{
	u32 digestsize_irqs, irq = 0;
	int cpu;

	/* Obtain the cap of maximum numbers of IRQs we count */
	digestsize_irqs = lrng_entropy_to_data(lrng_get_digestsize());
	if (!lrng_pcpu_continuous_compression) {
		/* Cap to max. number of IRQs the array can hold */
		digestsize_irqs = min_t(u32, digestsize_irqs,
					LRNG_DATA_NUM_VALUES);
	}

	for_each_online_cpu(cpu) {
		if (!lrng_pcpu_pool_online(cpu))
			continue;
		irq += min_t(u32, digestsize_irqs,
			     atomic_read_u32(per_cpu_ptr(&lrng_pcpu_array_irqs,
							 cpu)));
	}

	return irq;
}

/**
 * Trigger a switch of the hash implementation for the per-CPU pool.
 *
 * For each per-CPU pool, obtain the message digest with the old hash
 * implementation, initialize the per-CPU pool again with the new hash
 * implementation and inject the message digest into the new state.
 *
 * Assumption: the caller must guarantee that the new_cb is available during the
 * entire operation (e.g. it must hold the lock against pointer updating).
 */
int lrng_pcpu_switch_hash(int node,
			  const struct lrng_crypto_cb *new_cb, void *new_hash,
			  const struct lrng_crypto_cb *old_cb)
{
	u8 digest[LRNG_MAX_DIGESTSIZE];
	int ret, cpu;

	for_each_online_cpu(cpu) {
		struct shash_desc *pcpu_shash;
		unsigned long flags;
		spinlock_t *lock;

		/*
		 * Only switch the per-CPU pools for the current node because
		 * the crypto_cb only applies NUMA-node-wide.
		 */
		if (cpu_to_node(cpu) != node || !lrng_pcpu_pool_online(cpu))
			continue;

		pcpu_shash = (struct shash_desc *)per_cpu_ptr(lrng_pcpu_pool,
							      cpu);
		if (pcpu_shash->tfm == new_hash)
			continue;

		lock = per_cpu_ptr(&lrng_pcpu_lock, cpu);

		spin_lock_irqsave(lock, flags);
		/* Get the per-CPU pool hash with old digest ... */
		ret = old_cb->lrng_hash_final(pcpu_shash, digest) ?:
		      /* ... re-initialize the hash with the new digest ... */
		      new_cb->lrng_hash_init(pcpu_shash, new_hash) ?:
		      /*
		       * ... feed the old hash into the new state. We may feed
		       * uninitialized memory into the new state, but this is
		       * considered no issue and even good as we have some more
		       * uncertainty here.
		       */
		      new_cb->lrng_hash_update(pcpu_shash, digest,
					       sizeof(digest));
		spin_unlock_irqrestore(lock, flags);
		if (ret)
			goto out;

		pr_debug("Re-initialize per-CPU entropy pool for CPU %d on NUMA node %d with hash %s\n",
			 cpu, node, new_cb->lrng_hash_name());
	}

out:
	memzero_explicit(digest, sizeof(digest));
	return ret;
}

/*
 * When reading the per-CPU message digest, make sure we use the crypto
 * callbacks defined for the NUMA node the per-CPU pool is defined for because
 * the LRNG crypto switch support is only atomic per NUMA node.
 */
static inline u32
lrng_pcpu_pool_hash_one(struct lrng_drng *drng, int cpu, u8 *digest,
			struct shash_desc *aux_shash)
{
	const struct lrng_crypto_cb *pcpu_crypto_cb, *aux_crypto_cb;
	struct shash_desc *pcpu_shash =
		(struct shash_desc *)per_cpu_ptr(lrng_pcpu_pool, cpu);
	struct lrng_drng **lrng_drng = lrng_drng_instances();
	struct lrng_drng *pcpu_drng = drng;
	spinlock_t *lock = per_cpu_ptr(&lrng_pcpu_lock, cpu);
	unsigned long flags, flags2;
	u32 digestsize, digestsize_irqs, found_irqs;
	int node = cpu_to_node(cpu);
	void *pcpu_hash;

	/* Get the DRNG definition used for the per-CPU hash */
	if (lrng_drng && lrng_drng[node])
		pcpu_drng = lrng_drng[node];

	/*
	 * Lock guarding replacement of per-CPU hash - lock for hash
	 * implementation referenced by drng is already taken.
	 */
	if (pcpu_drng != drng)
		read_lock_irqsave(&pcpu_drng->hash_lock, flags);
	else
		__acquire(&pcpu_drng->hash_lock);

	/* Lock guarding against reading / writing to per-CPU pool */
	spin_lock_irqsave(lock, flags2);

	aux_crypto_cb = drng->crypto_cb;
	pcpu_crypto_cb = pcpu_drng->crypto_cb;
	pcpu_hash = pcpu_drng->hash;
	digestsize = pcpu_crypto_cb->lrng_hash_digestsize(pcpu_hash);
	digestsize_irqs = lrng_entropy_to_data(digestsize << 3);

	/* Obtain entropy statement like for the aux pool */
	found_irqs = atomic_xchg_relaxed(
				per_cpu_ptr(&lrng_pcpu_array_irqs, cpu), 0);
	/* Cap to maximum amount of data we can hold in hash */
	found_irqs = min_t(u32, found_irqs, digestsize_irqs);

	/* Cap to maximum amount of data we can hold in array */
	if (!lrng_pcpu_continuous_compression)
		found_irqs = min_t(u32, found_irqs, LRNG_DATA_NUM_VALUES);

	/* Store all not-yet compressed data in data array into hash */
	if (pcpu_crypto_cb->lrng_hash_update(pcpu_shash,
				(u8 *)per_cpu_ptr(lrng_pcpu_array, cpu),
				LRNG_DATA_ARRAY_SIZE * sizeof(u32)) ?:
	    /* Get the per-CPU pool digest, ... */
	    pcpu_crypto_cb->lrng_hash_final(pcpu_shash, digest) ?:
	    /* ... re-initialize the hash, ... */
	    pcpu_crypto_cb->lrng_hash_init(pcpu_shash, pcpu_hash) ?:
	    /* ... feed the old hash into the new state, ... */
	    pcpu_crypto_cb->lrng_hash_update(pcpu_shash, digest, digestsize) ?:
	    /* ... feed the old hash into the aux pool. */
	    aux_crypto_cb->lrng_hash_update(aux_shash, digest, digestsize))
		found_irqs = 0;

	spin_unlock_irqrestore(lock, flags2);
	if (pcpu_drng != drng)
		read_unlock_irqrestore(&pcpu_drng->hash_lock, flags);
	else
		__release(&pcpu_drng->hash_lock);

	return found_irqs;
}

/**
 * Hash all per-CPU pools and the auxiliary pool to form a new auxiliary pool
 * state. The message digest is at the same time the new state of the aux pool
 * to ensure backtracking resistance and the seed data used for seeding a DRNG.
 * The function will only copy as much data as entropy is available into the
 * caller-provided output buffer.
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
 * @pool: global entropy pool holding the aux pool
 * @outbuf: buffer to store data in with size LRNG_DRNG_SECURITY_STRENGTH_BYTES
 * @requested_bits: amount of data to be generated
 * @fully_seeded: indicator whether LRNG is fully seeded
 * @return: amount of collected entropy in bits.
 */
u32 lrng_pcpu_pool_hash(struct lrng_pool *pool,
			u8 *outbuf, u32 requested_bits, bool fully_seeded)
{
	SHASH_DESC_ON_STACK(shash, NULL);
	const struct lrng_crypto_cb *crypto_cb;
	struct lrng_drng *drng = lrng_drng_init_instance();
	u8 digest[LRNG_MAX_DIGESTSIZE];
	unsigned long flags, flags2;
	u32 digestsize_bits, found_ent_bits, found_irqs, unused_bits = 0,
	    collected_ent_bits = 0, collected_irqs = 0, requested_irqs;
	int ret, cpu;
	void *hash;

	/* Lock guarding replacement of per-CPU hash */
	read_lock_irqsave(&drng->hash_lock, flags);
	/* We operate on the non-atomic part of the aux pool */
	spin_lock_irqsave(&pool->lock, flags2);

	crypto_cb = drng->crypto_cb;
	hash = drng->hash;
	digestsize_bits = crypto_cb->lrng_hash_digestsize(hash) << 3;

	/* Harvest entropy from aux pool */
	ret = crypto_cb->lrng_hash_init(shash, hash) ?:
	      crypto_cb->lrng_hash_update(shash, (u8 *)pool, sizeof(*pool));
	if (ret)
		goto err;

	/* Deduct entropy counter from aux pool */
	found_ent_bits = atomic_xchg_relaxed(&pool->aux_entropy_bits, 0);
	/* Cap entropy by security strength of used digest */
	found_ent_bits = min_t(u32, digestsize_bits, found_ent_bits);

	/* We collected that amount of entropy */
	collected_ent_bits += found_ent_bits;
	/* We collected too much entropy and put the overflow back */
	if (collected_ent_bits > requested_bits) {
		/* Amount of bits we collected too much */
		unused_bits = collected_ent_bits - requested_bits;

		/* Store that for logging */
		found_ent_bits -= unused_bits;
		/* Put entropy back */
		atomic_add(found_ent_bits, &pool->aux_entropy_bits);
		/* Fix collected entropy */
		collected_ent_bits = requested_bits;
	}
	pr_debug("%u bits of entropy used from aux pool, %u bits of entropy remaining\n",
		 found_ent_bits, unused_bits);

	requested_irqs = lrng_entropy_to_data(requested_bits -
					      collected_ent_bits);

	/*
	 * Harvest entropy from each per-CPU hash state - even though we may
	 * have collected sufficient entropy, we will hash all per-CPU pools.
	 */
	for_each_online_cpu(cpu) {
		u32 pcpu_unused_irqs = 0;

		/* If pool is not online, then no entropy is present. */
		if (!lrng_pcpu_pool_online(cpu))
			continue;

		found_irqs = lrng_pcpu_pool_hash_one(drng, cpu, digest, shash);

		collected_irqs += found_irqs;
		if (collected_irqs > requested_irqs) {
			pcpu_unused_irqs = collected_irqs - requested_irqs;
			atomic_add_return_relaxed(pcpu_unused_irqs,
				per_cpu_ptr(&lrng_pcpu_array_irqs, cpu));
			collected_irqs = requested_irqs;
		}
		pr_debug("%u interrupts used from entropy pool of CPU %d, %u interrupts remain unused\n",
			 found_irqs - pcpu_unused_irqs, cpu, pcpu_unused_irqs);
	}

	ret = crypto_cb->lrng_hash_final(shash, pool->aux_pool);
	if (ret)
		goto err;

	collected_ent_bits += lrng_data_to_entropy(collected_irqs);

	/*
	 * Truncate to available entropy as implicitly allowed by SP800-90B
	 * section 3.1.5.1.1 table 1 which awards truncated hashes full
	 * entropy.
	 *
	 * During boot time, we read requested_bits data with
	 * collected_ent_bits entropy. In case our conservative entropy
	 * estimate underestimates the available entropy we can transport as
	 * much available entropy as possible. The entropy pool does not
	 * operate compliant to the German AIS 21/31 NTG.1 yet.
	 */
	memcpy(outbuf, pool->aux_pool, fully_seeded ? collected_ent_bits >> 3 :
						      requested_bits >> 3);

	pr_debug("obtained %u bits of entropy\n", collected_ent_bits);

out:
	spin_unlock_irqrestore(&pool->lock, flags2);
	read_unlock_irqrestore(&drng->hash_lock, flags);
	memzero_explicit(digest, sizeof(digest));
	return collected_ent_bits;

err:
	collected_ent_bits = 0;
	goto out;
}

/* Compress the lrng_pcpu_array array into lrng_pcpu_pool */
static inline void lrng_pcpu_array_compress(void)
{
	struct shash_desc *shash =
			(struct shash_desc *)this_cpu_ptr(lrng_pcpu_pool);
	struct lrng_drng **lrng_drng = lrng_drng_instances();
	struct lrng_drng *drng = lrng_drng_init_instance();
	const struct lrng_crypto_cb *crypto_cb;
	spinlock_t *lock = this_cpu_ptr(&lrng_pcpu_lock);
	unsigned long flags, flags2;
	int node = numa_node_id();
	void *hash;
	bool init = false;

	/* Get NUMA-node local hash instance */
	if (lrng_drng && lrng_drng[node])
		drng = lrng_drng[node];

	read_lock_irqsave(&drng->hash_lock, flags);
	crypto_cb = drng->crypto_cb;
	hash = drng->hash;

	if (unlikely(!this_cpu_read(lrng_pcpu_lock_init))) {
		init = true;
		spin_lock_init(lock);
		this_cpu_write(lrng_pcpu_lock_init, true);
		pr_debug("Initializing per-CPU entropy pool for CPU %d on NUMA node %d with hash %s\n",
			 raw_smp_processor_id(), node,
			 crypto_cb->lrng_hash_name());
	}

	spin_lock_irqsave(lock, flags2);

	if (unlikely(init) && crypto_cb->lrng_hash_init(shash, hash)) {
		this_cpu_write(lrng_pcpu_lock_init, false);
		pr_warn("Initialization of hash failed\n");
	} else if (lrng_pcpu_continuous_compression) {
		/* Add entire per-CPU data array content into entropy pool. */
		if (crypto_cb->lrng_hash_update(shash,
					(u8 *)this_cpu_ptr(lrng_pcpu_array),
					LRNG_DATA_ARRAY_SIZE * sizeof(u32)))
			pr_warn_ratelimited("Hashing of entropy data failed\n");
	}

	spin_unlock_irqrestore(lock, flags2);
	read_unlock_irqrestore(&drng->hash_lock, flags);
}

/* Compress data array into hash */
static inline void lrng_pcpu_array_to_hash(u32 ptr)
{
	u32 *array = this_cpu_ptr(lrng_pcpu_array);

	/*
	 * During boot time the hash operation is triggered more often than
	 * during regular operation.
	 */
	if (unlikely(!lrng_state_fully_seeded())) {
		if ((ptr & 31) && (ptr < LRNG_DATA_WORD_MASK))
			return;
	} else if (ptr < LRNG_DATA_WORD_MASK) {
		return;
	}

	if (lrng_raw_array_entropy_store(*array)) {
		u32 i;

		/*
		 * If we fed even a part of the array to external analysis, we
		 * mark that the entire array and the per-CPU pool to have no
		 * entropy. This is due to the non-IID property of the data as
		 * we do not fully know whether the existing dependencies
		 * diminish the entropy beyond to what we expect it has.
		 */
		atomic_set(this_cpu_ptr(&lrng_pcpu_array_irqs), 0);

		for (i = 1; i < LRNG_DATA_ARRAY_SIZE; i++)
			lrng_raw_array_entropy_store(*(array + i));
	} else {
		lrng_pcpu_array_compress();
		/* Ping pool handler about received entropy */
		lrng_pool_add_irq();
	}
}

/*
 * Concatenate full 32 bit word at the end of time array even when current
 * ptr is not aligned to sizeof(data).
 */
static inline void _lrng_pcpu_array_add_u32(u32 data)
{
	/* Increment pointer by number of slots taken for input value */
	u32 pre_ptr, mask, ptr = this_cpu_add_return(lrng_pcpu_array_ptr,
						     LRNG_DATA_SLOTS_PER_UINT);
	unsigned int pre_array;

	/*
	 * This function injects a unit into the array - guarantee that
	 * array unit size is equal to data type of input data.
	 */
	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS != (sizeof(data) << 3));

	/*
	 * The following logic requires at least two units holding
	 * the data as otherwise the pointer would immediately wrap when
	 * injection an u32 word.
	 */
	BUILD_BUG_ON(LRNG_DATA_NUM_VALUES <= LRNG_DATA_SLOTS_PER_UINT);

	lrng_pcpu_split_u32(&ptr, &pre_ptr, &mask);

	/* MSB of data go into previous unit */
	pre_array = lrng_data_idx2array(pre_ptr);
	/* zeroization of slot to ensure the following OR adds the data */
	this_cpu_and(lrng_pcpu_array[pre_array], ~(0xffffffff &~ mask));
	this_cpu_or(lrng_pcpu_array[pre_array], data & ~mask);

	/* Invoke compression as we just filled data array completely */
	if (unlikely(pre_ptr > ptr))
		lrng_pcpu_array_to_hash(LRNG_DATA_WORD_MASK);

	/* LSB of data go into current unit */
	this_cpu_write(lrng_pcpu_array[lrng_data_idx2array(ptr)],
		       data & mask);

	if (likely(pre_ptr <= ptr))
		lrng_pcpu_array_to_hash(ptr);
}

/* Concatenate a 32-bit word at the end of the per-CPU array */
void lrng_pcpu_array_add_u32(u32 data)
{
	/*
	 * Disregard entropy-less data without continuous compression to
	 * avoid it overwriting data with entropy when array ptr wraps.
	 */
	if (lrng_pcpu_continuous_compression)
		_lrng_pcpu_array_add_u32(data);
}

/* Concatenate data of max LRNG_DATA_SLOTSIZE_MASK at the end of time array */
static inline void lrng_pcpu_array_add_slot(u32 data)
{
	/* Get slot */
	u32 ptr = this_cpu_inc_return(lrng_pcpu_array_ptr) &
							LRNG_DATA_WORD_MASK;
	unsigned int array = lrng_data_idx2array(ptr);
	unsigned int slot = lrng_data_idx2slot(ptr);

	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS % LRNG_DATA_SLOTSIZE_BITS);
	/* Ensure consistency of values */
	BUILD_BUG_ON(LRNG_DATA_ARRAY_MEMBER_BITS !=
		     sizeof(lrng_pcpu_array[0]) << 3);

	/* zeroization of slot to ensure the following OR adds the data */
	this_cpu_and(lrng_pcpu_array[array],
		     ~(lrng_data_slot_val(0xffffffff & LRNG_DATA_SLOTSIZE_MASK,
					  slot)));
	/* Store data into slot */
	this_cpu_or(lrng_pcpu_array[array], lrng_data_slot_val(data, slot));

	lrng_pcpu_array_to_hash(ptr);
}

static inline void
lrng_time_process_common(u32 time, void(*add_time)(u32 data))
{
	enum lrng_health_res health_test;

	if (lrng_raw_hires_entropy_store(time))
		return;

	health_test = lrng_health_test(time);
	if (health_test > lrng_health_fail_use)
		return;

	if (health_test == lrng_health_pass)
		atomic_inc_return(this_cpu_ptr(&lrng_pcpu_array_irqs));

	add_time(time);
}

/*
 * Batching up of entropy in per-CPU array before injecting into entropy pool.
 */
static inline void lrng_time_process(void)
{
	u32 now_time = random_get_entropy();

	if (unlikely(!lrng_state_fully_seeded())) {
		/* During boot time, we process the full time stamp */
		lrng_time_process_common(now_time, _lrng_pcpu_array_add_u32);
	} else {
		/* Runtime operation */
		lrng_time_process_common(now_time & LRNG_DATA_SLOTSIZE_MASK,
					 lrng_pcpu_array_add_slot);
	}

	lrng_perf_time(now_time);
}

/* Hot code path - Callback for interrupt handler */
void add_interrupt_randomness(int irq, int irq_flg)
{
	if (lrng_pool_highres_timer()) {
		lrng_time_process();
	} else {
		struct pt_regs *regs = get_irq_regs();
		static atomic_t reg_idx = ATOMIC_INIT(0);
		u64 ip;
		u32 tmp;

		if (regs) {
			u32 *ptr = (u32 *)regs;
			int reg_ptr = atomic_add_return_relaxed(1, &reg_idx);
			size_t n = (sizeof(struct pt_regs) / sizeof(u32));

			ip = instruction_pointer(regs);
			tmp = *(ptr + (reg_ptr % n));
			tmp = lrng_raw_regs_entropy_store(tmp) ? 0 : tmp;
			_lrng_pcpu_array_add_u32(tmp);
		} else {
			ip = _RET_IP_;
		}

		lrng_time_process();

		/*
		 * The XOR operation combining the different values is not
		 * considered to destroy entropy since the entirety of all
		 * processed values delivers the entropy (and not each
		 * value separately of the other values).
		 */
		tmp = lrng_raw_jiffies_entropy_store(jiffies) ? 0 : jiffies;
		tmp ^= lrng_raw_irq_entropy_store(irq) ? 0 : irq;
		tmp ^= lrng_raw_irqflags_entropy_store(irq_flg) ? 0 : irq_flg;
		tmp ^= lrng_raw_retip_entropy_store(ip) ? 0 : ip;
		tmp ^= ip >> 32;
		_lrng_pcpu_array_add_u32(tmp);
	}
}
EXPORT_SYMBOL(add_interrupt_randomness);
