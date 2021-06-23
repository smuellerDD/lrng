# Validation of Raw Entropy Data

This tool is used to calculate the minimum entropy values
compliant to SP800-90B for the gathered data.

The validation operation of the raw entropy data is invoked with the
processdata.sh script.

The first step is performed by the extractlsb program, which reads the input
data, extracts the significant bits from each sample item (using a mask that
you provide) and split it in two bit stream files, one containing the var
sample, and the other the single sample.

In the second step, the binary data streams are processed with the SP800-90B
entropy assessment tool, which calculates the min entropy.

The resulting minimum entropy for each data stream is provided in the
*.minentropy file. The log file contains a summary of the steps performed and
the output of the extractlsb program.


## Prerequisites

To execute the testing, you need:

	* NIST SP800-90B tool from:
		https://github.com/usnistgov/SP800-90B_EntropyAssessment

	* Obtain the sample data recorded on the target platforms

	* Configure processdata.sh with proper parameter values


### Parameters of processdata.sh

ENTROPYDATA_DIR: Location of the sample data files (with .data extension)

RESULTS_DIR: Location for the interim data bit streams (var and single),
and results.

LOGFILE: Name of the log file. The default is $RESULTS_DIR/processdata.log.

EATOOL_NONIID: Path of the python program used from the Entropy Assessment tool
(usually, noniid_main.py).

BUILD_EXTRACT: Indicates whether the script will build the extractlsb program.
The default is "yes".

MASK_LIST: Indicates the extraction method from each sample item. You can
indicate one or more methods; the script will generate one bit stream data
file set (var and single) for each extraction method. See below for a more
detailed explanation.

MAX_EVENTS: the size of the sample that will be extracted from the sample data.
The default is 100000 (a 1% of the size of the sample file specified in the
ROUNDS define macro). Notice that the minimum value suggested by SP800-90B is
1000000, so you'll have to increase the default value (notice that this
severily impacts in the performance and memory requirements of the python tool).

## Processing of Test Data

To process the test data and obtain the entropy statement, you need to
execute the processdata.sh script.

The results are given in the RESULTS_DIR.

# Interpretation of Results

The result of the data analysis contains in the file
`lrng_raw_noise.minentropy_FF_8bits.txt` at the bottom data like the following:

```
H_original: 2.387470
H_bitstring: 0.337104

min(H_original, 8 X H_bitstring): 2.387470
```

The last value gives you the entropy estimate per time stamp measured for
one interrupt. That means for one interrupt event the given number of
entropy in bits is collected on average.

Per default, the LRNG heuristic applies one bit of entropy per interrupt
event. This implies that the measurement must show that *at least* 1 bit
of entropy is present. In the example above, the measurement shows that
2.3 bits of entropy is present which implies that the available amount of
entropy is more than what the LRNG heuristic applies.

# Approach to Solve Insufficient Entropy

In case your entropy assessment shows that insufficient entropy is
present (e.g. by showing that the measured entropy rate is less than 1), you
can adjust the heuristic entropy value the LRNG applies.

This is done with the kernel configuration option of
`CONFIG_LRNG_IRQ_ENTROPY_RATE`. This configuration value defines how many
interrupt events are collected by the LRNG in order to claim 256 bits of
entropy to be collected.

The entropy rate per interrupt event is therefore

```
	rate = 256/CONFIG_LRNG_IRQ_ENTROPY_RATE (bits of entropy per event)
```

Thus, if you, say, configure CONFIG_LRNG_IRQ_ENTROPY_RATE=512, the LRNG
heuristic applies an entropy estimate of 1/2 bits of entropy per interrupt
event.

With this configuration, you have to make sure that the heuristic value
is less than the measured entropy rate.

# Author
Stephan Mueller <smueller@chronox.de>
