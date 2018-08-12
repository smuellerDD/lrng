#!/usr/bin/perl
#
# Execution of Test procedure A as defined in AIS20/31 section 2.4.4.1
#
# Written by Stephan Mueller <Stephan.Mueller@atsec.com>
# 	Copyright (C) atsec information security GmbH, 2012
#
# Generate at least 5 MB of random data:
# dd if=/dev/urandom of=random.data bs=1024 count=5000
#
# Invoke the script with the generated data.
#
use strict;

#########################################################
# Helper
#########################################################

#
# Read and slice file into wordsize chunks
# $1 file
# $2 number of words
# $3 wordsize in bits
sub slice($$$)
{
	my $file = shift;
	my $words = shift;
	my $wordsize = shift;

	my $reqsize = $words * int($wordsize/8 + 1);
	my ($dev,$ino,$mode,$nlink,$uid,$gid,$rdev,$size,
	$atime,$mtime,$ctime,$blksize,$blocks) = stat($file);

	if($size < $reqsize)
	{
		die "File $file contains $size bytes of data but $reqsize bytes are required."
	}

	$/ = \($reqsize);

	open(FH, "<$file") or die "Cannot open file $file: $!";
	binmode FH;
	my $tmp = unpack("B*", <FH>);
	close(FH);

	my @rand=();
	my $offset=0;
	while($offset <= length($tmp))
	{
		push(@rand, substr($tmp, $offset, $wordsize));
		$offset = $offset + $wordsize;
	}
	return(@rand);
}

