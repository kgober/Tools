/*
 * dt2.c - DECtape II (TU58) manipulation program
 * Copyright (C) 2020 Kenneth Gober
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


#include <sys/types.h>	/* uint8_t */
#include <fcntl.h>	/* open() */
#include <stdio.h>	/* fputs() */
#include <stdlib.h>	/* strtol() */
#include <string.h>	/* strcmp() */
#include <strings.h>	/* strcasecmp() */
#include <termios.h>	/* tcgetattr(), tcsetattr(), tcdrain(), tcflush(), tcsendbreak() */
#include <unistd.h>	/* read(), write(), close() */


/* defaults which may be overidden by command line options */
char *DEV_PATH = "/dev/cua00";	/* device where TU58 is attached */
char *BAUD_XMIT = "38400";	/* your hardware may require BAUD_XMIT == BAUD_RECV */
char *BAUD_RECV = "38400";	/* note: command-line option sets both at once */
int DEBUG = 0;

/* defaults not settable via command line */
int UMAX = 255;			/* maximum unit number, normally 0 or 1 for a real TU58 */


/* packet types */
#define PKT_DATA  1
#define PKT_CMD   2
#define PKT_INIT  4
#define PKT_BOOT  8
#define PKT_CONT 16
#define PKT_XON  17
#define PKT_XOFF 19

/* CMD packet opcodes */
#define CMD_NOP   0
#define CMD_INIT  1
#define CMD_READ  2
#define CMD_WRITE 3
#define CMD_NOP4  4
#define CMD_SEEK  5
#define CMD_NOP6  6
#define CMD_DIAG  7
#define CMD_GETS  8	/* internally NOP */
#define CMD_SETS  9	/* internally NOP */
#define CMD_NOP10 10	/* internally NOP in newer ROMs */
#define CMD_NOP11 11
#define CMD_END   64

/* whether RSP or MRSP is being used */
#define MODE_RSP  1
#define MODE_MRSP 2
int MODE = MODE_RSP;
int FLAG_MRSP_NEXT = 0;

/* current drive number */
int UNIT = 0;

/* current block number */
int BNUM = 0;

/* current block size and number of blocks */
size_t BSIZE = 512;
int BCOUNT = 512;

/* buffers */
uint8_t RECV;
uint8_t CMD[14];
uint8_t BUF[512];
struct termios TIO_SAVE;


/* macros */
#define lo(x) ((uint8_t)((x) & 0xff))
#define hi(x) ((uint8_t)(((x) >> 8) & 0xff))


