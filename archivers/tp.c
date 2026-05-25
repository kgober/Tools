/*
 * tp.c - 'tp' format tape archive utility
 * Copyright (C) 2026 Kenneth Gober
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

#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>


char FUNCTION = 'u';		/* current function: 'r', 'u', 'd', 'x', or 't' */
char CREATE = 0;		/* modifier: 'c' */
char VERBOSE = 0;		/* modifier: 'v' */
char FAKE = 0;			/* modifier: 'f' */
char IGNORE = 0;		/* modifier: 'i' */
char WAIT = 0;			/* modifier: 'w' */
char ALLFILES = 0;		/* modifier: 'a' */
char TAPE_UNIT = 0;		/* modifier: 0..7 */
char *TAPE_FILE = NULL;		/* modifier: 'F' */
int UID = -1;			/* modifier: 'U' */
int GID = -1;			/* modifier: 'G' */

int DIR_BLOCKS = 24;		/* number of tape blocks in directory */
int DIR_ENTRIES = 24 * 8;	/* 8 entries per directory block */
int8_t *TAPE_DIR = NULL;	/* in-memory copy of tape directory */
char *DEV_NAME = NULL;		/* buffer for tape device name */
char MODE_BUF[10];		/* static buffer for mode_str() */

int usage(const char *command, int status);
int fn_write(char **name);
int fn_delete(char **name);
int fn_extract(char **name);
int fn_list(char **name);
char *tape_file_name();
void create_dir();
void read_dir(int fd);
void write_boot(int fd);
void write_dir(int fd);
void write_files(int fd);
void tp_dir_write(const char *name);
void tp_dir_delete(const char *name);
void tp_dir_extract(int fd, const char *name);
void tp_dir_list(const char *name);
void tp_dir_list_entry(const int8_t *entry);
char *mode_str(int mode);
int8_t *find_dir_entry(const char *name);
int8_t *find_dir_first(const char *name);
int8_t *find_dir_next(const char *name, int8_t *ptr);
uint16_t find_dir_blocks(int blocks);
int read_tape_blocks(int fd, char *path, int size, int mode);
int write_tape_blocks(int fd, char *path, int size);
int copy_blocks(int src_fd, int tgt_fd, size_t nbytes, int pad, char *name);
size_t read_buffer(int fd, void *buf, size_t nbytes);
void write_buffer(int fd, void *buf, size_t nbytes);
uint8_t get_byte(const int8_t *buf);
uint16_t get_word(const int8_t *buf);
uint32_t get_size(const int8_t *buf);
uint32_t get_dword(const int8_t *buf);
int8_t *put_byte(int8_t *buf, uint8_t byte);
int8_t *put_word(int8_t *buf, uint16_t word);
int8_t *put_size(int8_t *buf, uint32_t size);
int8_t *put_dword(int8_t *buf, uint32_t dword);


