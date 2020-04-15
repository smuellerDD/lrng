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
 * Compile:
 * gcc -Wall -pedantic -Wextra -o speedtest speedtest.c
 */

/*
 * Shall the GLIBC getrandom stub be used (requires GLIBC >= 2.25)
 */
#define USE_GLIBC_GETRANDOM

#ifdef USE_GLIBC_GETRANDOM
#include <sys/random.h>
#else
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

struct opts {
	uint64_t exectime;
	size_t buflen;
};

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

static int print_status(struct opts *opts,
			uint64_t processed_bytes, uint64_t totaltime)
{
#define VALLEN 20
	char byteseconds[VALLEN + 1];

	memset(byteseconds, 0, sizeof(byteseconds));
	bytes2string(processed_bytes, totaltime, byteseconds, (VALLEN + 1));
	printf("%8lu bytes | %*s/s | %12lu bytes |%12lu ns\n", opts->buflen,
	       VALLEN, byteseconds, processed_bytes, totaltime);

	return 0;
}

static int speedtest(struct opts *opts)
{
	uint64_t testduration = 0;
	uint64_t totaltime = 0;
	uint64_t bytes = 0;
	uint64_t nano = 1000000000;
	uint8_t *buffer = malloc(opts->buflen);
	int ret;

	if (!buffer)
		return -ENOMEM;

	testduration = nano * opts->exectime;

	while (totaltime < testduration) {
		struct timespec start;
		struct timespec end;

		start_time(&start);
#ifdef USE_GLIBC_GETRANDOM
		ret = getrandom(buffer, opts->buflen, 0);
#else
		ret = syscall(__NR_getrandom, buffer, opts->buflen, 0);
#endif
		end_time(&end);
		if (ret < 0) {
			ret = -errno;
			goto out;
		}
		totaltime += (ts2u64(&end) - ts2u64(&start));
		bytes += opts->buflen;
	}

	ret = print_status(opts, bytes, totaltime);

out:
	free(buffer);
	return ret;
}

int main(int argc, char *argv[])
{
#define MAXLEN	16
	struct opts opts;
	size_t buflens[MAXLEN];
	unsigned int i, lens = 0;
	int c = 0;

	opts.exectime = 2;
	opts.buflen = 4096;

	while (1)
	{
		int opt_index = 0;
		static struct option options[] =
		{
			{"exectime", 1, 0, 'e'},
			{"buflen", 1, 0, 'b'},
			{0, 0, 0, 0}
		};
		c = getopt_long(argc, argv, "e:b:", options, &opt_index);
		if(-1 == c)
			break;
		switch (c)
		{
			case 'e':
				opts.exectime = strtoul(optarg, NULL, 10);
				if (opts.exectime == ULONG_MAX)
					return -EINVAL;
				break;
			case 'b':
				buflens[lens] = strtoul(optarg, NULL, 10);
				lens++;
				if (lens >= MAXLEN)
					return -EINVAL;
				break;
			default:
				return -EINVAL;
		}
	}

	if (!lens)
		return speedtest(&opts);

	for (i = 0; i < lens; i++) {
		int ret;

		opts.buflen = buflens[i];
		ret = speedtest(&opts);
		if (ret)
			return ret;
	}
	return 0;
}
