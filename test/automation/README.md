# Automated Regression Tests

The directory contains automated regression tests for the LRNG verifying
various aspects of the LRNG with different permutations of ESDM configurations.

## Quick Start

Execute the following steps

1. Obtain [Eudyptula-Boot](https://github.com/vincentbernat/eudyptula-boot) and
   copy the shell script into `$PATH`.

2. Update the configuration options in `libtest.sh`.

3. Unpack a kernel and compile it with the options found in
   [Eudyptula-Boot](https://github.com/vincentbernat/eudyptula-boot) and the
   LRNG patches and configuration options you want to test. The directory
   `kernel-configuration` contains sample kernel configuration files that
   were used for successful testing.

4. Execute `exec_test.sh` to invoke all tests or invoke the different
   `??_test_*.sh` scripts to individually perform tests.

5. Collect the test logs from `$TMPDIR` configured in `libtest.sh`.

## Test Approach

The test harness uses QEMU virtualization to execute a kernel locally
and perform the testing. The virtualization is set up using the tool
[Eudyptula-Boot](https://github.com/vincentbernat/eudyptula-boot).

# Author
Stephan Mueller <smueller@chronox.de>
