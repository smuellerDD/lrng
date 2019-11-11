# Obtain Raw Noise Data

To obtain raw noise data from the LRNG, follow these steps:

1. Enable CONFIG_LRNG_TESTING, compile, install and reboot the kernel

2. Compile getrawentropy.c as documented in that file

3. Execute as root:
	getrawentropy -s 1000000 > /dev/shm/lrng_raw_noise.data

4. Read the test steps in lfsr_demonstration.c to potentially update that
   file. Then compile it and invoke as normal user:
	lfsr_demonstration > /dev/shm/lrng_raw_lfsr.data

4. Process the obtained data with validation/processdata.sh
