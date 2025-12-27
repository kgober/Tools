/*
 * vtape.c - convert a file into a SIMH virtual tape image
 * Copyright (C) 2025 Kenneth Gober
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
 * SOFTWARE. */

#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void usage(const char *command, int status);
void write_file(int fd);
size_t read_buffer(int fd, void *buf, size_t nbytes);
void write_buffer(int fd, const void *buf, size_t nbytes);
void write_int8(int fd, int8_t value);
void write_int32(int fd, int value);

size_t RECORD_SIZE = 512;	/* default: 512-byte records */
int FILE_MARK = 0;		/* default: do not append tape mark after next file */
int FILE_PAD = 0;		/* default: do not pad final record of next file */
int VERBOSE = 0;		/* default: do not write status to standard output */

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
				if (VERBOSE) fprintf(stderr, "write from standard input");
				write_file(STDIN_FILENO);
				fflag = 1;
				continue;
			}
			while (*(++arg))
			{
				if ((*arg == '?') || (*arg == 'h')) usage(cmd, 0);
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
				if (*arg == 'm') /* -m */
				{
					FILE_MARK++;
					continue;
				}
				if (*arg == 'M') /* -M */
				{
					if (VERBOSE) fprintf(stderr, "write file mark\n");
					write_int32(STDOUT_FILENO, 0);
					continue;
				}
				if (*arg == 'n') /* -n recordsize */
				{
					if (*(++arg) == 0) arg = *(++argv);
					if (arg == NULL) usage(cmd, 1);
					int n = strtonum(arg, 1, 65536, NULL);
					if (n == 0) err(1, "error processing -n argument");
					RECORD_SIZE = n;
					break;
				}
				if (*arg == 'f') /* -f filename */
				{
					if (*(++arg) == 0) arg = *(++argv);
					if (arg == NULL) usage(cmd, 1);
					if (VERBOSE) fprintf(stderr, "write file %s", arg);
					int n = open(arg, O_RDONLY);
					if (n == -1) err(1, "error opening file %s", arg);
					write_file(n);
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
		if (VERBOSE) fprintf(stderr, "write file %s", arg);
		int fd = open(arg, O_RDONLY);
		if (fd == -1) err(1, "error opening file %s", arg);
		write_file(fd);
		fd = close(fd);
		if (fd == -1) err(1, "error closing file %s", arg);
		fflag = 1;
	}

	if (fflag == 0)
	{
		/* if command-line didn't specify any files, assume stdin */
		if (VERBOSE) fprintf(stderr, "write from standard input");
		write_file(STDIN_FILENO);
	}

	/* write any remaining file marks */
	while (FILE_MARK != 0)
	{
		if (VERBOSE) fprintf(stderr, "write file mark\n");
		write_int32(STDOUT_FILENO, 0);
		FILE_MARK--;
	}

	return 0;
}

/* output usage message */
void usage(const char *command, int status)
{
	fprintf(stderr, "%s - write file(s) in SIMH virtual tape format\n", command);
	fprintf(stderr, "Usage: %s [options] [[-f] filename] ...\n", command);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -h or -?      - display this message\n");
	fprintf(stderr, "  -n recordsize - set the tape record size (default 512)\n");
	fprintf(stderr, "  -f filename   - write the named file (-f may be omitted)\n");
	fprintf(stderr, "  -m            - write a file mark after the next file\n");
	fprintf(stderr, "  -M            - write a file mark before the next file\n");
	fprintf(stderr, "  -p            - pad the next file to fill its last record\n");
	fprintf(stderr, "  -v            - display status information\n");
	fprintf(stderr, "  -             - write standard input\n");
	fprintf(stderr, "If no file arguments are given, standard input is assumed.\n");
	fprintf(stderr, "-m with no next file will write a file mark after the last file.\n");
	exit(status);
}

/* convert file to SIMH virtual tape format */
void write_file(int fd)
{
	int8_t *buf;
	size_t ct, last_ct;

	if ((buf = malloc(RECORD_SIZE)) == NULL) err(1, "unable to allocate memory");

	int n = 0;
	while ((ct = read_buffer(fd, buf, RECORD_SIZE)) > 0)
	{
		/* last record may be short */
		if (FILE_PAD != 0)
		{
			while (ct < RECORD_SIZE) buf[ct++] = 0;
			FILE_PAD = 0;
		}

		/* write record size */
		write_int32(STDOUT_FILENO, ct);

		/* write record */
		write_buffer(STDOUT_FILENO, buf, ct);

		/* add pad byte if needed */
		if ((ct & 1) != 0) write_int8(STDOUT_FILENO, 0);

		/* write record size */
		write_int32(STDOUT_FILENO, ct);

		n++;
		last_ct = ct;
	}

	if (VERBOSE)
	{
		if (n == 1)
		{
			fprintf(stderr, " (1 %lu-byte record)\n", last_ct);
		}
		else if (last_ct == RECORD_SIZE)
		{
			fprintf(stderr, " (%d %lu-byte records)\n", n, RECORD_SIZE);
		}
		else if (n == 2)
		{
			fprintf(stderr, " (1 %lu-byte record, 1 %lu-byte record)\n", RECORD_SIZE, last_ct);
		}
		else
		{
			fprintf(stderr, " (%d %lu-byte records, 1 %lu-byte record)\n", n - 1, RECORD_SIZE, last_ct);
		}
	}

	while (FILE_MARK != 0)
	{
		if (VERBOSE) fprintf(stderr, "write file mark\n");
		write_int32(STDOUT_FILENO, 0);
		FILE_MARK--;
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

/* write an 8-bit byte */
void write_int8(int fd, int8_t value)
{
	write_buffer(fd, &value, 1);
}

/* write a 32-bit integer in little-endian format */
void write_int32(int fd, int value)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		int8_t byte_val = value & 0xff;
		write_int8(fd, byte_val);
		value >>= 8;
	}
}
