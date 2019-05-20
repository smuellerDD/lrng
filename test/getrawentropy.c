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
 * Compile:
 * gcc -Wall -pedantic -Wextra -o getrawentropy getrawentropy.c
 */

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/random.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RAWENTROPY_SAMPLES	128

struct opts {
	size_t samples;
};

static int getrawentropy(struct opts *opts)
{
#define BUFFER_SIZE		(RAWENTROPY_SAMPLES * sizeof(uint32_t))
	uint32_t requested = opts->samples * sizeof(uint32_t);
	uint8_t *buffer_p, buffer[BUFFER_SIZE];
	int ret;

	while (requested) {
		unsigned int i;
		unsigned int gather = ((BUFFER_SIZE > requested) ?
				       requested : BUFFER_SIZE);

		buffer_p = buffer;

		ret = getrandom(buffer_p, gather, 0x0010);
		if (ret < 0) {
			ret = -errno;
			goto out;
		}

		for (i = 0; i < ret / (sizeof(uint32_t)); i++) {
			uint32_t val;

			memcpy(&val, buffer_p, sizeof(uint32_t));
			printf("%u\n", val);
			buffer_p += sizeof(uint32_t);
		}

		requested -= ret;
	}

	ret = 0;

out:
	return ret;
}

int main(int argc, char *argv[])
{
	struct opts opts;
	int c = 0;

	opts.samples = RAWENTROPY_SAMPLES;

	while (1)
	{
		int opt_index = 0;
		static struct option options[] =
		{
			{"samples", 1, 0, 's'},
			{0, 0, 0, 0}
		};
		c = getopt_long(argc, argv, "s:", options, &opt_index);
		if(-1 == c)
			break;
		switch (c)
		{
			case 's':
				opts.samples = strtoul(optarg, NULL, 10);
				if (opts.samples == ULONG_MAX)
					return -EINVAL;
				break;
			default:
				return -EINVAL;
		}
	}

	return getrawentropy(&opts);
}
