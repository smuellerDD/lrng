# LRNG Hacking

## LRNG DRNG Switchable Module

The LRNG allows runtime or boottime replacement of the deterministic
processing mechanisms. These include:

* The deterministic random number generator (DRNG) used to provide data
via the LRNG output interfaces.

* The hash operation used for conditioning operation.

To provide a new switchable module, you must implement all callbacks
listed in `include/linux/lrng.h`. Each interface is documented there.

The DRNG implementation is allowed to sleep. The hash implementation must not
sleep.

If you want to only provide a DRNG implementation, you may use the Linux
kernel crypto API SHASH implementation provided by lrng_kcapi_hash.h to
fill the function pointers for the hashing operation. Though, the
hash allocation function is what you must provide: you can use the
existing lrng_kcapi_hash_alloc() call, but you must provide the hash name
that can be used by the Linux kernel crypto API. See lrng_drbg_hash_alloc
as an example.

## Adding LRNG Entropy Source

In order to add an entropy source to the LRNG, the following decision first
must be made: is it a slow or fast source. The following definitions apply:

* Fast entropy source: It can deliver entropy at the time the LRNG requests it.

* Slow entropy source: The entropy "trickles in" over time that cannot be
defined and predicted by the LRNG.

### Adding Fast Entropy Source

The addition of a fast entropy source requires the following steps:

* Provide a `get` function that has the following parameters:

	- a buffer to be filled with random data

	- an integer specifying the amount of entropy to fill the buffer

	- it must return the amount entropy truly added to the buffer

* Extend `struct entropy_buf` by another buffer.

* Invoke the `get` function from `lrng_fill_seed_buffer`.

* Extend the calculation of `external_es` in `lrng_init_ops`.

* Extend the calculation in `lrng_slow_noise_req_entropy`.

* If the entropy source requires to be initialized at some point and will not
provide the data right from the start of the kernel, you may want to invoke
the `lrng_update_entropy_thresh` function during the entropy source
initialization to inform the LRNG that new entropy is available.
