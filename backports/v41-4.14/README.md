# Backport Patches for Kernel 4.14

NOTE: The software SHA-256 is not available which means that SHA-1 is used
for the default hash. When using lrng_drbg or lrng_kcapi, SHA-512 is used
though.

The patches allow applying the LRNG to older kernels. Perform the following
operations:

1. apply all six-digit patches providing necessary code updates to
   required before applying the LRNG patches.

2. apply all LRNG patches from upstream - note, some hunks are expected
   considering the original code base is written for a different kernel version

3. apply all two-digit patches providing code updates after the LRNG patches
   are applied.