int main(int argc, char **argv)
{
	char *cmd, *key, *s1, *s2;
	int i;

	cmd = *(argv++);
	if ((key = *(argv++)) != NULL)
	{
		while (*key)
		{
			char k = *(key++);
			if ((k == '?') || (k == 'h')) /* help */
			{
				return usage(cmd, 0);
			}
			else if (k == 'r') /* replace */
			{
				FUNCTION = 'r';
			}
			else if (k == 'u') /* update */
			{
				FUNCTION = 'u';
			}
			else if (k == 'd') /* delete */
			{
				FUNCTION = 'd';
			}
			else if (k == 'x') /* extract */
			{
				FUNCTION = 'x';
			}
			else if (k == 'X') /* extract to stdout */
			{
				FUNCTION = 'X';
			}
			else if (k == 't') /* table of contents */
			{
				FUNCTION = 't';
			}
			else if (k == 'm') /* magtape */
			{
				DIR_BLOCKS = 62;
				DIR_ENTRIES = 62 * 8;
				CREATE = 'c';
			}
			else if (k == 'M') /* magtape without implied 'c' */
			{
				DIR_BLOCKS = 62;
				DIR_ENTRIES = 62 * 8;
			}
			else if ((k >= '0') && (k <= '7')) /* tape unit */
			{
				TAPE_UNIT = k;
			}
			else if (k == 'v') /* verbose */
			{
				VERBOSE = 'v';
			}
			else if (k == 'c') /* create new tape directory */
			{
				CREATE = 'c';
			}
			else if (k == 'f') /* fake entries */
			{
				FAKE = 'f';
			}
			else if (k == 'i') /* ignore errors */
			{
				IGNORE = 'i';
			}
			else if (k == 'w') /* wait for confirmation */
			{
				WAIT = 'w';
			}
			else if (k == 'a') /* include dotfiles */
			{
				ALLFILES = 'a';
			}
			else if (k == 'F') /* use file rather than tape device */
			{
				TAPE_FILE = *(argv++);
				if (TAPE_FILE == NULL) err(1, "error processing -F argument");
			}
			else if (k == 'U') /* specify uid */
			{
				s1 = *(argv++);
				if ((s1 == NULL) || (*s1 == 0)) err(1, "error processing -U argument");
				long u = strtol(s1, &s2, 0);
				if ((s2 == s1) || (*s2 != 0)) err(1, "error processing -U argument");
				if ((u < 0) || (u > 255)) err(1, "-U value out of range");
				UID = u & 255;
			}
			else if (k == 'G') /* specify gid */
			{
				s1 = *(argv++);
				if ((s1 == NULL) || (*s1 == 0)) err(1, "error processing -G argument");
				long g = strtol(s1, &s2, 0);
				if ((s2 == s1) || (*s2 != 0)) err(1, "error processing -G argument");
				if ((g < 0) || (g > 255)) err(1, "-G value out of range");
				GID = g & 255;
			}
			else
			{
				return usage(cmd, 1); /* unrecognized option */
			}
		}
	}

	int rc = 0;
	switch (FUNCTION)
	{
		case 'r':
		case 'u':
			rc = fn_write(argv);
			break;

		case 'd':
			rc = (*argv) ? fn_delete(argv) : usage(cmd, 1);
			break;

		case 'x':
		case 'X':
			rc = fn_extract(argv);
			break;

		case 't':
			rc = fn_list(argv);
			break;
	}

	int entries = 0;
	int used = 0;
	int last = 0;
	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		if (get_word(p)) /* only non-empty directory entries */
		{
			entries++;
			int size = get_size(p + 37);
			if (size)
			{
				int blocks = (size + 511) / 512;
				used += blocks;
				int addr = get_word(p + 44);
				if (last < addr + blocks - 1) last = addr + blocks - 1;
			}
		}
		p += 64;
	}

	fprintf(stderr, "%4d entries\n", entries);
	fprintf(stderr, "%4d used\n", used);
	if ((DIR_BLOCKS == 24) && (used <= 553)) /* 553 = 578 - 1(boot) - 24(dir) */
	{
		/* also display free blocks for DECtape */
		fprintf(stderr, "%4d free\n", 553 - used);
	}
	fprintf(stderr, "%4d last\n", last);
	fprintf(stderr, "END\n");
	return rc;
}


/* output usage message */
int usage(const char *command, int status)
{
	fprintf(stderr, "%s - 'tp' format tape archive utility\n", command);
	fprintf(stderr, "Usage: %s key [name ...]\n", command);
	fprintf(stderr, "Key: one of:\n");
	fprintf(stderr, "  ?|h - display this message\n");
	fprintf(stderr, "  r   - replace named files on tape ('.' if no names given)\n");
	fprintf(stderr, "  u   - update named files on tape ('.' if no names given)\n");
	fprintf(stderr, "  d   - delete named files from tape (at least one name required)\n");
	fprintf(stderr, "  x   - extract named files from tape (all files if no names given\n");
	fprintf(stderr, "  X   - extract from tape to stdout (all files if no names given\n");
	fprintf(stderr, "  t   - list named files on tape (all files if no names given)\n");
	fprintf(stderr, "Key modifiers: any of:\n");
	fprintf(stderr, "  m   - use magtape rather than DECtape (implies 'c')\n");
	fprintf(stderr, "  M   - use magtape directory size only (does not imply 'c')\n");
	fprintf(stderr, "  0-7 - use specified tape unit (default: 0 for magtape, x for DECtape)\n");
	fprintf(stderr, "  F   - next name specifies tape file\n");
	fprintf(stderr, "  v   - display additional information\n");
	fprintf(stderr, "  c   - create new tape directory (usable only with 'r' and 'u')\n");
	fprintf(stderr, "  f   - new entries on tape are 'fake' (usable only with 'r' and 'u')\n");
	fprintf(stderr, "  i   - ignore tape I/O errors\n");
	fprintf(stderr, "  w   - wait for confirmation for each file\n");
	fprintf(stderr, "  a   - include names starting with '.' when writing directories\n");
	fprintf(stderr, "  U   - next name specifies uid (usable only with 'r' and 'u')\n");
	fprintf(stderr, "  G   - next name specifies gid (usable only with 'r' and 'u')\n");
	return status;
}


