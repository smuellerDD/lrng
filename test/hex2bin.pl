#!/usr/bin/perl
#
# Hex string to binary value converter
# Written by Stephan Mueller <smueller@atsec.com>
#
# convert ASCII hex to binary input
# $1 ASCII hex
# return binary representation
sub hex2bin($) {
	my $in = shift;
	my $len = length($in);
	$len = 0 if ($in eq "00");
	return pack("H$len", "$in");
}

sub main() {
	if($#ARGV != 1)
	{
		die "provide input file name, output file name $#ARGV";
	}
	my $file = $ARGV[0];
	my $newfile = $ARGV[1];
	open(SRC, "<$file") or die "Cannot open file $file: $!";
	open(DEST, ">>$newfile") or die "Cannot open file $newfile: $!";

	while(<SRC>)
	{
		my $line = $_;
		chomp($line);
		if ($line =~ /#/) { next; }
		my $out = hex2bin($line);
		print DEST $out;
	}
	close(SRC);
	close(DEST);
}

main();
1;
