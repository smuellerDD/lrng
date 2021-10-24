source("formatting.r")

args <- commandArgs(trailingOnly = TRUE)
src <- args[1]
out <- args[2]

rawdata <- scan(src, what=integer())
rawdata <- rawdata[rawdata < 256]

stat <- statsmatrix(rawdata)

#Histogram and curve over histogram
filename <- sprintf("%s.svg",out)
svg(filename, width=8, height=5, pointsize=10)
hist(rawdata, seq(min(rawdata)-1,max(rawdata)+1,1), main='Histogam Raw Time Stamps', prob=TRUE, xlab=stat, ylab="Relative Frequency", right=FALSE, xlim=range(0,max(rawdata)))
lines(density(rawdata, bw=1), col=1)
#Mean value
abline(v=mean(rawdata), col = "red", lwd = 1)
#Median value
abline(v=median(rawdata), col = "blue", lwd = 1)
# 25% Percentile
abline(v=quantile(rawdata, probs=0.25), col = "green", lwd = 1)
# 75% Percentile
abline(v=quantile(rawdata, probs=0.75), col = "green", lwd = 1)
dev.off()

