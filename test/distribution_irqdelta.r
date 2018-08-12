source("include/formatting.r")
source("include/bits.r")
source("include/entropy.r")
source("include/fft.r")

args <- commandArgs(trailingOnly = TRUE)
src <- args[1]

filedata <- matrix(scan(file=src, what=numeric()), ncol=1, byrow=TRUE)

printdata <- function(column, rawdata, lowbits, foldtype)
{

	if (column == 1)
	{
		filename <- sprintf("%s-%s-dist-%s",src, foldtype, lowbits)
		xlab <- "Time delta for two sequential timer values s"
		header <- sprintf("Distribution of time delta in %s up to %s (%s)", src, lowbits, foldtype)
	}

	#stat <- statsmatrix(rawdata)
	stat <- sprintf("Shannon E: %.2f - Min E: %.2f", shannon.entropy(rawdata), min.entropy(rawdata))
	#stat <- sprintf("%s - Shannon E: %.2f - Min E: %.2f", stat, shannon.entropy(rawdata), min.entropy(rawdata))

	#Histogram and curve over histogram
	# divisor is number of columns
	#interval <- (max(rawdata) - min(rawdata)) / 50
	interval <- length(table(rawdata))

	name <- sprintf("%s-hist.svg", filename)
	svg(name, width=8, height=5, pointsize=10)
	#name <- sprintf("%s-hist.pdf", filename)
	#pdf(name, width=8, height=5, pointsize=10)
	hist(rawdata, interval,  main=header, prob=TRUE, xlab=stat, ylab="Relative Frequency")
	#lines(density(rawdata, bw=interval), col=1)
	# unsure which we shall use
	#lines(density(rawdata, bw="nrd"), col=1)
	#Mean value
	abline(v=mean(rawdata), col = "red", lwd = 1)
	#Median value
	abline(v=median(rawdata), col = "blue", lwd = 1)
	# 25% Percentile
	abline(v=quantile(rawdata, probs=0.25), col = "green", lwd = 1)
	# 75% Percentile
	abline(v=quantile(rawdata, probs=0.75), col = "green", lwd = 1)

	# add a normal distribution that is based on the mean, standard diviation
	# and number of events as a base.
	xx=seq(min(rawdata), max(rawdata), length=length(rawdata))
	yy=dnorm(xx, m=mean(rawdata), sd=sd(rawdata))
	lines(xx,yy,lty=2,lwd=1,col="red")

	dev.off()

}

# Raw time stamp
data <- filedata[c(1:5000000),1]
printdata(1, data, max(data), "timestamp")

data <- filedata[,1]
# Time detal
delta <- abs(data[-1] - data[-length(data)])
quant<-round(quantile(delta, probs=0.99))
delta <- delta[delta<quant]
printdata(1, delta, quant, "timedelta")

