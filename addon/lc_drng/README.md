# KMAC-DRNG support from Leancrypto

The [Leancrypto](https://github.com/smuellerDD/leancrypto) library offers
support for the Linux kernel. All algorithms can be compiled into a
kernel module `leancrypto.ko`. Among other algorithms, the KMAC DRNG
based on KMAC256 is offered.

This directory provides the source code to make the KMAC DRNG available
to the LRNG.

## Compilation

The Leancrypto library user space installs its header files commonly to
`/usr/include` or `/usr/local/include`. These header files can at be directly
used for the kernel-space compilation as well. Thus, the `Makefile` in this
directory points to the directory with the Leancrypto header files. This
reference potentially must be updated.

Kbuild needs to have full knowledge of all symbols to avoid spitting out
warnings about undefined symbols. For compiling the `lrng_drng_lc.ko` module,
the `Module.symvers` generated during the compilation of the `leancrypto.ko`
kernel module is required. A reference to this file  must be provided in the
`Makefile`.

Afterwards, simply execute `make` to compile the `lrng_drng_lc.ko` module.

## Usage

It must be ensured that the `leancrypto.ko` module is inserted into the kernel
prior to inserting the `lrng_drng_lc.ko` module.

Once this is ensured simply `insmod lrng_drng_lc.ko`. This command registers
the KMAC DRNG automatically with the LRNG which in turn starts using it.

# References

[1] [Leancrypto](https://github.com/smuellerDD/leancrypto)
