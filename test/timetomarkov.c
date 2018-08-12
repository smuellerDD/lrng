/*
 * Copyright (C) 2016, Stephan Mueller <smueller@chronox.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL2
 * are required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[])
{
	FILE *f = NULL;
	char buf[64];
	int outfd = -1;
	unsigned char out = 0;
	unsigned int outptr = 4;

	if (argc != 3) {
		printf("Provide input / output file\n");
		return 1;
	}

	f = fopen(argv[1], "r");
	if (!f) {
		printf("File %s cannot be opened for read\n", argv[1]);
		return 1;
	}

	outfd = open(argv[2], O_CREAT|O_WRONLY|O_EXCL, 0777);
	if (outfd < 0) {
		printf("File %s cannot be opened for write\n", argv[2]);
		fclose(f);
		return 1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		uint32_t num = strtoul(buf, NULL, 10);

		/* only use the lower 4 bits of the value */
		num = num & 0x0f;
		out |= (num << outptr);
		if (!outptr) {
			outptr = 4;
			write(outfd, &out, sizeof(out));
			out = 0;
		} else {
			outptr -= 4;
		}
	}
	if (!outptr)
		write(outfd, &out, sizeof(out));

	fclose(f);
	close(outfd);
	return 0;
}
