#
# format statistics
statsmatrix <- function(rawdata)
{
	stat <- sprintf("Min: %s - 1st Qu: %s - Median: %s - Mean: %s\n3rd Qu: %s - Max: %s - Sigma: %s - Var Coeff: %s", round(min(rawdata), digits=2), round(quantile(rawdata, probs=0.25), digits=2), round(median(rawdata), digits=2), round(mean(rawdata), digits=2), round(quantile(rawdata, probs=0.75), digits=2), round(max(rawdata), digits=2), round(sd(rawdata), digits=2), round(sd(rawdata)/mean(rawdata), digits=log10(length(rawdata))))

	return(stat)
}

stats <- function(delta, absolute, probbitset, probbitnotset)
{
	# cut off zeros and ones which do not add to the maximum entropy and thus
	# prevent any log to be calculated
	probbitnotset <- probbitnotset[probbitnotset<1 & probbitnotset>0]
	probbitset <- probbitset[probbitset<1 & probbitset>0]


	tmp <- sprintf("Upper Threshold %.0f LSB: %.2f -- Shannon Entropy: %.2f -- Lower Threshold: %.2f\nSamples: %d -- 1st Qu: %.2f -- Median: %.2f -- Mean: %.2f -- 3rd Qu: %.2f", length(probbitnotset), max.entropy(probbitnotset, probbitset), shannon.entropy(delta), min.entropy(delta), length(absolute), quantile(delta, probs=0.25), median(delta), mean(delta), quantile(delta, probs=0.75))

	return(tmp)
}

max.occur <- function(p)
{
	unique <- unique(sort(p))
	max <- 0
	val <- 0
	for (i  in unique)
	{
		tmp <- sum(p == i)
		if(tmp > max)
		{
			max <- tmp
			val <- i
		}
	}
	sprintf("Max Wert: %d -- Max Anzahl: %d", val, max)
}

statsentropy <- function(absolute)
{
	stat <- sprintf("Shannon Entropie: %f -- Minimum Entropie %f -- %s -- Gesamtanzahl %d", shannon.entropy(absolute), min.entropy(absolute), max.occur(absolute), length(absolute))
	return(stat)
}

#
# Drawing a histogram
# file name
# file name extension
# delta matrix
# statistics
# main name
drawhistogramdelta <- function(givenname, extension, delta, stat, givenmain)
{
	# 100 columns
	interval <- (max(delta) - min(delta)) / 100

	name <- sprintf("%s-%s.svg", givenname, extension)
	main <- sprintf("%s - %d low bits", givenmain, extension)

	svg(name, width=8, height=5, pointsize=10)
	hist(delta, seq(min(delta)-interval,max(delta)+interval,interval), main=main, prob=FALSE, xlab=stat, ylab="Relative Frequency")
	#Mean value
	abline(v=mean(delta), col = "red", lwd = 1)
	#Median value
	abline(v=median(delta), col = "blue", lwd = 1)
	# 25% Percentile
	abline(v=quantile(delta, probs=0.25), col = "green", lwd = 1)
	# 75% Percentile
	abline(v=quantile(delta, probs=0.75), col = "green", lwd = 1)

	# add a normal distribution that is based on the mean, standard diviation
	# and number of events as a base.
	xx=seq(min(delta), max(delta), length=length(delta))
	yy=dnorm(xx, m=mean(delta), sd=sd(delta))
	lines(xx,yy,lty=2,lwd=1,col="red")

	dev.off()
}

#
# drawing plot
plotdelta <- function(givenname, extension, delta, stat, givenmain, type)
{
	name <- sprintf("%s-%s.svg", givenname, extension)
	main <- sprintf("%s - %d low bits", givenmain, extension)

	svg(name, width=8, height=5, pointsize=10)

	# 1000 columns max
#	interval <- length(delta)
#	if (interval > 10000)
#	{
#		interval <- (max(delta) - min(delta)) / 10000
#		hist(delta, seq(min(delta)-interval,max(delta)+interval,interval), main=main, prob=FALSE, xlab=stat, ylab="Differenzen", col="1")
#
#	}
#	else
#	{
		plot(delta, main=main, ylab="Deltas", xlab=stat, col="1", type=type)
#	}

	dev.off()
}

# sanity checking: list all variable locations whose bit size is larger
# than the provided value
# returning matrix without offending values
checkparameter <- function(p, q)
{
	if(max(p)> (2^q))
	{
		tmp <- sprintf("WARNING: Unexpected values at location: %s -- removed", which(jiffies>2^q))
		print(tmp)

		p <- p[which(p<2^q)]
	}
	return(p)
}
