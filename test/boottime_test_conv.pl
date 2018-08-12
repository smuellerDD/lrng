#!/usr/bin/perl
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
		my @val = split(/\s+/, $line);
		if ($#val < 50) {
			warn "Line with less than 50 lines seen -- ignoring";
			next;
		}
		for(my $i = 1; $i < 50; $i++) {
			my $tmp = $val[$i] - $val[$i - 1];
			# Handle wrap-arounds
			if ($tmp < 0) {
				$tmp += 4294967296;
			}
			print DEST "$tmp ";
		}
		print DEST "\n";
	}

	close(SRC);
        close(DEST);
}
main();
1;

