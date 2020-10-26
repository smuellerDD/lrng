# Linear Feedback Shift Register

The LRNG uses a hash operation to compress entropy. Such hash operation
is state-of-the-art for compressing entropy. The LRNG allows the use of
various different hash implementations.

Although the documentation [1] section 4.5 explains that the hash
implementations do not reduce the performance of the LRNG interrupt handler
compared to the legacy /dev/random implementation, it may be possible
that smaller systems, e.g. IoT systems, receive a significant performance
hit. To alleviate this performance hit, an LFSR implementation is
provided.

The LFSR provided with this directory offers a drop-in replacement of
the hashing operation. Instead of using the hash, the LFSR performs the
entropy compression. LFSRs are considered to compress entropy and provide
an output that is indistinguishable from an ideal random number generator.

The LFSR implemented here uses a primitive and irreducible polynomial.

# SP800-90B Impact

When using the LFSR instead of a hash, the entropy assessment relevant
for the SP800-90B compliance needs updating. Yet, due to the use of a
non-vetted conditioning component, the LRNG will never be able to
provide full entropy.

Note, due to the construction of the LRNG, two LFSR operations would
be chained to fill the temporary seed buffer:

- 1st LFSR operation is the compression of the per-CPU array into the
  per-CPU pool, and the

- 2nd operation is the compression of the different per-CPU pools and
  the aux pool to obtain the resulting 256 bits for the temporary seed buffer).

# Usage

## Wiring up of LFSR to replace hash

To use the LFSR instead of a hash, do:

1. Copy lrng_lfsr.c and lrng_lfsr.h into the LRNG kernel directory.

2. Modify the Makefile in the LRNG kernel directory to add lrng_lfsr.o
   compilation

3. Wire up the LFSR to replace the default hash implementation: replace
   all lrng_cc20_hash function pointers with the corresponding lrng_lfsr*
   functions from lrng_lfsr.h.

# Add power-on self test

To add the LFSR power-on self test, do:

1. Add the code in `lrng_selftest.c` to the `lrng_selftest.c` in the LRNG
   kernel directory.

2. Add invocation of `lrng_pool_lfsr_selftest` to function `lrng_selftest`
   to ensure it is invoked during startup time or during run time.

3. Disable use of `lrng_hash_selftest`.

# Test Tools

The following test tools are available in the `tests` directory:

1. `lfsr_testvector_generation.c` is provided to generate the reference test
   vectors used for the self tests. See code comments on how to compile and
   execute.

2. `lfsr_demonstration.c` is provided to allow studying of the LFSR in
   user space. See code comments on how to compile and execute. Per default,
   human reasable output is produced on STDOUT. Binary data generated
   from a monotonic increasing counter is produced on STDERR. When analyzing
   the output with statistical tests like `ent`, `dieharder`, or the
   SP800-90B analysis tool, the output should exhibit characteristics of
   a perfect random number generator.

# Bibliography

[1] https://www.chronox.de/lrng/doc/lrng.pdf

# Author
Copyright (c) 2020 - Stephan Mueller <smueller@chronox.de>
