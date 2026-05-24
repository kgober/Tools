/*
 * unvtape.c - extract file(s) from a SIMH virtual tape image
 * Copyright (C) 2025,2026 Kenneth Gober
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void usage(const char *command, int status);
void extract_file(int fd);
size_t read_buffer(int fd, void *buf, size_t nbytes);
void write_buffer(int fd, const void *buf, size_t nbytes);
int get_int32(int8_t *buf);

size_t RECORD_SIZE = 0;		/* default: variable-length records */
int FILE_SKIP = 0;		/* default: extract first file */
int FILE_PAD = 0;		/* default: do not pad short records */
int VERBOSE = 0;		/* default: do not write status to standard error */
int SUMMARY = 0;		/* default: do not summarize tape content */

int main(int argc, char **argv)
{
	int fflag = 0;	/* flag: command-line specified a file */
	char *cmd = *argv;

	while (*(++argv))
	{
		char *arg = *argv;
		if (arg[0] == '-')
		{
			if (arg[1] == 0)
			{
				/* "-" by itself reads from stdin */
				if (VERBOSE) fprintf(stderr, "standard input\n");
				extract_file(STDIN_FILENO);
				fflag = 1;
				continue;
			}
			while (*(++arg))
			{
				if ((*arg == '?') || (*arg == 'h')) usage(cmd, 0);
				if (*arg == '-') /* -- */
				{
					fflag = 1;
					continue;
				}
				if (*arg == 'S') /* -S */
				{
					SUMMARY = 1;
					VERBOSE = 1;
					continue;
				}
				if (*arg == 'v') /* -v */
				{
					VERBOSE = 1;
					continue;
				}
				if (*arg == 'p') /* -p */
				{
					FILE_PAD = 1;
					continue;
				}
				if (*arg == 's') /* -s num */
				{
					if (*(++arg) == 0) arg = *(++argv);
					if (arg == NULL) usage(cmd, 1);
					int n = strtonum(arg, 1, 1048576, NULL);
					if (n == 0) err(1, "error processing -s argument");
					FILE_SKIP = n;
					break;
				}
				if (*arg == 'n') /* -n recordsize */
				{
					if (*(++arg) == 0) arg = *(++argv);
					if (arg == NULL) usage(cmd, 1);
					int n = strtonum(arg, 1, 1048576, NULL);
					if (n == 0) err(1, "error processing -n argument");
					RECORD_SIZE = n;
					break;
				}
				if (*arg == 'f') /* -f filename */
				{
					if (*(++arg) == 0) arg = *(++argv);
					if (arg == NULL) usage(cmd, 1);
					if (VERBOSE) fprintf(stderr, "%s\n", arg);
					int n = open(arg, O_RDONLY);
					if (n == -1) err(1, "error opening file %s", arg);
					extract_file(n);
					n = close(n);
					if (n == -1) err(1, "error closing file %s", arg);
					fflag = 1;
					break;
				}
				usage(cmd, 1); /* unrecognized option */
			}
			continue;
		}

		/* assume non-option arguments are file names */
		if (VERBOSE) fprintf(stderr, "%s\n", arg);
		int fd = open(arg, O_RDONLY);
		if (fd == -1) err(1, "error opening file %s", arg);
		extract_file(fd);
		fd = close(fd);
		if (fd == -1) err(1, "error closing file %s", arg);
		fflag = 1;
	}

	if (fflag == 0)
	{
		/* if command-line didn't specify any files, assume stdin */
		if (VERBOSE) fprintf(stderr, "standard input\n");
		extract_file(STDIN_FILENO);
	}

	return 0;
}

/* output usage message */
void usage(const char *command, int status)
{
	fprintf(stderr, "%s - extract a file from SIMH virtual tape image\n", command);
	fprintf(stderr, "Usage: %s [options] [[-f] filename] ...\n", command);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h or -?      - display this message\n");
	fprintf(stderr, "  -S            - summarize tape content without extracting\n");
	fprintf(stderr, "  -s num        - skip past 'num' file marks in input (default 0)\n");
	fprintf(stderr, "  -n recordsize - set a fixed tape record size (default variable)\n");
	fprintf(stderr, "  -f filename   - extract from the named file (-f may be omitted)\n");
	fprintf(stderr, "  -p            - pad short records in the extracted file\n");
	fprintf(stderr, "  -v            - display status information\n");
	fprintf(stderr, "  -             - extract from standard input (default if no files given)\n");
	fprintf(stderr, "  --            - don't extract from standard input (suppress '-' default)\n");
	exit(status);
}

