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
 * The tool extracts the 4 or 8 LSB of the high-res time stamp and
 * concatenates them to form a binary data stream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

int main(int argc, char *argv[])
{
	FILE *f = NULL;
	char buf[16384];
	int outfd = -1;
	unsigned char out = 0;
	unsigned long column = 0;
	uint32_t bits = 0;
	int err = 0;

	if (argc != 5) {
		printf("Provide input_file output_file column bits\n");
		return 1;
	}

	f = fopen(argv[1], "r");
	if (!f) {
		printf("File %s cannot be opened for read\n", argv[1]);
		return 1;
	}

	outfd = open(argv[2], O_CREAT|O_WRONLY|O_EXCL, 0644);
	if (outfd < 0) {
		printf("File %s cannot be opened for write\n", argv[2]);
		fclose(f);
		return 1;
	}

	column = strtoul(argv[3], NULL, 10);
	if (column == ULONG_MAX) {
		printf("conversion of column number %s failed\n", argv[3]);
		err = 1;
		goto err;
	}

	bits = strtoul(argv[4], NULL, 10);
	if (column == ULONG_MAX) {
		printf("conversion of bits number %s failed\n", argv[4]);
		err = 1;
		goto err;
	}
	if (bits > 8) {
		printf("max 8 bits allowed\n");
		err = 1;
		goto err;
	}
	bits = (1 << bits) - 1;

	while (fgets(buf, sizeof(buf), f)) {
		unsigned long long read;
		char *saveptr = NULL;
	 	char *res = NULL;
		unsigned long i;

		res = strtok_r(buf, " ", &saveptr);
		if (!res) {
			printf("strtok_r error in buf %s\n", buf);
			err = 1;
			goto err;
		}
			
		for (i = 1; i < column; i++) {
			res = strtok_r(NULL, " ", &saveptr);
			if (!res) {
				printf("strtok_r error in column %lu: %d\n", i, errno);
				err = 1;
				goto err;
			}
		}

		read = strtoull(res, NULL, 10);

		out = read & bits;
		write(outfd, &out, sizeof(out));
	}

err:
	if (f)
		fclose(f);
	close(outfd);
	return err;
}
