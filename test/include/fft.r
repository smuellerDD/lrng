#
# Calculate and plot a fast fourier transformation
#

# Gets the frequencies returned by the FFT function
getFFTFreqs <- function(Nyq.Freq, data)
{
	if ((length(data) %% 2) == 1) # Odd number of samples
	{
		FFTFreqs <- c(seq(0, Nyq.Freq, length.out=(length(data)+1)/2), seq(-Nyq.Freq, 0, length.out=(length(data)-1)/2))
	}
	else # Even number
        {
		FFTFreqs <- c(seq(0, Nyq.Freq, length.out=length(data)/2), seq(-Nyq.Freq, 0, length.out=length(data)/2))
	}

	return (FFTFreqs)
}

# FFT plot
# Params:
# x,y -> the data for which we want to plot the FFT 
# samplingFreq -> the sampling frequency
# cutoff -> cut off all FFT results below it
# dst -> file to store graph in
# shadeNyq -> if true the region in [0;Nyquist frequency] will be shaded
# showPeriod -> if true the period will be shown on the top
# Returns a list with:
# freq -> the frequencies
# FFT -> the FFT values
# modFFT -> the modulus of the FFT
plotFFT <- function(x, y, samplingFreq, cutoff, dst = "fft.png", shadeNyq=TRUE, showPeriod = TRUE)
{
	Nyq.Freq <- samplingFreq/2
	FFTFreqs <- getFFTFreqs(Nyq.Freq, y)

	FFT <- fft(y)
	modFFT <- Mod(FFT)
	FFTdata <- cbind(FFTFreqs, modFFT)
	png(dst, width=1800, height=900)
	FFTdata2 <- FFTdata[1:nrow(FFTdata)/2,]
	# only print x data > cutoff
	if (cutoff > 0) {
		FFTdata2 <- FFTdata2[FFTdata2[,1]>cutoff,]
	}
	#svg(dst, width=8, height=5, pointsize=10)
	if (cutoff > 0) {
		str <- sprintf("Fast Fourier Transformation with cutoff at %f", cutoff)
	} else {
		str <- "Fast Fourier Transformation without cutoff"
	}
	plot(FFTdata2, t="l", pch=20, lwd=2, cex=0.8, main=str, xlab="Frequency (Hz)", ylab="Power")
	if (showPeriod == TRUE)
	{
		# Period axis on top
		a <- axis(3, lty=0, labels=FALSE)
		axis(3, cex.axis=0.6, labels=format(1/a, digits=2), at=a)
	}
	if (shadeNyq == TRUE)
	{
		# Gray out lower frequencies
		rect(0, 0, 2/max(x), max(FFTdata[,2])*2, col="gray", density=30)
	}
	dev.off()

	ret <- list("freq"=FFTFreqs, "FFT"=FFT, "modFFT"=modFFT)
	return (ret)
}

# Example
# A sum of 3 sine waves + noise
#x <- seq(0, 8*pi, 0.01)
#sine <- sin(2*pi*5*x) + 0.5 * sin(2*pi*12*x) + 0.1*sin(2*pi*20*x) + 1.5*runif(length(x))
#par(mfrow=c(2,1))
#plot(x, sine, "l")
#res <- plotFFT(x, sine, 100, 0)