/* extract file from SIMH virtual tape image */
void extract_file(int fd)
{
	int8_t *buf, hdr[4];
	size_t ct, sz, last_sz;

	size_t buf_size = (RECORD_SIZE == 0) ? 65536 : RECORD_SIZE;
	if ((buf = malloc(buf_size)) == NULL) err(1, "unable to initialize buffer");

	int n = 0;
	while ((ct = read_buffer(fd, &hdr, 4)) > 0)
	{
		if (((sz = get_int32(hdr)) == 0) || ((sz & 0xF0000000) == 0xF0000000))
		{
			if (VERBOSE)
			{
				if (n != 0)
				{
					if (n == 1)
					{
						fprintf(stderr, " (1 %zu-byte record%s)", last_sz, (FILE_SKIP) ? ", skipped": "");
					}
					else
					{
						fprintf(stderr, " (%d %zu-byte records%s)", n, last_sz, (FILE_SKIP) ? ", skipped" : "");
					}
					n = 0;
				}
				if (sz == 0)
				{
					fprintf(stderr, " (file mark)\n");
				}
				else if ((sz & 0xF0000000) == 0xF0000000)
				{
					int m = sz & 0x0FFFFFFF;
					if (m == 0x0FFFFFFF)
					{
						fprintf(stderr, " (tape end mark)\n");
						break;
					}
					else if (m == 0xFFFFFFE)
					{
						fprintf(stderr, " (erase gap)");
					}
					else
					{
						fprintf(stderr, " (tape marker 0xF%X)", m);
					}
				}
			}
			last_sz = 0;
			if (sz == 0)
			{
				if (FILE_SKIP > 0)
				{
					FILE_SKIP--;
					continue;
				}
				if (SUMMARY) continue;
			}
			break;
		}

		if ((VERBOSE) && (sz != last_sz) && (n != 0))
		{
			if (n == 1)
			{
				fprintf(stderr, " (1 %zu-byte record%s)", last_sz, (FILE_SKIP) ? ", skipped": "");
			}
			else
			{
				fprintf(stderr, " (%d %zu-byte records%s)", n, last_sz, (FILE_SKIP) ? ", skipped" : "");
			}
			n = 0;
		}

		if (sz > buf_size)
		{
			if ((buf_size = sz) & 1) buf_size++;
			if ((buf = realloc(buf, buf_size)) == NULL) err(1, "unable to resize buffer");
		}
		if ((ct = sz) & 1) ct++;
		ct = read_buffer(fd, buf, ct);
		if (ct == 0) err(1, "unexpected end of tape reading %zu-byte record", sz);
		ct = read_buffer(fd, &hdr, 4);
		if (ct == 0) err(1, "unexpected end of tape reading record trailer");
		n++;
		last_sz = sz;

		if ((!FILE_SKIP) && (!SUMMARY))
		{
			if (RECORD_SIZE != 0)
			{
				if (sz > RECORD_SIZE) sz = RECORD_SIZE;
				if (FILE_PAD) while(sz < RECORD_SIZE) buf[sz++] = 0;
			}
			write_buffer(STDOUT_FILENO, buf, sz);
		}
	}
	free(buf);

	if (VERBOSE)
	{
		if (n == 1)
		{
			fprintf(stderr, " (1 %lu-byte record%s)", last_sz, (FILE_SKIP) ? ", skipped": "");
		}
		else if (n > 1)
		{
			fprintf(stderr, " (%d %lu-byte records%s)", n, last_sz, (FILE_SKIP) ? ", skipped" : "");
		}
		if (last_sz != 0)
		{
			fprintf(stderr, "\n");
		}
	}
}

/* read a full buffer (even from a pipe) */
size_t read_buffer(int fd, void *buf, size_t nbytes)
{
	size_t p = 0;
	while (p < nbytes)
	{
		ssize_t ct = read(fd, buf + p, nbytes - p);
		if (ct == -1) err(1, NULL);
		if (ct == 0) break;
		p += ct;
	}
	return p;
}

/* write a buffer */
void write_buffer(int fd, const void *buf, size_t nbytes)
{
	size_t p = 0;
	while (p < nbytes)
	{
		ssize_t ct = write(fd, buf + p, nbytes - p);
		if (ct == -1) err(1, NULL);
		p += ct;
	}
}

/* get a 32-bit integer in little-endian format */
int get_int32(int8_t *buf)
{
	int i;

	int value = 0;
	for (i = 3; i >= 0; i--)
	{
		value = (value << 8) | (buf[i] & 255);
	}
	return value;
}
