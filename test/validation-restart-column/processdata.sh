#!/bin/bash
#
# Process the entropy data

############################################################
# Configuration values                                     #
############################################################

# point to the directory that contains the results from
# the entropy collection
if [ ! -z "$1" ]
then
	RESULTSDIR="$1"
else
	RESULTSDIR="../results-measurements"
fi

# Directory that shall hold the final result data
OUTDIR="../results-analysis-restart-column"

# Number of test samples to be processed
NUMSAMPLES=1000

# point to the min entropy tool
TEST="../SP800-90B_EntropyAssessment/cpp/ea_non_iid"

############################################################
# Code only after this line -- do not change               #
############################################################

#############################
# Preparation
#############################
INPUT="$RESULTSDIR/lrng_raw_noise_restart*.data"
DELTAFILE="$OUTDIR/restart-consolidated"

EXEC="extractlsb"
CFILE="extractlsb.c"
EXT4BIT="4bitout"
EXT8BIT="8bitout"

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

trap "rm -f $EXEC; rm -f  $DELTAFILE $DELTAFILE.$EXT4BIT* $DELTAFILE.$EXT8BIT* $DELTAFILE.minentropy_?bit_word*; exit 0" 0 1 2 3 15

rm -f $EXEC
gcc -Wall -Wextra -pedantic -o $EXEC $CFILE

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

	./boottime_test_conv.pl $i $DELTAFILE
done

#
# Step 2: Get the AIS20/31 min entropy and Shannon Entropy
#
# The matrix generated in step 1 is now processed column-wise to generate
# the entropy value for each column. This implies we calculate the
# entropy value for all time deltas between all first and 2nd interrupts.
#
R --no-save --args "$DELTAFILE" < boottime_test_dist.r
mv *.csv $OUTDIR

#
# Step 3: Calculate SP800-90B
#
# Just like in step 2, we calculate the entropy column-wise
#
processing()
{
	local file=$1

	j=0;
	while [ $j -lt $NUMSAMPLES ]
	do
		if [ ! -s $file.$EXT4BIT.$j ]; then
			echo "Process 4 bit word $j"
			./$EXEC $file ${file}.$EXT4BIT.$j $j 4
		else
			echo "Skipping 4 bit word $j (file exists)"
		fi

		if [ ! -s $file.$EXT8BIT.$j ]; then
			echo "Process 8 bit word $j"
			./$EXEC $file $file.$EXT8BIT.$j $j 8
		else
			echo "Skipping 8 bit word $j (file exists)"
		fi

		if [ ! -s $file.minentropy_4bit_word$j ]; then
			$TEST $file.$EXT4BIT.$j 4 -i -a -v > $file.minentropy_4bit_word$j
		else
			echo "Skipping $file.minentropy_4bit_word$j (file exists)"
		fi

		if [ ! -s $file.minentropy_8bit_word$j ]; then
			$TEST $file.$EXT8BIT.$j 8 -i -a -v > $file.minentropy_8bit_word$j
		else
			echo "Skipping $file.minentropy_8bit_word$j (file exists)"
		fi

		j=$(($j+1))
	done

}

processing "$DELTAFILE"

#
# Step 4: Get the SP800-90B Minimum entropy value
#
a=0
echo -e "Time Stamp Position / Time delta\tLowest SP800-90B Minimum Entropy estimate of 4 Bits Width Time Stamp\tLowest SP800-90B Minimum Entropy estimate of 8 Bits Width Time Stamp" > ${DELTAFILE}_min_ent_extracted.csv
while [ $a -lt $NUMSAMPLES ]
do
        low4=$(cat ${DELTAFILE}.minentropy_4bit_word$a | grep min | cut -d":" -f2)
        low8=$(cat ${DELTAFILE}.minentropy_8bit_word$a | grep min | cut -d":" -f2)

        echo -e "$a\t$low4\t$low8" 
        a=$(($a+1))
done >> ${DELTAFILE}_min_ent_extracted.csv

for z in $OUTDIR/*_min_ent_extracted.csv
do
	# minimum value
	IFS="
"
	echo -en "Minimum\t" >> $z
	a=1000
	for i in $(cat $z | awk '{print $2}')
	do
		# Is value nummeric?
		check=$(echo "$i" | grep -E ^\-?[0-9]*\.?[0-9]+$)
		if [ "$check" = '' ];
		then
			continue
		fi

		if [ $(echo "$a>$i" | bc) -eq 1 ]
			then a=$i;
		fi
	done
	echo -ne "$a\t" >> $z

	a=1000
	for i in $(cat $z | awk '{print $3}')
	do
		# Is value nummeric?
		check=$(echo "$i" | grep -E ^\-?[0-9]*\.?[0-9]+$)
		if [ "$check" = '' ];
		then
			continue
		fi

		if [ $(echo "$a>$i" | bc) -eq 1 ]
			then a=$i;
		fi
	done
	echo -e "$a" >> $z

	# maximum value
	echo -en "Maximum\t" >> $z
	a=0
	for i in $(cat $z | awk '{print $2}')
	do
		# Is value nummeric?
		check=$(echo "$i" | grep -E ^\-?[0-9]*\.?[0-9]+$)
		if [ "$check" = '' ];
		then
			continue
		fi

		if [ $(echo "$a>$i" | bc) -eq 0 ]
			then a=$i;
		fi
	done
	echo -ne "$a\t" >> $z

	a=0
	for i in $(cat $z | awk '{print $3}')
	do
		# Is value nummeric?
		check=$(echo "$i" | grep -E ^\-?[0-9]*\.?[0-9]+$)
		if [ "$check" = '' ];
		then
			continue
		fi

		if [ $(echo "$a>$i" | bc) -eq 0 ]
			then a=$i;
		fi
	done
	echo -e "$a" >> $z
done
