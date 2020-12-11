#!/bin/bash
#
# Process the entropy data

############################################################
# Configuration values                                     #
############################################################

EXPECTED_ENTROPY="1"

# point to the directory that contains the results from
# the entropy collection
if [ ! -z "$1" ]
then
	RESULTSDIR="$1"
else
	RESULTSDIR="../results-measurements"
fi

# Directory that shall hold the final result data
OUTDIR="../results-analysis-restart-soundness"

# Number of test samples to be processed
NUMSAMPLES=1000

############################################################
# Code only after this line -- do not change               #
############################################################

#############################
# Preparation
#############################
INPUT="$RESULTSDIR/lrng_raw_noise_restart*.data"
DELTAFILE="$OUTDIR/restart-consolidated"

EXEC="./sp80090b_sanity"

if [ ! -d $RESULTSDIR ]
then
	echo "Directory with raw entropy data $RESULTSDIR is missing"
	exit 1
fi

if [ ! -d $OUTDIR ]
then
	mkdir $OUTDIR
	if [ $? -ne 0 ]
	then
		echo "Directory for results $OUTDIR cannot be created"
		exit 1
	fi
fi

trap "make clean; rm -f $DELTAFILE; exit" 0 1 2 3 15

make clean
make

rm -f $DELTAFILE

#############################
# Actual data processing
#############################
#
# Step 1: Extract all time deltas of all raw files into one file with one line
#	  per restart test instance
#
# The output file contains $NUMSAMPLES time deltas per line where each line
# indicates one boot cycle
#
for i in $INPUT
do
	echo "Process recorded entropy data $i"

	./results_to_matrix.pl $i $DELTAFILE
done

#
# Step 2: Get the maximum value according to SP800-90B section 3.1.5 steps
#	  2 through 4
#
# The matrix generated in step 1 is now processed column-wise to generate
# the entropy value for each column. This implies we calculate the
# entropy value for all time deltas between all first and 2nd interrupts.
#
maxval=$($EXEC $DELTAFILE 2>/dev/null)

#
# Step 3: Calculate SP800-90B sanity test
#
echo "Maximum occurrence: $maxval" > $OUTDIR/sp900-90b-sanity.txt
echo "Anticipated entropy: $EXPECTED_ENTROPY" >> $OUTDIR/sp900-90b-sanity.txt
Rscript --vanilla binominial_distribution.r $maxval $EXPECTED_ENTROPY >> $OUTDIR/sp900-90b-sanity.txt 2>&1
