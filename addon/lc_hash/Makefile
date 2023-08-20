# Adjust the refernce to the Leancrypto header file as needed
CFLAGS_lrng_hash_lc.o =-I/usr/local/include

# Adjust the reference to the Leancrypto linux_kernel directory holding
# the Module.symvers file
KBUILD_EXTRA_SYMBOLS = /home/sm/hacking/sources/leancrypto/leancrypto/linux_kernel/Module.symvers

###################### NO CHANGES AFTER THIS LINE ######################
CFLAGS_lrng_hash_lc.o +=-DLINUX_KERNEL
obj-m += lrng_hash_lc.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
