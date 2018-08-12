source("include/formatting.r")
source("include/bits.r")
source("include/entropy.r")
source("include/fft.r")

args <- commandArgs(trailingOnly = TRUE)
src <- args[1]

filedata <- matrix(scan(file=src, what=numeric()), ncol=49, byrow=TRUE)

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
data <- filedata[,1]
printdata(1, data, max(data), "timedelta-1st-delta")
quant<-round(quantile(data, probs=0.99))
data <- data[data<quant]
printdata(1, data, max(data), "timedelta-1st-delta-99quartile")

data <- filedata[,2]
printdata(1, data, max(data), "timedelta-2nd-delta")
quant<-round(quantile(data, probs=0.99))
data <- data[data<quant]
printdata(1, data, max(data), "timedelta-2nd-delta-99quartile")

data <- filedata[,3]
printdata(1, data, max(data), "timedelta-3rd-delta")
quant<-round(quantile(data, probs=0.99))
data <- data[data<quant]
printdata(1, data, max(data), "timedelta-3rd-delta-99quartile")

data <- filedata[,10]
printdata(1, data, max(data), "timedelta-10th-delta")
quant<-round(quantile(data, probs=0.99))
data <- data[data<quant]
printdata(1, data, max(data), "timedelta-10th-delta-99quartile")

write.table(apply(filedata, 2, mean), file="boottime-test-timedelta-mean.csv", row.names=FALSE, col.names=FALSE)
write.table(apply(filedata, 2, sd), file="boottime-test-timedelta-standardderivation.csv", row.names=FALSE, col.names=FALSE)
write.table(apply(filedata, 2, min.entropy), file="boottime-test-timedelta-minentropy.csv", row.names=FALSE, col.names=FALSE)
write.table(apply(filedata, 2, shannon.entropy), file="boottime-test-timedelta-shannonentropy.csv", row.names=FALSE, col.names=FALSE)

