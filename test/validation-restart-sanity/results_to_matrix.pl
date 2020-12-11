#!/usr/bin/perl

#
# Calculate the delta of values and output them in one line
#
# The expected input is a file containing absolute time stamps in one
# column
#

use strict;

sub main()
{

        if($#ARGV != 1)
        {
                die "provide input file name, output file name $#ARGV";
        }
        my $file = $ARGV[0];
        my $outfile = $ARGV[1];
	open(SRC, "<$file") or die "Cannot open file $file: $!";
	open(DEST, ">>$outfile") or die "Cannot open file $outfile: $!";

	while(<SRC>)
        {
		my $line = $_;
		chomp($line);

		print DEST "$line ";
	}

	print DEST "\n";

	close(DEST);
}
main();
1;