/* r/u - replace/update files  */
int fn_write(char **name)
{
	int fd, i;

	if (CREATE) /* create empty tape directory */
	{
		create_dir();
	}
	else /* read existing tape directory */
	{
		char *fn = tape_file_name();
		fd = open(fn, O_RDONLY);
		if (fd == -1) err(1, "Tape open error: %s", fn);
		off_t n = lseek(fd, 512, SEEK_SET);
		if (n == -1) err(1, "Seek error: %s", fn);
		read_dir(fd);
		close(fd);
	}

	if (*name == NULL) /* write "." if no names specified */
	{
		tp_dir_write(".");
	}
	else while (*name) /* write each specified name */
	{
		tp_dir_write(*(name++));
	}

	/* check for updates to tape directory */
	int dirty = 0;
	int last = DIR_BLOCKS;
	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		if (p[31]) dirty = 1; /* dirty flag */
		if (get_word(p))
		{
			int size = get_size(p + 37);
			if (size)
			{
				int blocks = (size + 511) / 512;
				int addr = get_word(p + 44);
				if (last < addr + blocks - 1) last = addr + blocks - 1;
			}
		}
		p += 64;
	}

	if (dirty) /* write out updates */
	{
		char *fn = tape_file_name();
		fd = open(fn, O_WRONLY | (TAPE_FILE ? O_CREAT : 0), 0600);
		if (fd == -1) err(1, "Tape open error: %s", fn);
		write_boot(fd);
		write_dir(fd);
		write_files(fd);
		if (TAPE_FILE) ftruncate(fd, (last + 1) * 512);
		close(fd);
	}

	return 0;
}


/* d - delete files */
int fn_delete(char **name)
{
	int fd, i;

	/* read existing tape directory */
	char *fn = tape_file_name();
	fd = open(fn, O_RDONLY);
	if (fd == -1) err(1, "Tape open error: %s", fn);
	off_t n = lseek(fd, 512, SEEK_SET);
	if (n == -1) err(1, "Seek error: %s", fn);
	read_dir(fd);
	close(fd);

	while (*name) /* delete each specified name */
	{
		tp_dir_delete(*(name++));
	}

	/* check for updates to tape directory */
	int dirty = 0;
	int last = DIR_BLOCKS;
	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		if (p[31]) dirty = 1; /* dirty flag */
		if (get_word(p))
		{
			int size = get_size(p + 37);
			if (size)
			{
				int blocks = (size + 511) / 512;
				int addr = get_word(p + 44);
				if (last < addr + blocks - 1) last = addr + blocks - 1;
			}
		}
		p += 64;
	}

	if (dirty) /* write out updates */
	{
		char *fn = tape_file_name();
		fd = open(fn, O_WRONLY | (TAPE_FILE ? O_CREAT : 0), 0600);
		if (fd == -1) err(1, "Tape open error: %s", fn);
		write_boot(fd);
		write_dir(fd);
		if (TAPE_FILE) ftruncate(fd, (last + 1) * 512);
		close(fd);
	}

	return 0;
}


/* x - extract files */
int fn_extract(char **name)
{
	int i;

	/* read existing tape directory */
	char *fn = tape_file_name();
	int fd = open(fn, O_RDONLY);
	if (fd == -1) err(1, "Tape open error: %s", fn);
	off_t n = lseek(fd, 512, SEEK_SET);
	if (n == -1) err(1, "Seek error: %s", fn);
	read_dir(fd);

	if (*name == NULL) /* extract all if no names specified */
	{
		tp_dir_extract(fd, NULL);
	}
	else while (*name) /* extract each specified name */
	{
		tp_dir_extract(fd, *(name++));
	}

	close(fd);
	return 0;
}