sub step2(@)
{
	my @rand =@_;

	my @newrand;

	# we throw away the last array member as we do not know whether it is
	# full
	for(my $i=0; $i < $#rand -1 ; $i++)
	{
		for (my $j=0; $j<length($rand[0]); $j++)
		{
			$newrand[$j]= $newrand[$j] . substr($rand[$i], $j, 1);
		}
	}

	if((length($rand[0]) != ($#newrand+1)) ||
	   (length($newrand[0]) != ($#rand-1)))
	{
		warn length($rand[0]);
		warn ($#newrand+1);
		warn length($newrand[0]);
		warn ($#rand-1);
		die "Conversion failed";
	}
	return(@newrand);
}

sub checkres($$)
{
	my $test = shift;
	my $res = shift;
	if($res eq "")
	{
		print("Test $test passed\n");
		return(0);
	}
	else
	{
		print("Test $test failed\n");
		print("Failure code:$res\n");
		return(1);
	}
}

sub XOR($$)
{
	my $val1 = shift;
	my $val2 = shift;
	if ($val1 != $val2)
	{
		return(1);
	}
	else
	{
		return(0);
	}
}

#########################################################
# Test definitions
#########################################################

sub T0(@)
{
	my @rand =@_;

	my $res = "";

	# disjoint means that strings do not match with each other
	my $offset = 0;
	while($offset <= $#rand)
	{
		# there is no need to check already checked combinations
		# as the check of A == B is identical to B == A
		# That means, we are only forward looking: assume you have
		# a number array of:
		#
		# A B C D E
		#
		# The loops check:
		# 1. loop: A with every member right of A
		# 2. loop: B with every member right of B (excluding A == B)
		#    which was already checked
		# 3. loop: C with every member right of C (excluding A == C and
		#    B == C which was already checked)
		# and so on
		#
		# We ensure that we do not check a number with itself (i.e.
		# A == A is excluded)
		my $compval=$offset+1;
		while ($compval <= $#rand)
		{
			if ($rand[$offset] eq $rand[$compval])
			{
				$res = $res . " $offset/$compval";
			}
			$compval++;
		}
		$offset++;
	}
	return(checkres("T0", $res));
}

sub T1(@)
{
	my @rand = @_;
	my $res = "";

	# according to AIS 20/31
	my $max = 10346;
	my $min = 9654;

	for(my $i=0; $i<=$#rand; $i++)
	{
		my @bits = split(//,$rand[$i]);
		my $sum = 0;
		# we cut off at 20000 as the test requires, discarding the
		# rest
		foreach(my $j=0; $j<20000; $j++)
		{
			$sum = $sum + $bits[$j];
		}
		if ($sum < $min || $sum > $max)
		{
			$res = $res . " $i($sum)";
		}
	}
	return(checkres("T1", $res));
}

sub T2(@)
{
	my @rand = @_;
	my $res = "";

	# according to AIS 20/31
	my $min = 1.03;
	my $max = 57.4;

	for(my $i=0; $i<=$#rand; $i++)
	{
		my @bits = split(//,$rand[$i]);
		my @arr;
		foreach(my $j=0; $j<5000; $j++)
		{
			my $c = 8*$bits[4*$j-3] + 4*$bits[4*$j-2] + 2*$bits[4*$j-1] + $bits[4*$j];
			$arr[$c]++;
		}

		my $sum = 0;
		#this logic is taken from BSI test
		for(my $j=0; $j<16; $j++) { $sum = $sum + $arr[$j]**2; }
		my $T = (16/5000)*$sum-5000;

		if ($T < $min || $T > $max)
		{
			$res = $res . " $i($T)";
		}
	}
	return(checkres("T2", $res));
}

sub T3(@)
{
	my @rand = @_;
	my $res = "";

	my @lower = (2267,1079,502,223,90,90);
	my @upper = (2733,1421,748,402,223,223);

	for(my $i=0; $i<=$#rand; $i++)
	{
		my @bits = split(//,$rand[$i]);
		my @run0 = ();
		my @run1 = ();

		my $run = 0;
		for(my $j=1; $j<20000; $j++)
		{
			if($bits[$j-1] == $bits[$j])
		       	{
			       $run++;
			}
			else
			{
				if($run>5) { $run = 5; }
				if($bits[$j-1] == 1)
				{
					$run1[$run]++;
				}
				else
				{
					$run0[$run]++;
				}
				$run=0;
			}
		}
		if($run>5) { $run = 5; }
		if($bits[19999] == 1)
		{
			$run1[$run]++;
		}
		else
		{
			$run0[$run]++;
		}

		for(my $j=0; $j <=5; $j++)
		{
			if ($run0[$j] < $lower[$j] || $run0[$j] > $upper[$j])
			{
				$res = $res . " $i($j run0($run0[$j]))";
			}
			if ($run1[$j] < $lower[$j] || $run1[$j] > $upper[$j])
			{
				$res = $res . " $i($j run1($run1[$j]))";
			}
		}

	}
	return(checkres("T3", $res));
}

sub T4(@)
{
	my @rand = @_;
	my $res = "";

	for(my $i=0; $i<=$#rand; $i++)
	{
		my @bits = split(//,$rand[$i]);

		my $run = 1;
		for(my $j=1; $j<20000; $j++)
		{
			if ($bits[$j-1]==$bits[$j])
			{
				$run++;
				if($run >= 34)
				{
					$res = $res . " $i($j)";
				}
			}
			else
			{
				$run = 1;
			}
		}
	}
	return(checkres("T4", $res));

}

sub T5(@)
{
	my @rand = @_;
	my $res = "";

		# according to AIS 20/31
	my $min = 2326;
	my $max = 2674;

	for(my $i=0; $i<=$#rand; $i++)
	{
		my @bits = split(//,$rand[$i]);

		my $taumax = 0;
		for(my $tau = 1; $tau <= 5000; $tau++)
		{
			my $sum = 0;
			for (my $j=0; $j<5000; $j++)
			{
				$sum = $sum + XOR($bits[$j], $bits[$j+$tau]);
			}
			my $tmp = abs($sum-2500);
			if($tmp > $taumax)
			{
				$taumax = $tmp;
			}
		}

		my $sum = 0;
		for (my $j=10000; $j<15000; $j++)
		{
			$sum = $sum + XOR($bits[$j], $bits[$j+$taumax]);
		}
		if($sum < $min || $sum > $max)
		{
			$res = $res . " $i(tau $taumax sum $sum)";
		}
	}
	return(checkres("T5", $res));
	
}
#########################################################
#########################################################
sub main()
{
	if (! -f $ARGV[0])
	{
		die "Invocation: $0 <filename>  whereas the file contains at least 2**16 * 48 Bit of random numbers";
	}

	my @rand_T0 = slice($ARGV[0], 2**16, 48);
	my @rand_T1 = slice($ARGV[0], 20000, 257);
	my @newrand = step2(@rand_T1);

	# tests
	my $res = 0;
	$res = T0(@rand_T0);
	$res = $res + T1(@newrand);
	$res = $res + T2(@newrand);
	$res = $res + T3(@newrand);
	$res = $res + T4(@newrand);
	$res = $res + T5(@newrand);

	exit($res);
}

main;
1;
