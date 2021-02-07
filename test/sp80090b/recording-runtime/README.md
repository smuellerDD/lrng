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
   `cat /proc/lrng_type` for a specification of the ued hash). This can
   be achieved by compiling CONFIG_LRNG_ACVT_HASH and use the test harness
   provided with https://github.com/smuellerDD/acvpparser and compile it
   with `make lrng`. This harness will test the SHA implementation currently
   in use by the LRNG with an ACVP test vector.