/* t - list table of contents */
int fn_list(char **name)
{
	int i;

	/* read existing tape directory */
	char *fn = tape_file_name();
	int fd = open(fn, O_RDONLY);
	if (fd == -1) err(1, "Tape open error: %s", fn);
	off_t n = lseek(fd, 512, SEEK_SET);
	if (n == -1) err(1, "Seek error: %s", fn);
	read_dir(fd);
	close(fd);

	if (VERBOSE) printf("   mode    uid gid tapa    size   date    time name\n");

	if (*name == NULL) /* list all if no names specified */
	{
		tp_dir_list(NULL);
	}
	else while (*name) /* list each specified name */
	{
		tp_dir_list(*(name++));
	}

	return 0;
}


/* get name of tape device or file */
char *tape_file_name()
{
	char *name = TAPE_FILE;
	if (name == NULL)
	{
		name = (DIR_BLOCKS == 24) ? "/dev/tapx" : "/dev/mt0";
		if (TAPE_UNIT) /* make name mutable, then update unit digit */
		{
			size_t len = strlen(name);
			DEV_NAME = realloc(DEV_NAME, len + 1);
			strlcpy(DEV_NAME, name, len + 1);
			DEV_NAME[--len] = TAPE_UNIT;
			name = DEV_NAME;
		}
	}
	return name;
}


/* initialize in-memory tape directory */
void create_dir()
{
	TAPE_DIR = calloc(DIR_BLOCKS, 512);
	if (TAPE_DIR == NULL) err(1, "unable to allocate tape directory buffer");
}


/* read into in-memory tape directory */
void read_dir(int fd)
{
	int i, j, k;

	create_dir();

	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_BLOCKS; i++)
	{
		size_t ct = read_buffer(fd, p, 512);
		if (ct == 0) err(1, "unable to read tape directory");
		for (j = 0; j < 8; j++)
		{
			int sum = 0;
			for (k = 0; k < 64; k += 2) sum = (sum + get_word(p + k)) & 65535;
			if (sum != 0) err(1, "Directory checksum");
			p += 64;
		}
	}
}


/* write tape boot block */
void write_boot(int fd)
{
	if (CREATE)
	{
		int8_t *buf = calloc(1, 512);
		if (buf == NULL) err(1, "unable to allocate tape buffer");

		/* copy tape bootstrap if available */		
		struct stat sb;
		char *name = (DIR_BLOCKS == 24) ? "/usr/mdec/tboot" : "/usr/mdec/mboot";
		if ((stat(name, &sb) != -1) && (S_ISREG(sb.st_mode)))
		{
			int d = open(name, O_RDONLY);
			if (d != -1) read_buffer(d, buf, 512);
			close(d);
		}

		/* write boot block */
		write_buffer(fd, buf, 512);
		free(buf);
	}
	else
	{
		off_t p = lseek(fd, 512, SEEK_SET);
		if (p == -1) err(1, "unable to seek to directory in %s", tape_file_name());
	}
}


/* write tape directory */
void write_dir(int fd)
{
	int i, j;

	int8_t *buf = malloc(512);
	if (buf == NULL) err(1, "unable to allocate tape buffer");

	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_BLOCKS; i++)
	{
		memcpy(buf, p, 512);
		for (j = 31; j < 512; j += 64) buf[j] = 0; /* clear dirty flags */
		write_buffer(fd, buf, 512);
		p += 512;
	}
}


