#!/bin/bash
#
# Process the entropy data

############################################################
# Configuration values                                     #
############################################################

# point to the directory that contains the results from the entropy collection
ENTROPYDATA_DIR="../results-measurements"

NONIID_DATA="lrng_raw_noise_restart*.data"

# this is where the resulting data and the entropy analysis will be stored
RESULTS_DIR="../results-analysis-restart"

# location of log file
LOGFILE="$RESULTS_DIR/processdata.log"

# point to the min entropy tool
EATOOL_NONIID="../SP800-90B_EntropyAssessment/cpp/ea_restart"

# specify if you want to compile the extractlsb program in this script
BUILD_EXTRACT="yes"

# specify the list of significant bits and length that you want to analize. 
# Indicate first the mask in hexa format and then the number of 
# bits separated by a colon. 
# The tool generates one set of var and single data files, and the EA results 
# for each element. 
# The mask can have a maximum of 8 bits on, the EA tool only manages samples 
# up to one byte.

# List of masks usually analyzed (4 and 8 LSB) 
MASK_LIST="0F:4 FF:8"

# List used for ARM Cortext A9 and A7 processors
#MASK_LIST="FF:4,8 7F8:4,8"

# Maximum number of entries to be extracted from the original file
MAX_EVENTS=1000

############################################################
# Code only after this line -- do not change               #
############################################################

EXTRACT="extractlsb"
CFILE="extractlsb.c"

if [ ! -d $ENTROPYDATA_DIR ]
then
	echo "ERROR: Directory with raw entropy data $ENTROPYDATA_DIR is missing"
	exit 1
fi

if [ ! -d $RESULTS_DIR ]
then
	mkdir $RESULTS_DIR
	if [ $? -ne 0 ]
	then
		echo "ERROR: Directory with raw entropy data $RESULTS_DIR could not be created"
		exit 1
	fi
fi

if [ ! -f $EA_TOOL ]
then
	echo "ERROR: Path of Entropy Data tool $EA_TOOL is missing"
	exit 1
fi


rm -f $RESULTS_DIR/*.txt $RESULTS_DIR/*.data  $RESULTS_DIR/*.log

trap "make clean; exit " 0 1 2 3 15


if [ "$BUILD_EXTRACT" = "yes" ]
then
	echo "Building $EXTRACT ..."
	make clean
	make
fi

if [ ! -x $EXTRACT ] 
then
	echo "ERROR: Cannot execute $EXTRACT program"
	exit 1
fi

# Step 1: concatenate data from each reboot
filepath="$RESULTS_DIR/concatenated_data"
rm -f $filepath
files=0
for file in $ENTROPYDATA_DIR/$NONIID_DATA
do
	echo "Concatenating recorded entropy data $file" | tee -a $LOGFILE

	cat $file >> $filepath
	files=$((files+1))
done

# Step 2: converting the input data into managable data
events=$((files*MAX_EVENTS))
echo "Converting recorded entropy data $file into different bit output" | tee -a $LOGFILE
for item in $MASK_LIST
do
	mask=${item%:*}
	bits=${item#*:}
		
	./$EXTRACT $filepath $filepath.${mask}bitout.data $events $mask 2>&1 | tee -a $LOGFILE
done

# Step 3: analyze with tool
echo "" | tee -a $LOGFILE
echo "Extraction finished. Now analyzing entropy for noise source ..." | tee -a $LOGFILE
echo "" | tee -a $LOGFILE

for item in $MASK_LIST
do
	mask=${item%:*}
	bits_field=${item#*:}
	bits_list=`echo $bits_field | sed -e "s/,/ /g"`

	infile=$filepath.${mask}bitout.data

	for bits in $bits_list
	do
		outfile=${filepath}.minentropy_${mask}_${bits}bits.txt
		$EATOOL_NONIID -n -v $infile ${bits} 1 > $outfile
	done
done
