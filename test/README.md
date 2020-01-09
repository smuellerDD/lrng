# Test Cases for the LRNG

This directory contains various tests for the LRNG validation.

The following description of the different tests apply

* `atomic`: This directory contains a kernel module that tries to generate
  large quantities of random numbers in atomic context. This shall verify
  that the LRNG properly delivers data in atomic context. Such test is of
  particular interest when the lrng_drbg.ko module is loaded and the
  kernel crypto API DRBG is used which may sleep.

* `sp80090b`: This directory contains the raw noise data gathering test
  compliant to SP800-90B section 3.1.3. In addition, the restart test
  defined by SP800-90B section 3.1.4. Please read the README in this directory.

* `getrawentropy.c`: This application is used when compiling the lrng_testing.c
  kernel code to obtain raw, unconditioned entropy. If the kernel was booted
  with the kernel command line option of `lrng_testing.boot_test=1`, this tool
  would obtain the raw entropy data from the first 1,000 interrupt events.

* `lfsr_demonstration.c`: This application provides the output of the LFSR to
  demonstrate that it generates white noise from a monotonic counter. To start
  the test, please follow the instructions in the initial comments of
  `lfsr_demonstration.c`.

* `lrng_get_speed.sh`: This script summarizes the performance of either the
  legacy Linux-RNG or the LRNG. Simply invoke on a kernel with either the
  legacy Linux-RNG or the LRNG compiled and analyze the human-readable output
  indicating the performance in bytes per second for different request sizes.
  This tool requires the compiled `speedtest.c` code.

* `sanity_test`: The test provides a sanity test to verify that no bugs like
  CVE-2013-4345 are present. The result listing is a Chi-Square result value
  and should therefore not be below 1 or above 99.

* `speedtest.c`: This application allows the measurement of the performance
  when generating random numbers with different block sizes. It is used
  by `lrng_get_speed.sh`.

* `swap_stress.sh`: This tool must be run with root privilege. It is a stress
  test for swapping DRNG implementations to verify proper locking and proper
  swap operation in loaded systems. The tool instantiates applications that
  continuously pull data from /dev/urandom (one caller per identified NUMA node)
  and a caller pulling /dev/random. While the LRNG is under this load, the
  kernel module lrng_drbg.ko is continuously loaded and unloaded triggering a
  constant swap of the DRNG from / to ChaCha20 and SP800-90A DRBG.

* `syscall_test.c`: This tool allows invoking of all types of the getrandom(2)
  system calls and monitor the behavior.

* `test_proc_A.pl`: This test performs the Test Procedure A from AIS 31 as
  defined in section 2.4.4.1. Generate at least 5 MB of data from /dev/urandom
  and invoke the script with the generate data. Beware, this test may run for
  a long time (one or two hours).

* `time_storage.c`: Test demonstration verifying the correctness of the
  storage of the truncated time stamp into a buffer as used by the interrupt
  handling code.

Please note that the ChaCha20 DRNG implementation can be tested with
https://github.com/smuellerDD/chacha20_drng which used to be the test
tool for this implementation and now turned into a stand-alone DRNG.

## Appropriateness of Polynomials

To test whether a polynomial is irreducible and primitive (i.e. the properties
that are the key for a polynomial to be used in an LFSR), the online version
of Magma can be used available at:

http://magma.maths.usyd.edu.au/calc/

Use the following code and only replace the polynomials (note, the
polynomials in the code are always smaller by one due to C arrays starting
at 0):

F:=GF(2);
F;
P<x>:=PolynomialRing(F);
P;
P<x>:=x^64 + x^63 + x^61 + x^60 + 1;
P;
print "is irreducible: "; IsIrreducible(P);
print "is primitive: "; IsPrimitive(P);
