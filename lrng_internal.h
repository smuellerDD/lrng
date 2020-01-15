/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (C) 2018 - 2020, Stephan Mueller <smueller@chronox.de>
 */

#ifndef _LRNG_INTERNAL_H
#define _LRNG_INTERNAL_H

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

/*************************** General LRNG parameter ***************************/

/* Entropy pool parameter
 *
 * The LRNG_POOL_SIZE cannot be smaller than 64 bytes as the SHA-1 operation
 * in lrng_chacha20.c requires multiples of 64 bytes
 */
#define LRNG_POOL_SIZE			(16 << CONFIG_LRNG_POOL_SIZE)
#define LRNG_POOL_WORD_BYTES		(4)	/* (sizeof(atomic_t)) */
#define LRNG_POOL_SIZE_BYTES		(LRNG_POOL_SIZE * LRNG_POOL_WORD_BYTES)
#define LRNG_POOL_SIZE_BITS		(LRNG_POOL_SIZE_BYTES * 8)
#define LRNG_POOL_WORD_BITS		(LRNG_POOL_WORD_BYTES * 8)

/* Security strength of LRNG -- this must match DRNG security strength */
#define LRNG_DRNG_SECURITY_STRENGTH_BYTES 32
#define LRNG_DRNG_SECURITY_STRENGTH_BITS (LRNG_DRNG_SECURITY_STRENGTH_BYTES * 8)
#define LRNG_DRNG_BLOCKSIZE 64		/* Maximum of DRNG block sizes */

/*
 * SP800-90A defines a maximum request size of 1<<16 bytes. The given value is
 * considered a safer margin.
 *
 * This value is allowed to be changed.
 */
#define LRNG_DRNG_MAX_REQSIZE		(1<<12)

/*
 * SP800-90A defines a maximum number of requests between reseeds of 2^48.
 * The given value is considered a much safer margin, balancing requests for
 * frequent reseeds with the need to conserve entropy. This value MUST NOT be
 * larger than INT_MAX because it is used in an atomic_t.
 *
 * This value is allowed to be changed.
 */
#define LRNG_DRNG_RESEED_THRESH		(1<<20)

/*
 * Number of interrupts to be recorded to assume that DRNG security strength
 * bits of entropy are received.
 * Note: a value below the DRNG security strength should not be defined as this
 *	 may imply the DRNG can never be fully seeded in case other noise
 *	 sources are unavailable.
 *
 * This value is allowed to be changed.
 */
#define LRNG_IRQ_ENTROPY_BITS		LRNG_DRNG_SECURITY_STRENGTH_BITS

/*
 * Min required seed entropy is 128 bits covering the minimum entropy
 * requirement of SP800-131A and the German BSI's TR02102.
 *
 * This value is allowed to be changed.
 */
#define LRNG_FULL_SEED_ENTROPY_BITS	LRNG_DRNG_SECURITY_STRENGTH_BITS
#define LRNG_MIN_SEED_ENTROPY_BITS	128
#define LRNG_INIT_ENTROPY_BITS		32

/*
 * Amount of entropy that is lost with the conditioning functions of LFSR and
 * hash_df as shown with the entropy analysis compliant to SP800-90B.
 */
#define LRNG_CONDITIONING_ENTROPY_LOSS	1

/*
 * Wakeup value
 *
 * This value is allowed to be changed.
 */
#if (LRNG_POOL_SIZE_BITS <= (LRNG_DRNG_SECURITY_STRENGTH_BITS * 2))
# define LRNG_WRITE_WAKEUP_ENTROPY	(LRNG_DRNG_SECURITY_STRENGTH_BITS + \
					LRNG_CONDITIONING_ENTROPY_LOSS)
#else
# define LRNG_WRITE_WAKEUP_ENTROPY	(LRNG_DRNG_SECURITY_STRENGTH_BITS * 2)
#endif