int main(int argc, char **argv)
{
	char *pname = *argv++;
	--argc;

	/* options */
	while ((argc > 0) && (**argv == '-'))
	{
		if (strcmp(*argv, "-f") == 0)
		{
			if (--argc == 0) return 1;
			argv++;
			DEV_PATH = *argv++;
			--argc;
			continue;
		}
		if (strcmp(*argv, "-s") == 0)
		{
			if (--argc == 0) return 1;
			argv++;
			BAUD_RECV = *argv;
			BAUD_XMIT = *argv;
			argv++;
			--argc;
			continue;
		}
		if (strcmp(*argv, "-m") == 0)
		{
			MODE = MODE_MRSP;
			FLAG_MRSP_NEXT = 1;
			argv++;
			--argc;
			continue;
		}
		if (strcmp(*argv, "-d") == 0)
		{
			DEBUG = 1;
			argv++;
			--argc;
			continue;
		}
		if (strcmp(*argv, "-") == 0)
		{
			argv++;
			--argc;
			break;
		}
		argc = 0;
	}
	if (argc == 0) return usage(pname);

	int fd = open(DEV_PATH, O_RDWR | O_SYNC);
	if (fd == -1) return 1;
	if (termio_init(fd) != 0) return 1;

	/* commands */
	int rc = 0, argi = 0;
	while (argi < argc)
	{
		char *cmd = argv[argi++];
		rc = argi;
		int num = (argi < argc) ? parse_num(argv[argi]) : -1;
		if (num != -1) argi++;
		if (strcasecmp(cmd, "init") == 0)
		{
			if (do_init(fd) < 0) break;
		}
		else if ((strcasecmp(cmd, "drive") == 0) || (strcasecmp(cmd, "unit") == 0))
		{
			if ((num < 0) || (num > UMAX)) break;
			UNIT = num;
		}
		else if (strcasecmp(cmd, "boot") == 0)
		{
			if (num == -1) num = UNIT;
			if (do_boot(fd, num) < 0) break;
		}
		else if (strcasecmp(cmd, "rewind") == 0)
		{
			if (num == -1) num = UNIT;
			if (do_rewind(fd, num) < 0) break;
		}
		else if (strcasecmp(cmd, "status") == 0)
		{
			if (num == -1) num = UNIT;
			if (do_status(fd, num) < 0) break;
		}
		else if (strcasecmp(cmd, "retension") == 0)
		{
			if (num == -1) num = UNIT;
			if (do_retension(fd, num) < 0) break;
		}
		else if (strcasecmp(cmd, "seek") == 0)
		{
			if ((num < 0) || (num >= BCOUNT)) break;
			BNUM = num;
		}
		else if (strcasecmp(cmd, "read") == 0)
		{
			if (num == -1) num = BCOUNT - BNUM;
			if (do_read(fd, num, 0) < 0) break;
		}
		else if (strcasecmp(cmd, "readv") == 0)
		{
			if (num == -1) num = BCOUNT - BNUM;
			if (do_read(fd, num, 1) < 0) break;
		}
		else if (strcasecmp(cmd, "write") == 0)
		{
			if (num == -1) num = BCOUNT - BNUM;
			if (do_write(fd, num, 0) < 0) break;
		}
		else if (strcasecmp(cmd, "writev") == 0)
		{
			if (num == -1) num = BCOUNT - BNUM;
			if (do_write(fd, num, 1) < 0) break;
		}
		else if (strcasecmp(cmd, "blocksize") == 0)
		{
			if ((num != 128) && (num != 512)) break;
			BSIZE = num;
			BCOUNT = (BSIZE == 128) ? 2048 : 512;
		}
		else
		{
			fputs("unrecognized command: ", stderr);
			fputs(cmd, stderr);
			fputs("\n", stderr);
			break;
		}
		rc = 0;
	}

	termio_restore(fd);
	close(fd);
	return rc;
}


int usage(char *pname)
{
	fputs("usage: ", stderr);
	fputs(pname, stderr);
	fputs(" [options] command [num] ...\n", stderr);
	fputs("options:\n", stderr);
	fputs(" -f device - set TU58 device\n", stderr);
	fputs(" -s speed - set TU58 baud rate\n", stderr);
	fputs(" -m - enable MRSP\n", stderr);
	fputs(" -d - enable debug output (to stderr)\n", stderr);
	fputs("commands:\n", stderr);
	fputs(" init - initialize TU58 device\n", stderr);
	fputs(" drive|unit unit_num - set current unit number\n", stderr);
	fputs(" boot [unit_num] - read boot block\n", stderr);
	fputs(" rewind [unit_num] - rewind tape\n", stderr);
	fputs(" status [unit_num] - report status (to stderr)\n", stderr);
	fputs(" retension [unit_num] - retension tape\n", stderr);
	fputs(" seek block_num - set current block number\n", stderr);
	fputs(" read [block_count] - read blocks\n", stderr);
	fputs(" readv [block_count] - read blocks with reduced sensitivity\n", stderr);
	fputs(" write [block_count] - write blocks\n", stderr);
	fputs(" writev [block_count] - write and verify blocks\n", stderr);
	fputs(" blocksize {128|512} - set current block size\n", stderr);
	return 1;
}


