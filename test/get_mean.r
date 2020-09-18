# Calculate the mean of the input data
#
# Use this script to obtain the mean of the time duration of IRQ handler
#
# 1. Get data: getrawentropy -s 1000 -f /sys/kernel/debug/lrng_testing/lrng_irq_perf > duration.data
#
# 2. Calculate mean: Rscript --vanilla get_mean.r duration.data
#

args <- commandArgs(trailingOnly = TRUE)

if (length(args) != 1) {
	stop("Invoke with <input file>")
}

file <- args[1]

m <- mean(read.table(file)[,1])

message("Mean value ", m)