/*
 * Oversampling factor of IRQ events to obtain
 * LRNG_DRNG_SECURITY_STRENGTH_BYTES. This factor is used when a
 * high-resolution time stamp is not available. In this case, jiffies and
 * register contents are used to fill the entropy pool. These noise sources
 * are much less entropic than the high-resolution timer. The entropy content
 * is the entropy content assumed with LRNG_IRQ_ENTROPY_BITS divided by
 * LRNG_IRQ_OVERSAMPLING_FACTOR.
 *
 * This value is allowed to be changed.
 */
#define LRNG_IRQ_OVERSAMPLING_FACTOR	10

/*
 * Alignmask which should cover all cipher implementations
 * WARNING: If this is changed to a value larger than 8, manual
 * alignment is necessary as older versions of GCC may not be capable
 * of aligning stack variables at boundaries greater than 8.
 * In this case, PTR_ALIGN must be used.
 */
#define LRNG_KCAPI_ALIGN		8

/************************ Default DRNG implementation *************************/

extern struct chacha20_state chacha20;
extern const struct lrng_crypto_cb lrng_cc20_crypto_cb;
void lrng_cc20_init_state(struct chacha20_state *state);

/********************************** /proc *************************************/

#ifdef CONFIG_SYSCTL
void lrng_pool_inc_numa_node(void);
#else
static inline void lrng_pool_inc_numa_node(void) { }
#endif

/****************************** LRNG interfaces *******************************/

extern u32 lrng_write_wakeup_bits;
extern int lrng_drng_reseed_max_time;

void lrng_writer_wakeup(void);
void lrng_init_wakeup(void);
void lrng_debug_report_seedlevel(const char *name);
void lrng_process_ready_list(void);

/************************** Entropy pool management ***************************/

enum lrng_external_noise_source {
	lrng_noise_source_hw,
	lrng_noise_source_user
};

bool lrng_state_exseed_allow(enum lrng_external_noise_source source);
void lrng_state_exseed_set(enum lrng_external_noise_source source, bool type);
void lrng_state_init_seed_work(void);
u32 lrng_avail_entropy(void);
void lrng_set_entropy_thresh(u32 new);
int lrng_pool_trylock(void);
void lrng_pool_unlock(void);
void lrng_reset_state(void);
void lrng_pool_all_numa_nodes_seeded(void);
bool lrng_state_min_seeded(void);
bool lrng_state_fully_seeded(void);
bool lrng_state_operational(void);
bool lrng_pool_highres_timer(void);
void lrng_pool_set_entropy(u32 entropy_bits);
void lrng_pool_lfsr(const u8 *buf, u32 buflen);
void lrng_pool_lfsr_nonaligned(const u8 *buf, u32 buflen);
void lrng_pool_lfsr_u32(u32 value);
void lrng_pool_add_irq(u32 irq_num);
void lrng_pool_add_entropy(u32 entropy_bits);

