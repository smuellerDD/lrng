# Obtain Raw Noise Data

This tool offers the collection of raw entropy from the running Linux
kernel compliant to SP800-90B section 3.1.3

To obtain raw noise data from the LRNG, follow these steps:

1. Enable LRNG_RAW_HIRES_ENTROPY, compile, install and reboot the kernel

2. Compile getrawentropy.c as documented in that file

3. Execute as root:
	getrawentropy -s 1000000 > /dev/shm/lrng_raw_noise.data

4. Process the obtained data with validation*/processdata.sh

Note, in addition to the raw-nose data test, a validation of the
vetted conditioning component is required:

1. Perform the ACVP test of the used SHA conditioning function (see
   `cat /proc/lrng_type` for a specification of the used hash). This can
   be achieved as follows:

	a) When using the built-in SHA-256 algorithm, you need to
	   compile CONFIG_LRNG_ACVT_HASH and use the test harness
	   provided with https://github.com/smuellerDD/acvpparser.
	   This test harness is to be compiled with `make lrng`.

	b) When using the LRNG kernel module of lrng_drbg.ko providing
	   the SP800-90A DRBG together with the hash implementation
	   from the kernel crypto API, the LRNG does not need a specific
	   interface as above. Yet, you need to obtain the test harness
	   provided with https://github.com/smuellerDD/acvpparser. This
	   harness is to be compiled:

		i) make kcapi_lrng

		ii) cd backend_interfaces/kcapi_lrng

		iii) make && insmod kcapi-lrng.ko && cd ../../

  The test harness now can execute the test vectors as documented with the
  ACVP Parser.

# Note on Built-in Algorithms

The built-in algorithms of ChaCha20 DRNG and SHA-256 are commonly not intended
for ACVP testing. Albeit the SHA-256 can be ACVP-tested, the ChaCha20 DRNG
cannot.

Yet, the SHA-256 interface is provided allowing to test the SHA-256
implementation even with your own vectors to analyze whether SHA-256 works as
intended.

The ChaCha20 DRNG testing can be conducted with the user space implementation
provided by https://github.com/smuellerDD/chacha20_drng. This implementation
is an extraction of the ChaCha20 DRNG used by the LRNG. You can see that by
comparing the power-on self-test provided in lrng_selftest.c and used in
the ChaCha20 DRNG.
