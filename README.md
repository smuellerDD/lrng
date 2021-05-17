# Linux /dev/random - a new approach

The code in this repository provides a different approach to /dev/random which
is called Linux Random Number Generator (LRNG) to collect entropy within the
Linux kernel. The main improvements compared to the legacy /dev/random is to
provide sufficient entropy during boot time as well as in virtual environments
and when using SSDs. A secondary design goal is to limit the impact of the
entropy collection on massive parallel systems and also allow the use
accelerated cryptographic primitives. Also, all steps of the entropic data
processing are testable.

The design and implementation is driven by a set of goals described in [1]
that the LRNG completely implements. Furthermore, [1] includes a
comparison with RNG design suggestions such as SP800-90B, SP800-90C, and
AIS20/31.

The LRNG provides a complete separation of the noise source maintenance
and the collection of entropy into an entropy pool from the post-processing
using a pseudo-random number generator. Different PRNGs are supported,
including:

* Built-in ChaCha20 PRNG which has no dependency to other kernel
  frameworks.

* SP800-90A DRBG using the kernel crypto API including its accelerated
  raw cipher implementations.

* Arbitrary PRNGs registered with the kernel crypto API

Booting the patch with the kernel command line option
"dyndbg=file drivers/char/lrng* +p" generates logs indicating the operation
of the LRNG. Each log is prepended with "lrng:".

The LRNG has a flexible design by allowing an easy replacement of the
deterministic random number generator component.

## LRNG Tests

Implementation verification as well as performance tests are provided in the
`test` directory.

## LRNG Backports

Backports to older kernels are provided with the patch sets in `backports`.
Note, the core idea of these backports is to use the unmodified LRNG patch
series and add patches to be applied before and after applying the LRNG
patches.

## ChaCha20 DRNG

The ChaCha20 DRNG used by the LRNG is implemented following standard pseudo-
random number generator architectures. To analyze the characteristics of the
ChaCha20 DRNG, the ChaCha20 DRNG is copied into a user space library
which is available at [2].

The ChaCha20 DRNG code used for the LRNG is identical to the code in [2]
which allows to apply conclusions drawn from [2] to be applied to the LRNG.

# Cryptographic Algorithm Testing

The used cryptographic algorithms are testable with the acvpparser [3].

# References

[1] `doc/lrng.pdf`

[2] [ChaCha20 DRNG](https://github.com/smuellerDD/chacha20_drng)

[3] [ACVPParser](https://github.com/smuellerDD/acvpparser)

# Author
Stephan Mueller <smueller@chronox.de>