struct entropy_buf {
	u8 a[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u8 b[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u8 c[LRNG_DRNG_SECURITY_STRENGTH_BYTES];
	u32 now;
};

int lrng_fill_seed_buffer(const struct lrng_crypto_cb *crypto_cb, void *hash,
			  struct entropy_buf *entropy_buf, u32 entropy_retain);
void lrng_init_ops(u32 seed_bits);

/************************** Jitter RNG Noise Source ***************************/

#ifdef CONFIG_LRNG_JENT
u32 lrng_get_jent(u8 *outbuf, unsigned int outbuflen);
u32 lrng_jent_entropylevel(void);
#else /* CONFIG_CRYPTO_JITTERENTROPY */
static inline u32 lrng_get_jent(u8 *outbuf, unsigned int outbuflen) {return 0; }
static inline u32 lrng_jent_entropylevel(void) { return 0; }
#endif /* CONFIG_CRYPTO_JITTERENTROPY */

/*************************** CPU-based Noise Source ***************************/

u32 lrng_get_arch(u8 *outbuf);
u32 lrng_slow_noise_req_entropy(u32 required_entropy_bits);

/****************************** DRNG processing *******************************/

/* Secondary DRNG state handle */
struct lrng_drng {
	void *drng;				/* DRNG handle */
	void *hash;				/* Hash handle */
	const struct lrng_crypto_cb *crypto_cb;	/* Crypto callbacks */
	atomic_t requests;			/* Number of DRNG requests */
	unsigned long last_seeded;		/* Last time it was seeded */
	bool fully_seeded;			/* Is DRNG fully seeded? */
	bool force_reseed;			/* Force a reseed */
	struct mutex lock;
	spinlock_t spin_lock;
};

extern struct mutex lrng_crypto_cb_update;

struct lrng_drng *lrng_drng_init_instance(void);
struct lrng_drng *lrng_drng_atomic_instance(void);

static __always_inline bool lrng_drng_is_atomic(struct lrng_drng *drng)
{
	return (drng->drng == lrng_drng_atomic_instance()->drng);
}

/* Lock the DRNG */
static __always_inline void lrng_drng_lock(struct lrng_drng *drng,
					   unsigned long *flags)
{
	/* Use spin lock in case the atomic DRNG context is used */
	if (lrng_drng_is_atomic(drng)) {
		spin_lock_irqsave(&drng->spin_lock, *flags);

		/*
		 * In case a lock transition happened while we were spinning,
		 * catch this case and use the new lock type.
		 */
		if (!lrng_drng_is_atomic(drng)) {
			spin_unlock_irqrestore(&drng->spin_lock, *flags);
			mutex_lock(&drng->lock);
		}
	} else {
		mutex_lock(&drng->lock);
	}
}

/* Unlock the DRNG */
static __always_inline void lrng_drng_unlock(struct lrng_drng *drng,
					     unsigned long *flags)
{
	if (lrng_drng_is_atomic(drng))
		spin_unlock_irqrestore(&drng->spin_lock, *flags);
	else
		mutex_unlock(&drng->lock);
}

bool lrng_get_available(void);
void lrng_set_available(void);
void lrng_drngs_init_cc20(void);
void lrng_drng_reset(struct lrng_drng *drng);
int lrng_drng_get_atomic(u8 *outbuf, u32 outbuflen);
int lrng_drng_get_sleep(u8 *outbuf, u32 outbuflen);
void lrng_drng_force_reseed(void);
void lrng_drng_seed_work(struct work_struct *dummy);

#ifdef CONFIG_NUMA
struct lrng_drng **lrng_drng_instances(void);
void lrng_drngs_numa_alloc(void);
#else	/* CONFIG_NUMA */
static inline struct lrng_drng **lrng_drng_instances(void) { return NULL; }
static inline void lrng_drngs_numa_alloc(void) { return; }
#endif /* CONFIG_NUMA */

/************************** Health Test linking code **************************/

enum lrng_health_res {
	lrng_health_pass,		/* Health test passes on time stamp */
	lrng_health_fail_use,		/* Time stamp unhealthy, but mix in */
	lrng_health_fail_drop		/* Time stamp unhealthy, drop it */
};

#ifdef CONFIG_LRNG_HEALTH_TESTS
bool lrng_sp80090b_startup_complete(void);
bool lrng_sp80090b_compliant(void);

enum lrng_health_res lrng_health_test(u32 now_time);
void lrng_health_disable(void);

void lrng_reset(void);
#else	/* CONFIG_LRNG_HEALTH_TESTS */
static inline bool lrng_sp80090b_startup_complete(void) { return true; }
static inline bool lrng_sp80090b_compliant(void) { return false; }

static inline enum lrng_health_res
lrng_health_test(u32 now_time) { return lrng_health_pass; }
static inline void lrng_health_disable(void) { }
#endif	/* CONFIG_LRNG_HEALTH_TESTS */

/****************************** Helper code ***********************************/

static inline u32 atomic_read_u32(atomic_t *v)
{
	return (u32)atomic_read(v);
}

/*************************** Auxiliary functions ******************************/

void invalidate_batched_entropy(void);

/***************************** Testing code ***********************************/

#ifdef CONFIG_LRNG_TESTING
bool lrng_raw_entropy_store(u32 value);
#else	/* CONFIG_LRNG_TESTING */
static inline bool lrng_raw_entropy_store(u32 value) { return false; }
#endif	/* CONFIG_LRNG_TESTING */

#endif /* _LRNG_INTERNAL_H */
