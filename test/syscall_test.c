/*
 * Copyright (C) 2018, Stephan Mueller <smueller@chronox.de>
 *
 * License: see LICENSE file in root directory
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

/*
 * Test all invocation types of the getrandom(2) system call.
 */

/*
 * Compile test application:
 * gcc -DTEST -Wall -pedantic -Wextra -o syscall_test syscall_test.c
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/random.h>

#define GRND_INSECURE	0x0004
#define GRND_TRUERANDOM	0x0008

static inline ssize_t __getrandom(uint8_t *buffer, size_t bufferlen,
				  unsigned int flags)
{
	ssize_t ret, totallen = 0;

	if (bufferlen > INT_MAX)
		return -EINVAL;

	do {
		ret = getrandom(buffer, bufferlen, flags);
		if (ret > 0) {
			bufferlen -= ret;
			buffer += ret;
			totallen += ret;
		}
	} while ((ret > 0 || errno == EINTR) && bufferlen);

	return ((ret < 0) ? -errno : totallen);
}

static inline ssize_t getrandom_urandom(uint8_t *buffer, size_t bufferlen)
{
	return __getrandom(buffer, bufferlen, 0);
}

static inline ssize_t getrandom_random(uint8_t *buffer, size_t bufferlen)
{
	return __getrandom(buffer, bufferlen, GRND_RANDOM);
}

static inline ssize_t getrandom_insecure(uint8_t *buffer, size_t bufferlen)
{
	return __getrandom(buffer, bufferlen, GRND_INSECURE);
}

static inline ssize_t getrandom_trng(uint8_t *buffer, size_t bufferlen)
{
	return __getrandom(buffer, bufferlen, GRND_TRUERANDOM);
}

static inline ssize_t getrandom_trng_nonblock(uint8_t *buffer, size_t bufferlen)
{
	return __getrandom(buffer, bufferlen, GRND_TRUERANDOM | GRND_NONBLOCK);
}

#ifdef TEST
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/*
 * This is x86 specific to reduce the CPU jitter
 */
static inline void cpusetup(void)
{
#ifdef __X8664___
	asm volatile("cpuid"::"a" (0), "c" (0): "memory");
	asm volatile("cpuid"::"a" (0), "c" (0): "memory");
	asm volatile("cpuid"::"a" (0), "c" (0): "memory");
#endif
}

static inline uint64_t ts2u64(struct timespec *ts)
{
	return (uint64_t)((uint64_t)ts->tv_sec * 1000000000 +
			  (uint64_t)ts->tv_nsec);
}

static inline void get_nstime(struct timespec *ts)
{
	clock_gettime(CLOCK_REALTIME, ts);
}

static inline void start_time(struct timespec *ts)
{
	cpusetup();
	get_nstime(ts);
}

static inline void end_time(struct timespec *ts)
{
	get_nstime(ts);
}


/*
 * Convert an integer value into a string value that displays the integer
 * in either bytes, kB, or MB
 *
 * @bytes value to convert -- input
 * @str already allocated buffer for converted string -- output
 * @strlen size of str
 */
static void bytes2string(uint64_t bytes, uint64_t ns, char *str, size_t strlen)
{
	long double seconds = ns / 1000000000;
	long double bytes_per_second = bytes / seconds;

	if (1000000000 < bytes_per_second) {
		bytes_per_second /= 1000000000;
		snprintf(str, strlen, "%Lf GB", bytes_per_second);
		return;

	} else if (1000000 < bytes) {
		bytes_per_second /= 1000000;
		snprintf(str, strlen, "%Lf MB", bytes_per_second);
		return;
	} else if (1000 < bytes) {
		bytes_per_second /= 1000000;
		snprintf(str, strlen, "%Lf kB", bytes_per_second);
		return;
	}
	snprintf(str, strlen, "%Lf B", bytes_per_second);
	str[strlen] = '\0';
}

static int print_status(size_t buflen,
			uint64_t processed_bytes, uint64_t totaltime)
{
#define VALLEN 20
	char byteseconds[VALLEN + 1];

	memset(byteseconds, 0, sizeof(byteseconds));
	bytes2string(processed_bytes, totaltime, byteseconds, (VALLEN + 1));
	printf("%8lu bytes | %*s/s | %12lu bytes |%12lu ns\n", buflen,
	       VALLEN, byteseconds, processed_bytes, totaltime);

	return 0;
}

int main(int argc, char *argv[])
{
#define MAXLEN	65536
	uint64_t totaltime = 0;
	uint8_t buffer[MAXLEN];
	size_t bufferlen = MAXLEN, tmp;
	struct timespec start, end;
	int c = 0;
	ssize_t ret = 0;
	ssize_t (*rnd)(uint8_t *buffer, size_t bufferlen);

	while (1)
	{
		int opt_index = 0;
		static struct option options[] =
		{
			{"urandom", 0, 0, 'u'},
			{"random", 0, 0, 'r'},
			{"insecure", 0, 0, 'i'},
			{"trng", 0, 0, 't'},
			{"trng-nonblock", 0, 0, 'n'},
			{"buflen", 1, 0, 'b'},
			{0, 0, 0, 0}
		};
		c = getopt_long(argc, argv, "uritnb:", options, &opt_index);
		if (-1 == c)
			break;
		switch (c)
		{
			case 'u':
				rnd = &getrandom_urandom;
				break;
			case 'r':
				rnd = &getrandom_random;
				break;
			case 'i':
				rnd = &getrandom_insecure;
				break;
			case 't':
				rnd = &getrandom_trng;
				break;
			case 'n':
				rnd = &getrandom_trng_nonblock;
				break;
			case 'b':
				tmp = strtoul(optarg, NULL, 10);
				if (tmp >= MAXLEN)
					return -EINVAL;
				bufferlen = tmp;
				break;
			default:
				return -EINVAL;
		}
	}

	start_time(&start);
	ret = rnd(buffer, bufferlen);
	end_time(&end);

	if (ret < 0)
		goto out;
	totaltime += (ts2u64(&end) - ts2u64(&start));

	ret = print_status(ret, ret, totaltime);

out:
	return ret;
}
#endif	/* TEST */
