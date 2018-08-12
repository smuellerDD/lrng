#
# conversion float to bit
# p value to convert
# q set least significant bits
numToBits64 <- function(p, q)
{
	if(max((p / 2^53)) >1)
	{
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("Interger value > 2^53 seen")
	       print("Bit conversion imprecise!")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       options(scipen=15)
	       print(max(p))
	}
	#intToBits only chews on 31 bit ints, but prints out 32 values :-(
	upper  <- floor(p / 2^62)
	middle <- floor((p - upper * (2^62))/2^31) 
	lower  <- p - upper * (2^62) - middle*(2^31)

	upper  <- apply(as.matrix(upper), 1, intToBits)[1:2,]
	middle <- apply(as.matrix(middle), 1, intToBits)[1:31,]
	lower  <- apply(as.matrix(lower), 1, intToBits)[1:31,]

	upper  <- rev(apply(as.matrix(upper), 1, as.numeric))
	middle <- rev(apply(as.matrix(middle), 1, as.numeric))
	lower  <- rev(apply(as.matrix(lower), 1, as.numeric))

	ret <- c(upper, middle, lower)
	ret[1:(64-q)] <- 0
	return(ret)
}

# same as above, but without significant bits massage
# p is a matrix
# q set least significant bits
numToBits64_array <- function(p)
{
	if(max((p / 2^53)) >1)
	{
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("Interger value > 2^53 seen")
	       print("Bit conversion imprecise!")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       print("WARNING WARNING WARNING WARNING WARNING WARNING")
	       options(scipen=15)
	       print(max(p))
	}
	#intToBits only chews on 31 bit ints, but prints out 32 values :-(
	upper  <- floor(p / 2^62)
	middle <- floor((p - upper * (2^62))/2^31) 
	lower  <- p - upper * (2^62) - middle*(2^31)

	upper  <- apply(as.matrix(upper), 1, intToBits)[1:2,]
	middle <- apply(as.matrix(middle), 1, intToBits)[1:31,]
	lower  <- apply(as.matrix(lower), 1, intToBits)[1:31,]
	
	upper  <- apply(as.matrix(upper), 1, as.numeric)
	middle  <- apply(as.matrix(middle), 1, as.numeric)
	lower  <- apply(as.matrix(lower), 1, as.numeric)

	# reversing order
	upper  <- upper[, ncol(upper):1 ]
	middle <- middle[, ncol(middle):1 ]
	lower  <- lower[, ncol(lower):1 ]

	com <- cbind(upper, middle, lower)
	return(com)
}

#conversion bit to float
# p value to convert
bits64ToNum <-function(p)
{
	return(sum(p * 2^rev(seq(along=p)-1)))
}

bitmaskval <- function(p, q)
{
	return(bits64ToNum(numToBits64(p, q)))
}

# bit mask calculation
# p matrix
# q set least significant bits
bitmask <- function (p, q)
{
	if(q >= 64)
	{
		return(p)
	}
	print(">>>>>>>>>Go and grab some coffee, and then some more<<<<<<<<<<")
	print(">>>>>>>>>Or help writing a native bit manipulation routine for R<<<<<<<")
	mapply(bitmaskval, p, q)
}

# 
# get sum of east significant bits
# p array with values
# q least significant bits
#
bitsumming <- function(p, q)
{
	if(q > 64 || q < 0)
	{
		return(NA)
	}

	com<- numToBits64_array(p)
	sum <- apply(com, 2, sum)
	sum[1:(64-q)] <- 0
	return(sum)
}

bitdelta <- function(p, q)
{
	if(q > 64 || q < 0)
	{
		return(NA)
	}
	com <- numToBits64_array(p)

	bitdelta <- c(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
	oldbits<-com[1,]
	for (i in 2:length(com[,1]))
	{
		bitdelta <- bitdelta + abs(oldbits-com[i,])
		oldbits<-com[i,]
	}
	bitdelta[1:(64-q)] <- 0
	return(bitdelta)
}
