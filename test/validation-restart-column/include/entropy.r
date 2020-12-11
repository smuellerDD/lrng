#
# Calculation of the Shannon Entropy over array p
#
shannon.entropy <- function(p)
{
#        if (min(p) < 0 || sum(p) <= 0)
#                return(NA)
        p.norm <- table(p)/length(p)
        -sum(log2(p.norm)*p.norm)
}

# See "Renyi's Entropy, Divergence and Their Noparametric Estimators" by
# Dongxin Xu and Deniz Erdogmns
# Theorem says that min entropy is > 1/2 renyi2.entropy
#renyi2.entropy < function(p)
#{
#	p.norm <- table(p)/length(p)
#	-log2(sum(p.norm^2))
#}
#
# Calculation of the lower boundary of the Shannon Entropy over array p
#
min.entropy <- function(p)
{
#        if (min(p) < 0 || sum(p) <= 0)
#                return(NA)
        p.norm <- table(p)/length(p)
        min(-log2(p.norm))
}

#
# Calculation of upper boundary of the Shannon Entropy provided the events
# are stochastical independent
#
# p - probability that bit not set
# q - probability that bit is set
#
max.entropy <- function(p, q)
{
	if (min(p) < 0 || sum(p) <= 0)
		return(NA)
	if (min(q) < 0 || sum(q) <= 0)
		return(NA)
	-sum(log2(p)*p + log2(q)*q)
}