/* write tape data blocks */
void write_files(int fd)
{
	int i;

	/* count number of files with data to be written */
	int dirty = 0;
	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		if (get_word(p))
		{
			if ((get_size(p + 37)) && (p[31])) dirty++;
		}
		p += 64;
	}
	if (dirty == 0) return;

	/* allocate buffer of zeros for writing unused blocks */
	int8_t *zero = calloc(1, 512);
	if (zero == NULL) err(1, "unable to allocate tape buffer");

	int cur_block = DIR_BLOCKS + 1;

	while (dirty--)
	{
		/* find entry with the lowest tape address */
		int8_t *entry = NULL;
		int addr = 0;
		int size = 0;
		p = TAPE_DIR;
		for (i = 0; i < DIR_ENTRIES; i++)
		{
			if (get_word(p))
			{
				int p_size = get_size(p + 37);
				if ((p_size) && (p[31]))
				{
					int p_addr = get_word(p + 44);
					if (p_addr >= cur_block)
					{
						if ((addr == 0) || (p_addr < addr))
						{
							entry = p;
							addr = p_addr;
							size = p_size;
						}
					}
				}
			}
			p += 64;
		}

		if (VERBOSE)
		{
			char f = entry[31];
			fprintf(stderr, "%c %s\n", f, entry);
		}
		entry[31] = 0;

		/* advance to desired tape address */
		while (addr > cur_block)
		{
			if (CREATE) write_buffer(fd, zero, 512);
			else lseek(fd, 512, SEEK_CUR);
			cur_block++;
		}

		/* write file data blocks */
		cur_block += write_tape_blocks(fd, entry, size);
	}
}


/* write name to directory */
void tp_dir_write(const char *name)
{
	int i;
	struct stat sb, esb;

	if (strlen(name) > 31)
	{
		err(1, "%s -- Name too long", name);
	}
	else if (stat(name, &sb) == -1)
	{
		err(1, "%s -- Cannot open file", name);
	}
	else if (S_ISDIR(sb.st_mode)) /* directory */
	{
		char *buf = NULL;
		int bufsize = 0;
		DIR *dir = opendir(name);
		if (dir == NULL) err(1, "unable to read directory %s", name);
		struct dirent *ent;
		while ((ent = readdir(dir)) != NULL)
		{
			if ((*(ent->d_name) == '.') && (!ALLFILES)) continue;
			if (strcmp(ent->d_name, ".") == 0) continue;
			if (strcmp(ent->d_name, "..") == 0) continue;
			int pathlen =  strlen(name) + ent->d_namlen + 2;
			if (pathlen > bufsize) /* grow pathname buffer */
			{
				bufsize = pathlen;
				buf = realloc(buf, bufsize);
				if (buf == NULL) err(1, "unable to allocate name buffer");
			}
			buf[0] = 0;
			strlcat(buf, name, bufsize);
			if (buf[strlen(buf) - 1] != '/') strlcat(buf, "/", bufsize);
			strlcat(buf, ent->d_name, bufsize);
			tp_dir_write(buf);
		}
		if (buf != NULL) free(buf);
		closedir(dir);
	}
	else if (S_ISREG(sb.st_mode)) /* regular file */
	{
		char f = FUNCTION;
		int8_t *entry = find_dir_entry(name);
		if (entry == NULL) /* name not found in directory; add file */
		{
			f = 'a';
			entry = find_dir_entry(NULL);
			if (entry == NULL) err(1, "tape directory full");
		}
		if (f == 'u') /* check if update is required */
		{
			if (sb.st_mtim.tv_sec <= get_dword(entry + 40)) return; /* skip */
			f = 'r'; /* replace */
		}
		int size = (FAKE) ? 0 : sb.st_size;
		if (size >= 16777215) /* check if size overflows 3 bytes */
		{
			err(1, "%s -- Size too big", name);
		}
		int addr = (FAKE) ? 0 : find_dir_blocks((size + 511) / 512);
		strlcpy(entry, name, 32);
		put_word(entry + 32, sb.st_mode & 65535);
		put_byte(entry + 34, (UID != -1) ? UID : (sb.st_uid > 255) ? 255 : sb.st_uid);
		put_byte(entry + 35, (GID != -1) ? GID : (sb.st_gid > 255) ? 255 : sb.st_gid);
		put_byte(entry + 36, 0);
		put_size(entry + 37, size);
		put_dword(entry + 40, sb.st_mtim.tv_sec);
		put_word(entry + 44, addr);
		for (i = 46; i < 62; i++) put_byte(entry + i, 0);
		int checksum = 0;
		for (i = 0; i < 62; i += 2) checksum += get_word(entry + i);
		put_word(entry + 62, 65536 - (checksum & 65535));
		put_byte(entry + 31, f); /* dirty flag */
	}
}


