source("formatting.r")

args <- commandArgs(trailingOnly = TRUE)
src <- args[1]

rawdata <- scan(src, what=integer(), quiet=TRUE)

# remove errors in observations
rawdata <- rawdata[rawdata < 256]

# Chi-Squared Goodness-of-Fit Test:
# Assume equidistribution over $slots discrete values
#count <- length(rawdata)
obs <- table(rawdata)
slots <- length(obs)
exp <- rep(1/slots, slots)
chisquared <- chisq.test(x = obs, p = exp, simulate.p.value = FALSE)

status <- c(round(quantile(rawdata, probs=0.25, names=FALSE), digits=2), round(median(rawdata), digits=2), round(mean(rawdata), digits=2), round(quantile(rawdata, probs=0.75, names=FALSE), digits=2), round(sd(rawdata), digits=2), round(sd(rawdata)/mean(rawdata), digits=log10(length(rawdata))), round(chisquared$p.value, digits=4), chisquared$parameter)

write.table(matrix(status, nrow=1), file="", row.names=FALSE, col.names=FALSE)

# for i in $(find ../../../test-results/ -name lrng_raw_noise.data) ; do echo -n "$i "; Rscript --vanilla stats.r $i; done
# r <- read.csv("stats_systems", header=FALSE, sep=" ")