/* set TU58 tty device to raw mode, saving old tty state */
int termio_init(int fd)
{
	speed_t ispeed, ospeed;
	if (strcmp(BAUD_XMIT, "150") == 0) ospeed = B150;
	else if (strcmp(BAUD_XMIT, "300") == 0) ospeed = B300;
	else if (strcmp(BAUD_XMIT, "600") == 0) ospeed = B600;
	else if (strcmp(BAUD_XMIT, "1200") == 0) ospeed = B1200;
	else if (strcmp(BAUD_XMIT, "2400") == 0) ospeed = B2400;
	else if (strcmp(BAUD_XMIT, "4800") == 0) ospeed = B4800;
	else if (strcmp(BAUD_XMIT, "9600") == 0) ospeed = B9600;
	else if (strcmp(BAUD_XMIT, "19200") == 0) ospeed = B19200;
	else if (strcmp(BAUD_XMIT, "38400") == 0) ospeed = B38400;
	if (strcmp(BAUD_RECV, "150") == 0) ispeed = B150;
	else if (strcmp(BAUD_RECV, "300") == 0) ispeed = B300;
	else if (strcmp(BAUD_RECV, "600") == 0) ispeed = B600;
	else if (strcmp(BAUD_RECV, "1200") == 0) ispeed = B1200;
	else if (strcmp(BAUD_RECV, "2400") == 0) ispeed = B2400;
	else if (strcmp(BAUD_RECV, "4800") == 0) ispeed = B4800;
	else if (strcmp(BAUD_RECV, "9600") == 0) ispeed = B9600;
	else if (strcmp(BAUD_RECV, "19200") == 0) ispeed = B19200;
	else if (strcmp(BAUD_RECV, "38400") == 0) ispeed = B38400;

	struct termios TIO;
	cfmakeraw(&TIO);
	if (cfsetispeed(&TIO, ispeed) == -1) return -1;
	if (cfsetospeed(&TIO, ospeed) == -1) return -1;
	TIO.c_iflag &= ~(BRKINT|IGNPAR|PARMRK|INPCK|ISTRIP|INLCR|IGNCR|ICRNL|IXON|IMAXBEL);
	TIO.c_iflag |= IGNBRK|IXOFF;
	TIO.c_oflag &= ~(OPOST);
	TIO.c_cflag &= ~(CSIZE|CSTOPB|PARENB|CCTS_OFLOW|CRTS_IFLOW|MDMBUF);
	TIO.c_cflag |= CS8|CREAD|CLOCAL;
	TIO.c_lflag &= ~(ECHO|ECHOCTL|ISIG|ICANON);
	TIO.c_lflag |= NOFLSH;
	TIO.c_cc[VSTART] = PKT_CONT;
	TIO.c_cc[VSTOP] = PKT_XOFF;
	TIO.c_cc[VMIN] = 1;
	TIO.c_cc[VTIME] = 0;
	if (tcgetattr(fd, &TIO_SAVE) == -1) return -1;
	return tcsetattr(fd, TCSANOW, &TIO);
}


/* restore old tty state */
int termio_restore(int fd)
{
	return tcsetattr(fd, TCSANOW, &TIO_SAVE);
}


/* convert a string to a non-negative number.  requires 32-bit int, 64-bit long. */
int parse_num(char *str)
{
	if (str == NULL) return -1;
	if (*str == '\0') return -1;
	char *ptr;
	long val = strtol(str, &ptr, 0);
	if (*ptr != '\0') return -1;
	if ((val < 0) || (val > 0x7fffffff)) return -1;
	return val;
}


int do_init(int fd)
{
	if (send_break(fd) < 0) return -1;
	if (send_init(fd) < 0) return -1;
	if (send_init(fd) < 0) return -1;
	if (recv_continue(fd) < 0) return -1;
	return 0;
}


int do_boot(int fd, int dnum)
{
	if ((dnum < 0) || (dnum > UMAX)) return -1;
	if (send_break(fd) < 0) return -1;
	if (send_init(fd) < 0) return -1;
	if (send_boot(fd, dnum) < 0) return -1;
	if (recv_bytes(fd, 512) < 0) return -1;
	return 0;
}


int do_rewind(int fd, int dnum)
{
	if ((dnum < 0) || (dnum > UMAX)) return -1;
	if (send_seek(fd, dnum, 0) < 0) return -1;
	if (recv_end(fd) < 0) return -1;
	return 0;
}


int do_status(int fd, int dnum)
{
	fprintf(stderr, "device: %s\n", DEV_PATH);
	fprintf(stderr, "unit: %d\n", dnum);
	fprintf(stderr, "position: %d\n", BNUM);
	fprintf(stderr, "blocksize: %d\n", BSIZE);
	return 0;
}


int do_retension(int fd, int dnum)
{
	if ((dnum < 0) || (dnum > UMAX)) return -1;
	if (send_seek(fd, dnum, BCOUNT - 1) < 0) return -1;
	if (recv_end(fd) < 0) return -1;
	if (send_seek(fd, dnum, 0) < 0) return -1;
	if (recv_end(fd) < 0) return -1;
	return 0;
}


