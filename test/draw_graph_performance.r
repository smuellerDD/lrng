# Plot data from IRQ duration table
#
# 1. create file with output from get_mean.r with the following format:
#
# Collection Size	Mean Software SHA-256	Mean AVX2 SHA-512
# 16	179.728	155.761
# 32	154.453	120.905
# 64	101.263	89.795
# 128	69.286	74.531
# 256	70.959	76.43
# 512	83.127	69.442
# 1024	66.431	55.825
#
# 2. Generate plot: Rscript --vanilla draw_graph_performance.r mean.data
#

args <- commandArgs(trailingOnly = TRUE)

if (length(args) != 1) {
	stop("Invoke with <input file>")
}

file <- args[1]

data <- read.csv(file=file, header=TRUE, sep="\t")

# add measured legacy /dev/random speed
data[,4] <- 97

svg("mean_duration_one_irq.svg", width=8, height=5, pointsize=10)

# print software
plot(data[,1], data[,2], type="b", col="red", main="Mean Duration in Cycles for one IRQ", pch=19, xlab="LRNG collection size", ylab="Mean Duration in Cycles for one IRQ", ylim=c(min(data[,2:3]), max(data[,2:3])))

# print AVX2
lines(data[,1], data[,3], pch=19, col="blue", type="b", lty=1)

# print legacy /dev/random
lines(data[,1], data[,4], col="gray", type="l", lty=1)

legend(650, 160, legend=c("Software SHA-256", "AVX SHA-512", "Legacy /dev/random"), col=c("red", "blue", "gray"), lty=1)

