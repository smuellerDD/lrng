# Validation of Raw Entropy Data Restart Test

This validation tool processes the restart raw entropy data compliant to
SP800-90B section 3.1.4.

Each restart must be recorded in a single file where each raw entropy
value is stored on one line.

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

EATOOL: Path of the program used from the Entropy Assessment restart tool
(usually, ea_restart).

BUILD_EXTRACT: Indicates whether the script will build the extractlsb program.
The default is "yes".

MASK_LIST: Indicates the extraction method from each sample item. You can
indicate one or more methods; the script will generate one bit stream data
file set (var and single) for each extraction method. See below for a more
detailed explanation.

MAX_EVENTS: the size of the sample that will be extracted from the sample data.
The default is 1000 as this is the size of events for one restart.