int do_read(int fd, int count, int mode)
{
	if ((count < 0) || (count > (BCOUNT - BNUM))) return -1;
	while (count > 0)
	{
		int len = count * BSIZE;
		if (len > 65535) len = 65536 - BSIZE;
		if (send_read(fd, UNIT, BNUM, len, mode) < 0) return -1;
		int ct = len;
		while (ct > 0)
		{
			int n = recv_data(fd);
			if (n < 0) return -1;
			ct -= n;
		}
		ct = len / BSIZE;
		BNUM += ct;
		count -= ct;
		if ((ct = recv_end(fd)) < 0) return -1;
		if (ct != len) return -1;
	}
	return 0;
}


int do_write(int fd, int count, int mode)
{
	if ((count < 0) || (count > (BCOUNT - BNUM))) return -1;
	while (count > 0)
	{
		int len = count * BSIZE;
		if (len > 65535) len = 65536 - BSIZE;
		if (send_write(fd, UNIT, BNUM, len, mode) < 0) return -1;
		int ct = len;
		while (ct > 0)
		{
			int n = (ct > 128) ? 128 : ct;
			if (recv_continue(fd) < 0) return -1;
			if (send_data(fd, n) < 0) return -1;
			ct -= n;
		}
		ct = len / BSIZE;
		BNUM += ct;
		count -= ct;
		if ((ct = recv_end(fd)) < 0) return -1;
		if (ct != len) return -1;
	}
	return 0;
}


int send_break(int fd)
{
	if (tcdrain(fd) == -1) return -1;
	if (tcsendbreak(fd, 0) == -1) return -1;
	return tcflush(fd, TCIFLUSH);
}


int send_init(int fd)
{
	if (DEBUG) fputs("send INIT", stderr);
	CMD[0] = PKT_INIT;
	if (write_buf(fd, CMD, 1) < 1) return -1;
	return 0;
}


int send_boot(int fd, int dnum)
{
	if (DEBUG) fputs("send BOOT", stderr);
	CMD[0] = PKT_BOOT;
	CMD[1] = dnum;
	if (write_buf(fd, CMD, 2) < 2) return -1;
	return 0;
}


int send_seek(int fd, int dnum, int bnum)
{
	if (DEBUG) fputs("send SEEK", stderr);
	return send_cmd(fd, CMD_SEEK, dnum, bnum, 0, 0);
}


int send_read(int fd, int dnum, int bnum, int count, int mod)
{
	if (DEBUG) fputs("send READ", stderr);
	return send_cmd(fd, CMD_READ, dnum, bnum, count, mod);
}


int send_write(int fd, int dnum, int bnum, int count, int mod)
{
	if (DEBUG) fputs("send WRITE", stderr);
	return send_cmd(fd, CMD_WRITE, dnum, bnum, count, mod);
}


int send_cmd(int fd, int op, int dnum, int bnum, int count, int mod)
{
	CMD[0] = PKT_CMD;
	CMD[1] = 10;
	CMD[2] = op;
	CMD[3] = mod | (BSIZE == 128) ? 128 : 0;
	CMD[4] = dnum;
	CMD[5] = (MODE == MODE_MRSP) ? 8 : 0;
	CMD[6] = 0;
	CMD[7] = 0;
	CMD[8] = lo(count);
	CMD[9] = hi(count);
	CMD[10] = lo(bnum);
	CMD[11] = hi(bnum);
	int sum = cksum_buf(CMD, 12);
	CMD[12] = lo(sum);
	CMD[13] = hi(sum);
	if (DEBUG) fprintf(stderr, " op=%d mod=%d unit=%d sw=%d bnum=%d ct=%d ck=0x%4x\n", CMD[2], CMD[3], CMD[4], CMD[5], bnum, count, sum);
	if(write_buf(fd, CMD, 14) < 14) return -1;
	return 0;
}


int send_data(int fd, int count)
{
	BUF[0] = PKT_DATA;
	BUF[1] = count;
	if (read_buf(0, BUF + 2, count) < count) return -1;
	int sum = cksum_buf(BUF, count += 2);
	BUF[count++] = lo(sum);
	BUF[count++] = hi(sum);
	if (DEBUG) fprintf(stderr, "send DATA ct=%d ck=0x%4x\n", count - 2, sum);
	if (write_buf(fd, BUF, count) < count) return -1;
	return 0;
}