/* delete name from directory */
void tp_dir_delete(const char *name)
{
	int i;
	int8_t *entry;

	/* if name refers to a file delete it */
	if ((entry = find_dir_entry(name)) != NULL)
	{
		if (VERBOSE) fprintf(stderr, "d %s\n", entry);

		/* mark directory entry deleted */
		put_word(entry, 0);

		/* recalculate checksum */
		int checksum = 0;
		entry[31] = 0;
		for (i = 0; i < 62; i += 2) checksum += get_word(entry + i);
		put_word(entry + 62, 65536 - (checksum & 65535));

		/* set dirty flag */
		entry[31] = 'd';

		return;
	}

	/* if name refers to a directory delete all matching entries */
	int dir_len = strlen(name) + 2;
	char *dir_name = malloc(dir_len);
	if (dir_name == NULL) err(1, "unable to allocate name buffer");
	dir_name[0] = 0;
	strlcat(dir_name, name, dir_len);
	strlcat(dir_name, "/", dir_len);
	int ct = 0;
	while ((entry = find_dir_first(dir_name)) != NULL)
	{
		tp_dir_delete(entry);
		ct++;
	}
	if (ct == 0) printf("%s not found\n", name);
}


/* extract from directory */
void tp_dir_extract(int fd, const char *name)
{
	int i;
	int8_t *entry;

	/* if name is NULL extract all files */
	if (name == NULL)
	{
		entry = TAPE_DIR;
		for (i = 0; i < DIR_ENTRIES; i++)
		{
			if (get_word(entry)) tp_dir_extract(fd, entry);
			entry += 64;
		}
		return;
	}

	/* if name refers to a file extract it */
	if ((entry = find_dir_entry(name)) != NULL)
	{
		if (VERBOSE) fprintf(stderr, "x %s\n", entry);

		int addr = get_word(entry + 44);
		off_t n = lseek(fd, addr * 512, SEEK_SET);
		if (n == -1) err(1, "Seek error: %s", name);

		int mode = get_word(entry + 32) & 07777;
		int size = get_size(entry + 37);

		if (FUNCTION == 'X') copy_blocks(fd, STDOUT_FILENO, size, 0, entry);
		else read_tape_blocks(fd, entry, size, mode);

		time_t mtime = get_dword(entry + 40);
		struct timeval times[2];
		times[0].tv_sec = mtime;
		times[0].tv_usec = 0;
		times[1] = times[0];
		utimes(entry, times);

		uid_t uid = get_byte(entry + 34);
		gid_t gid = get_byte(entry + 35);
		chown(entry, uid, gid);
		chmod(entry, mode);

		return;
	}

	/* if name refers to a directory extract all matching entries */
	int dir_len = strlen(name) + 2;
	char *dir_name = malloc(dir_len);
	if (dir_name == NULL) err(1, "unable to allocate name buffer");
	dir_name[0] = 0;
	strlcat(dir_name, name, dir_len);
	if (dir_name[strlen(dir_name) - 1] != '/') strlcat(dir_name, "/", dir_len);
	int ct = 0;
	entry = TAPE_DIR;
	while ((entry = find_dir_next(dir_name, entry)) != NULL)
	{
		tp_dir_extract(fd, entry);
		ct++;
		entry += 64;
	}
	if (ct == 0) printf("%s not found\n", name);
	free(dir_name);
}


/* list directory */
void tp_dir_list(const char *name)
{
	int i;
	int8_t *entry;

	/* if name is NULL list all files */
	if (name == NULL)
	{
		entry = TAPE_DIR;
		for (i = 0; i < DIR_ENTRIES; i++)
		{
			if (get_word(entry)) tp_dir_list_entry(entry);
			entry += 64;
		}
		return;
	}

	/* if name refers to a file list it */
	if ((entry = find_dir_entry(name)) != NULL)
	{
		tp_dir_list_entry(entry);
		return;
	}

	/* if name refers to a directory list all matching entries */
	int dir_len = strlen(name) + 2;
	char *dir_name = malloc(dir_len);
	if (dir_name == NULL) err(1, "unable to allocate name buffer");
	dir_name[0] = 0;
	strlcat(dir_name, name, dir_len);
	if (dir_name[strlen(dir_name) - 1] != '/') strlcat(dir_name, "/", dir_len);
	int ct = 0;
	entry = TAPE_DIR;
	while ((entry = find_dir_next(dir_name, entry)) != NULL)
	{
		tp_dir_list_entry(entry);
		ct++;
		entry += 64;
	}
	if (ct == 0) printf("%s not found\n", name);
	free(dir_name);
}


