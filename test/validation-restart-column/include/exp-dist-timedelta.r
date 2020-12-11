rawdata <- matrix(ncol=3, nrow = 16384)
rawdata[,1] <- c(256:1279)
rawdata[,2] <- c(256:1279)
rawdata[,3] <- c(256:1279)
rawdata[,1] <- rawdata[,1]*22.96
rawdata[,2] <- rawdata[,2]*1.6

maxseq <- 50000

svg("exp-dist-timedelta.svg", width=8, height=5, pointsize=10)
xx=seq(0, maxseq, rawdata[1,3])
yy=dnorm(xx, m=rawdata[1,1], sd=rawdata[1,2])
yy=yy*rawdata[1,3]/(1000000)
plot(xx,yy, type="l", lwd=1, xlab="Time delta per RNG round", ylab="Relative Frequency", main="Distribution of time delta per RNG round")

for (i in c(2:length(rawdata[,1])))
{
	xx=seq(0, maxseq, rawdata[i,3])
	yy=dnorm(xx, m=rawdata[i,1], sd=rawdata[i,2])
	yy=yy*rawdata[i,3]/(1000000)
	lines(xx,yy,lty=2,lwd=1)
}
dev.off()

svg("exp-dist-timedelta-subset.svg", width=8, height=5, pointsize=10)
xx=seq(0, maxseq, rawdata[1,3])
yy=dnorm(xx, m=rawdata[1,1], sd=rawdata[1,2])
yy=yy*rawdata[1,3]/(1000000)
plot(xx,yy, type="l", lwd=1, xlab="Time delta per RNG round", ylab="Relative Frequency", main="Distribution of time delta per RNG round")

for (i in seq(300, 1200, 200))
{
        xx=seq(0, maxseq, rawdata[i,3])
        yy=dnorm(xx, m=rawdata[i,1], sd=rawdata[i,2])
	yy=yy*rawdata[i,3]/(1000000)
        lines(xx,yy,lty=2,lwd=1)
}
xx=seq(0, maxseq, rawdata[16384,3])
yy=dnorm(xx, m=rawdata[16384,1], sd=rawdata[16384,2])
yy=yy*rawdata[16384,3]/(1000000)
lines(xx,yy,lty=2,lwd=1)
dev.off()