int recv_continue(fd)
{
	if (DEBUG) fputs("recv CONT", stderr);
	if (read_buf(fd, &RECV, 1) < 1) return -1;
	if (DEBUG) fprintf(stderr, " flag=%d\n", RECV);
	if (RECV == PKT_CONT) return 0;
	if (RECV == PKT_INIT) do_init(fd);
	return -1;
}


/* note: returns byte count value from packet on success */
int recv_end(fd)
{
	if (DEBUG) fputs("recv END", stderr);
	if (read_buf(fd, &RECV, 1) < 1) return -1;
	if (RECV == PKT_CMD)
	{
		CMD[0] = RECV;
		if (read_buf(fd, CMD + 1, 13) < 13) return -1;
		int len = CMD[8] + (CMD[9] << 8);
		int stat = CMD[10] + (CMD[11] << 8);
		int sum = cksum_buf(CMD, 12);
		if (DEBUG) fprintf(stderr, " flag=%d op=%d rc=%d unit=%d ct=%d stat=0x%4x ck=0x%4x/%2x%2x\n", RECV, CMD[2], (int)((int8_t)(CMD[3])), CMD[4], len, stat, sum, CMD[13], CMD[12]);
		if ((CMD[12] != lo(sum)) || (CMD[13] != hi(sum))) return -2;
		if (CMD[2] == CMD_END) return len;
	}
	if (DEBUG) fprintf(stderr, " flag=%d\n", RECV);
	if (RECV == PKT_INIT) do_init(fd);
	return -1;
}


/* note: returns size of data packet payload on success */
int recv_data(int fd)
{
	if (DEBUG) fputs("recv DATA", stderr);
	if (read_buf(fd, &RECV, 1) < 1) return -1;
	if (RECV == PKT_DATA)
	{
		BUF[0] = RECV;
		if (read_buf(fd, BUF + 1, 1) < 1) return -1;
		int len = BUF[1] + 2;
		if (read_buf(fd, BUF + 2, len) < len) return -1;
		int sum = cksum_buf(BUF, len);
		if (DEBUG) fprintf(stderr, " flag=%d ct=%d sum=0x%4x/%2x%2x\n", RECV, BUF[1], sum, BUF[len + 1], BUF[len]);
		if ((BUF[len] != lo(sum)) || (BUF[len + 1] != hi(sum))) return -2;
		return write_buf(1, BUF + 2, BUF[1]);
	}
	if (DEBUG) fprintf(stderr, " flag=%d\n", RECV);
	if (RECV == PKT_INIT) do_init(fd);
	return -1;
}


int recv_bytes(int fd, int count)
{
	if (DEBUG) fprintf(stderr, "recv BYTES ct=%d\n", count);
	while (count > 0)
	{
		int len = count;
		if (len > sizeof(BUF)) len = sizeof(BUF);
		int n = read_buf(fd, BUF, len);
		if (n > 0) n = write_buf(1, BUF, n);
		if (n < len) return -1;
		count -= n;
	}
	return 0;
}


int cksum_buf(uint8_t *buf, size_t count)
{
	int sum = 0;
	while (count-- > 0)
	{
		sum += *buf++;
		if (count-- > 0) sum += (*buf++) << 8;
		if (sum > 65535) sum -= 65535;	/* TU58 end-around carry */
	}
	return sum;
}


/* read 'count' bytes into 'buf' from 'fd'.  return number of bytes able to be read. */
int read_buf(int fd, uint8_t *buf, size_t count)
{
	int p = 0;
	while (count > 0)
	{
		int n = read(fd, buf + p, count);
		if (n == -1) break;
		p += n;
		count -= n;
	}
	return p;
}


/* write 'count' bytes from 'buf' to 'fd'.  return number of bytes able to be written. */
int write_buf(int fd, uint8_t *buf, size_t count)
{
	int p = 0;
	while (count > 0)
	{
		int n = write(fd, buf + p, count);
		if (n == -1) break;
		p += n;
		count -= n;
	}
	return p;
}
