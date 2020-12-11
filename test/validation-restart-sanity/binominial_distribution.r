args <- commandArgs(trailingOnly = TRUE)

if (length(args) != 2) {
	stop("Invoke with <max> <H>")
}

max <- as.numeric(args[1])
H <- as.numeric(args[2])
result <- 1 - pbinom(max, size=1000, prob=2^(-H))
if (result < 0.000005) {
	message("Sanity test fail: ", result)
} else {
	message("Sanity test pass: ", result)
}
