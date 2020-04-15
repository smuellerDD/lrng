/*
 * Copyright (C) 2019, Stephan Mueller <smueller@chronox.de>
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
#include <limits.h>
#include <linux/random.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef GRND_INSECURE
# define GRND_INSECURE	0x0004
#endif

static inline ssize_t __getrandom(uint8_t *buffer, size_t bufferlen,
				  unsigned int flags)
{
	ssize_t ret, totallen = 0;

	if (bufferlen > INT_MAX)
		return -EINVAL;

	do {
#ifdef USE_GLIBC_GETRANDOM
		ret = getrandom(buffer, bufferlen, flags);
#else
		ret = syscall(__NR_getrandom, buffer, bufferlen, flags);
#endif
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

static size_t read_entropy_avail(int fd)
{
	ssize_t data = 0;
	char buf[11] = { 0 };
	size_t entropy = 0;

	data = read(fd, buf, sizeof(buf));
	lseek(fd, 0, SEEK_SET);

	if (data < 0)
		return 0;
	if (data == 0)
		return 0;

	entropy = strtoul(buf, NULL, 10);
	if (entropy > INT_MAX)
		return 0;

	return entropy;
}

static ssize_t getrandom_ntg1(uint8_t *buffer, size_t bufferlen,
			      unsigned int flags)
{
	struct stat statdata;
	size_t avail_entropy;
	ssize_t ret;
	static int lrng_type = 1;
	int fd = 1;

	(void)flags;

	if (lrng_type > 0)
		lrng_type = stat("/proc/lrng_type", &statdata);

	/*
	 * One request cannot be larger than the security strength and thus
	 * the reseed size of the RNG.
	 */
	if (bufferlen > 32)
		return -EINVAL;

	/*
	 * Read the amount of available entropy and reject the call if
	 * we cannot satisfy the request.
	 *
	 * Note, instead of an error we could poll this file to wait until
	 * sufficient data is available.
	 */
	fd = open("/proc/sys/kernel/random/entropy_avail", O_RDONLY);
	if (fd < 0) {
		ret = -errno;
		goto out;
	}

	avail_entropy = read_entropy_avail(fd) >> 3;

	/*
	 * Require at least twice the amount of entropy to be reseeded
	 * as a safety measure.
	 */
	if ((bufferlen * 2) > avail_entropy) {
		ret = -E2BIG;
		goto out;
	}

	close(fd);
	fd = open("/dev/random", O_RDWR);
	if (fd < 0) {
		ret = -errno;
		goto out;
	}

	/* Triggering a reseed operation */
	if (lrng_type >= 0) {
		/* Unprivileged operation */
		ret = write(fd, "0", 1);
		if (ret < 0) {
			ret = -errno;
			goto out;
		}
		if (ret != 1) {
			ret = -EFAULT;
			goto out;
		}
	} else {
		/* Requiring CAP_SYS_ADMIN */
		if (ioctl(fd, RNDRESEEDCRNG) < 0) {
			ret = -errno;
			goto out;
		}
	}

	/*
	 * Read random data from a freshly seeded DRNG.
	 *
	 * Note, there is a race: the reseed and the gathering of random
	 * data is a non-atomic operation. This means that other processes
	 * could gather random data between the reseed trigger and our read
	 * operation here. This race implies that this call here does not
	 * gather the first random data after a reseed, which are for sure
	 * fully NTG.1 compliant data, but the second or third block of
	 * random data after a reseed.
	 *
	 * Note 2, there is a race: In case of the LRNG and the possible
	 * presence of an independent atomic DRNG, the atomic DRNG may be
	 * reseeded with the first 32 random bytes of the DRNG we pull from.
	 */
	ret = getrandom_urandom(buffer, bufferlen);

out:
	if (fd >= 0)
		close(fd);
	return ret;
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

static inline ssize_t __getrandom_ntg1(uint8_t *buffer, size_t bufferlen)
{
	return getrandom_ntg1(buffer, bufferlen, 0);
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
			{"insecure", 0, 0, 'i'},
			{"random", 0, 0, 'r'},
			{"ntg1", 0, 0, 'n'},
			{"buflen", 1, 0, 'b'},
			{0, 0, 0, 0}
		};
		c = getopt_long(argc, argv, "uirnb:", options, &opt_index);
		if (c == -1)
			break;
		switch (c)
		{
			case 'u':
				rnd = &getrandom_urandom;
				break;
			case 'i':
				rnd = &getrandom_insecure;
				break;
			case 'r':
				rnd = &getrandom_random;
				break;
			case 'n':
				rnd = &__getrandom_ntg1;
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
	return -ret;
}
#endif	/* TEST */