/* list directory entry */
void tp_dir_list_entry(const int8_t *entry)
{
	if (VERBOSE)
	{
		const int8_t *p = entry + 32;
		int mode = get_word(p); p += 2;
		int uid = get_byte(p); p++;
		int gid = get_byte(p); p++;
		p++;
		int size = get_size(p); p += 3;
		time_t mod_time = get_dword(p); p += 4;
		int tape_addr = get_word(p); p += 2;
		struct tm *t = localtime(&mod_time);
		printf("%s %3d %3d %4d %8d ", mode_str(mode), uid, gid, tape_addr, size);
		printf("%2.2d/%2.2d/%2.2d ", t->tm_year % 100, t->tm_mon + 1, t->tm_mday);
		printf("%2.2d:%2.2d ", t->tm_hour, t->tm_min);
	}
	printf("%s\n", entry);
}


/* convert mode to 'rwxrwxrwx' string */
char *mode_str(int mode)
{
	MODE_BUF[0] = (mode & 0400) ? 'r' : '-';
	MODE_BUF[1] = (mode & 0200) ? 'w' : '-';
	MODE_BUF[2] = (mode & 04000) ? 's' : (mode & 0100) ? 'x' : '-';
	MODE_BUF[3] = (mode & 0040) ? 'r' : '-';
	MODE_BUF[4] = (mode & 0020) ? 'w' : '-';
	MODE_BUF[5] = (mode & 02000) ? 's' : (mode & 0010) ? 'x' : '-';
	MODE_BUF[6] = (mode & 0004) ? 'r' : '-';
	MODE_BUF[7] = (mode & 0002) ? 'w' : '-';
	MODE_BUF[8] = (mode & 01000) ? 's' : (mode & 0001) ? 'x' : '-';
	MODE_BUF[9] = 0;
	return MODE_BUF;
}


/* find directory entry (full name match) */
int8_t *find_dir_entry(const char *name)
{
	int i;

	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		if (get_word(p)) /* entry is in use */
		{
			if ((name) && (strncmp(p, name, 32) == 0)) return p; /* name found */
		}
		else if (name == NULL)
		{
			return p; /* unused entry found */
		}
		p += 64;
	}
	return NULL; /* not found */
}


/* find first directory entry (prefix name match) */
int8_t *find_dir_first(const char *name)
{
	return find_dir_next(name, TAPE_DIR);
}


/* find next directory entry (prefix name match) */
int8_t *find_dir_next(const char *name, int8_t *ptr)
{
	int i;

	int len = strlen(name);
	int8_t *limit = TAPE_DIR + DIR_ENTRIES * 64;
	while (ptr < limit)
	{
		if ((name != NULL) && (strncmp(ptr, name, len) == 0)) return ptr;
		if ((name == NULL) && (get_word(ptr) == 0)) return ptr;
		ptr += 64;
	}
	return NULL;
}


