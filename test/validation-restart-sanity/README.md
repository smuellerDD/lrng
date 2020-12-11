# Sanity of Raw Entropy Data Restart Test Data

This validation tool processes the restart raw entropy data compliant to
SP800-90B section 3.1.5, i.e. it performs the sanity test.

Each restart must be recorded in a single file where each raw entropy
value is stored on one line.

## Prerequisites

To execute the testing, you need:

	* NIST SP800-90B tool from:
		https://github.com/usnistgov/SP800-90B_EntropyAssessment

	* Obtain the sample data recorded on the target platforms

	* Configure processdata.sh with proper parameter values


### Parameters of processdata.sh

EXPECTED_ENTROPY: Expected entropy of the data in bits

RESULTSDIR: Location of the sample data files (with .data extension)

OUTDIR: Location for the interim data bit streams (var and single),
and results.

## Conclusion

The results files contain the final sanity validation result.
