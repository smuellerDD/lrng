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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <openssl/bn.h>

#define MAJVERSION 0 /* API / ABI incompatible changes, functional changes that
		      * require consumer to be updated (as long as this number
		      * is zero, the API is not considered stable and can
		      * change without a bump of the major version) */
#define MINVERSION 0 /* API compatible, ABI may change, functional
		      * enhancements only, consumer can be left unchanged if
		      * enhancements are not considered */
#define PATCHLEVEL 0 /* API / ABI compatible, no functional changes, no
		      * enhancements, bug fixes only */

#define MAX_ROW		1000
#define MAX_COL		1000

static int read_data(unsigned long long array[MAX_COL][MAX_ROW],
		     const char *pathname)
{
	FILE *file = NULL;
	int ret = 0, character;
	unsigned int row = 0, col = 0;
	char str[13];
	char *str_p = str;

	file = fopen(pathname, "r");
	if (!file)
		return -errno;

	while (1) {
		character = fgetc(file);
		if (character == EOF)
			break;

		if (isdigit(character)) {
			/* Prevent overflow */
			if ((unsigned long long)(str_p - str) >= sizeof(str) - 1) {
				fprintf(stderr, "Prevent overflow %ld\n", str_p - str);
				ret = -EINVAL;
				goto out;
			}

			*str_p = (char)character;
			str_p++;

			continue;
		}

		str[str_p - str - 1] = '\0';
		/* We have some data */
		if (str_p > str) {
			unsigned long long val = strtoull(str, NULL, 10);

			array[col][row] = val;

			str_p = str;
			memset(str, 0, sizeof(str));
			row++;
			if (row > MAX_ROW) {
				fprintf(stderr, "Row overflowing\n");
				ret = -EINVAL;
				goto out;
			}
		}

		if (character != 32 && isspace(character)) {
			col++;
			row = 0;

			if (col > MAX_COL) {
				fprintf(stderr, "Col overflowing, stopping\n");
				goto out;
			}
		}
	}

out:
	if (file)
		fclose(file);

	return ret;
}

static int get_max_rows(unsigned long long array[MAX_COL][MAX_ROW],
			unsigned int *max_out)
{
	unsigned int max = 0;
	unsigned int i, j, k;

	for (i = 0; i < MAX_ROW; i++) {
		for (j = 0; j < MAX_COL; j++) {
			unsigned long long curr = array[i][j];
			unsigned int found = 0;

			for (k = 0; k < MAX_COL; k++) {
				if (curr == array[i][k]) {
					found++;

					if (max < found) {
						fprintf(stderr, "max at row %u: %llu (counts: %u)\n",
						       i, array[i][k], found);
					}
				}
			}

			if (max < found)
				max = found;
		}
	}

	fprintf(stderr, "Max over all rows %u\n", max);

	if (*max_out < max)
		*max_out = max;

	return 0;
}

static int get_max_cols(unsigned long long array[MAX_COL][MAX_ROW],
			unsigned int *max_out)
{
	unsigned int max = 0;
	unsigned int i, j, k;

	for (i = 0; i < MAX_COL; i++) {
		for (j = 0; j < MAX_ROW; j++) {
			unsigned long long curr = array[j][i];
			unsigned int found = 0;

			for (k = 0; k < MAX_COL; k++) {
				if (curr == array[k][i]) {
					found++;

					if (max < found) {
						fprintf(stderr, "max at col %u: %llu (counts %u)\n",
						       i, array[k][i], found);
					}
				}
			}

			if (max < found)
				max = found;
		}
	}

	fprintf(stderr, "Max over all columns %u\n", max);

	if (*max_out < max)
		*max_out = max;

	return 0;
}

int main(int argc, char *argv[])
{
	unsigned long long array[MAX_COL][MAX_ROW] = { 0 };
	unsigned int max = 0;
	int ret;

	if (argc != 2) {
		fprintf(stderr, "Provide input file\n");
		return EINVAL;
	}

	ret = read_data(array, argv[1]);
	if (ret)
		return -ret;

// 	unsigned int i,j;
// 	for (i = 0; i < MAX_COL; i++) {
// 		for (j = 0; j < MAX_ROW; j++) {
// 			printf("%u ", array[i][j]);
// 		}
// 		printf("\n");
// 	}

	ret = get_max_rows(array, &max);
	if (ret)
		return -ret;
	ret = get_max_cols(array, &max);
	if (ret)
		return -ret;

	fprintf(stdout, "%u\n", max);

	return 0;
}