/* find available blocks from directory */
uint16_t find_dir_blocks(int blocks)
{
	int i, j;
	int addr = DIR_BLOCKS + 1; /* first potentially allocatable extent */
	int limit = addr + blocks; /* first block past extent */

	int8_t *p = TAPE_DIR;
	for (i = 0; i < DIR_ENTRIES; i++)
	{
		int e_size = get_size(p + 37);
		int e_addr = get_word(p + 44);
		int e_lim = e_addr + (e_size + 511) / 512;

		if ((get_word(p) == 0) || (e_size == 0))
		{
			/* skip unused or fake entry */
			p += 64;
			continue;
		}

		if ((e_addr < limit) && (e_lim > addr))
		{
			/* entry p covers some part of extent */
			addr = e_lim;
			limit = addr + blocks;
		}
		else if (e_addr >= limit)
		{
			/* entry p starts after extent, see if any later entry conflicts */
			int8_t *q = p + 64;
			int maybe = addr;
			for (j = i + 1; j < DIR_ENTRIES; j++)
			{
				e_addr = get_word(q + 44);
				e_size = get_size(q + 37);
				e_lim = e_addr + (e_size + 511) / 512;

				if ((get_word(q) == 0) || (e_size == 0))
				{
					q += 64;
					continue;
				}

				if ((e_addr < limit) && (e_lim > addr))
				{
					/* entry q covers some part of extent */
					addr = e_lim;
					limit = addr + blocks;
				}
				q += 64;
			}
			if (addr == maybe) return addr; /* confirmed available */
		}
		p += 64;
	}

	/* final extent has now been pushed past all conflicting entries */
	return addr;
}


/* read tape data blocks into file (extract) */
int read_tape_blocks(int fd, char *path, int size, int mode)
{
	int tgt = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (tgt == -1)
	{
		printf("%s -- create error\n", path);
		return 0;
	}

	int blocks = copy_blocks(fd, tgt, size, 0, path);

	close(tgt);
	return blocks;
}


/* write tape data blocks from file (record) */
int write_tape_blocks(int fd, char *path, int size)
{
	int src = open(path, O_RDONLY);
	if (src == -1) err(1, "%s -- Cannot open file", path);

	int blocks = copy_blocks(src, fd, size, 1, path);

	close(src);
	return blocks;
}


/* copy data blocks */
int copy_blocks(int src_fd, int tgt_fd, size_t nbytes, int pad, char *name)
{
	size_t len;

	int blocks = 0;

	int8_t *buf = malloc(512);
	if (buf == NULL) err(1, "unable to allocate tape buffer");

	while (nbytes > 0)
	{
		len = nbytes;
		if (len > 512) len = 512;
		size_t ct = read_buffer(src_fd, buf, len);
		if (ct < len) err(1, "unable to read block for file %s", name);
		if (pad) while (ct < 512) buf[ct++] = 0;
		write_buffer(tgt_fd, buf, ct);
		blocks++;
		nbytes -= len;
	}

	free(buf);
	return blocks;
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
void write_buffer(int fd, void *buf, size_t nbytes)
{
	size_t p = 0;
	while (p < nbytes)
	{
		ssize_t ct = write(fd, buf + p, nbytes - p);
		if (ct == -1) err(1, NULL);
		p += ct;
	}
}


/* get a byte from a buffer */
uint8_t get_byte(const int8_t *buf)
{
	return *buf;
}


/* get a 2-byte word from a buffer (PDP-11 byte order) */
uint16_t get_word(const int8_t *buf)
{
	return (get_byte(buf + 1) << 8) | get_byte(buf);
}


/* get a 3-byte file size from a buffer (high byte, then low word) */
uint32_t get_size(const int8_t *buf)
{
	return (get_byte(buf) << 16) | get_word(buf + 1);
}


/* get a 4-byte dword from a buffer (PDP-11 byte order) */
uint32_t get_dword(const int8_t *buf)
{
	return (get_word(buf) << 16) | get_word(buf + 2);
}


/* put a byte into a buffer */
int8_t *put_byte(int8_t *buf, uint8_t byte)
{
	*(buf++) = byte;
	return buf;
}


/* put a 2-byte word into a buffer (PDP-11 byte order) */
int8_t *put_word(int8_t *buf, uint16_t word)
{
	buf = put_byte(buf, word & 255);
	return put_byte(buf, word >> 8);
}


/* put a 3-byte size into a buffer (high byte, the low word) */
int8_t *put_size(int8_t *buf, uint32_t size)
{
	buf = put_byte(buf, (size >> 16) & 255);
	return put_word(buf, size & 65535);
}


/* put a 4-byte dword into a buffer (PDP-11 byte order) */
int8_t *put_dword(int8_t *buf, uint32_t dword)
{
	buf = put_word(buf, dword >> 16);
	return put_word(buf, dword & 65535);
}
