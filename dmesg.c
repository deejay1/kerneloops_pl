#define _GNU_SOURCE
/*
 * Copyright 2007, Intel Corporation
 *
 * This file is part of kerneloops.org
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * Authors:
 * 	Arjan van de Ven <arjan@linux.intel.com>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kerneloops.h"


struct line_info {
	char *ptr;
	char level;
};

static struct line_info *lines_info;
static int lines_info_alloc;
static int linecount;

#define MIN(A,B) ((A) < (B) ? (A) : (B))

#define REALLOC_CHUNK 1000
static int set_line_info(int index, char *linepointer, char linelevel)
{
	if (index >= lines_info_alloc) {
		struct line_info *new_info;
		new_info = realloc(lines_info,
			(lines_info_alloc + REALLOC_CHUNK) * sizeof(struct line_info));
		if (!new_info)
			return -1;
		lines_info_alloc += REALLOC_CHUNK;
		lines_info = new_info;
	}

	lines_info[index].ptr = linepointer;
	lines_info[index].level = linelevel;
	return 0;
}

/*
 * This function splits the dmesg buffer data into lines
 * (null terminated).
 */
static int fill_lineinfo(char *buffer, size_t buflen, int remove_syslog)
{
	char *c, *linepointer, linelevel;
	linecount = 0;
	if (!buflen)
		return 0;
	buffer[buflen - 1] = '\n';  /* the buffer usually ends with \n, but let's make sure */
	c = buffer;
	while (c < buffer + buflen) {
		int len = 0;
		char *c9;

		c9 = memchr(c, '\n', buffer + buflen - c); /* a \n will always be found */
		assert(c9);
		len = c9 - c;

		/* in /var/log/messages, we need to strip the first part off, upto the 3rd ':' */
		if (remove_syslog) {
			char *c2;
			int i;

			/* skip non-kernel lines */
			c2 = memmem(c, len, "kernel:", 7);
			if (!c2)
				c2 = memmem(c, len, "kerneloops:", 11);
			if (!c2)
				goto next_line;

			/* skip to message in "Jan 01 01:23:45 hostname kernel: message" */
			for (i = 0; i < 3; i++) {
				c = memchr(c, ':', len);
				if (!c)
					goto next_line;
				c++;
				len = c9 - c;
			}
			c++;
			len--;
		}

		linepointer = c;
		linelevel = 0;
		/* store and remove kernel log level */
		if (len >= 3 && *c == '<' && *(c+2) == '>') {
			linelevel = *(c+1);
			c += 3;
			len -= 3;
			linepointer = c;
		}
		/* remove jiffies time stamp counter if present */
		if (*c == '[') {
			char *c2, *c3;
			c2 = memchr(c, '.', len);
			c3 = memchr(c, ']', len);
			if (c2 && c3 && (c2 < c3) && (c3-c) < 14 && (c2-c) < 8) {
				c = c3 + 1;
				if (*c == ' ')
					c++;
				len = c9 - c;
				linepointer = c;
			}
		}

		assert(c + len == c9);
		*c9 = '\0'; /* turn the \n into a string termination */

		/* if we see our own marker, we know we submitted everything upto here already */
		if (memmem(linepointer, len, "www.kerneloops.org", 18)) {
			linecount = 0;
			lines_info[0].ptr = NULL;
		}
		if (set_line_info(linecount, linepointer, linelevel) < 0)
			return -1;
		linecount++;
next_line:
		c = c9 + 1;
	}
	return 0;
}

/*
 * extract_oops tries to find oops signatures in a log
 */
static void extract_oops(char *buffer, size_t buflen, int remove_syslog)
{
	int i;
	char prevlevel = 0;
	int oopsstart = -1;
	int oopsend;
	int inbacktrace = 0;

	lines_info = NULL;
	lines_info_alloc = 0;

	if (fill_lineinfo(buffer, buflen, remove_syslog) < 0)
		goto fail;

	oopsend = linecount;

	i = 0;
	while (i < linecount) {
		char *c = lines_info[i].ptr;

		if (c == NULL) {
			i++;
			continue;
		}
		if (oopsstart < 0) {
			/* find start-of-oops markers */
			if (strstr(c, "general protection fault:"))
				oopsstart = i;
			if (strstr(c, "BUG:"))
				oopsstart = i;
			if (strstr(c, "kernel BUG at"))
				oopsstart = i;
			if (strstr(c, "do_IRQ: stack overflow:"))
				oopsstart = i;
			if (strstr(c, "RTNL: assertion failed"))
				oopsstart = i;
			if (strstr(c, "Eeek! page_mapcount(page) went negative!"))
				oopsstart = i;
			if (strstr(c, "near stack overflow (cur:"))
				oopsstart = i;
			if (strstr(c, "double fault:"))
				oopsstart = i;
			if (strstr(c, "Badness at"))
				oopsstart = i;
			if (strstr(c, "NETDEV WATCHDOG"))
				oopsstart = i;
			if (strstr(c, "WARNING:") &&
			    !strstr(c, "appears to be on the same physical disk"))
				oopsstart = i;
			if (strstr(c, "Unable to handle kernel"))
				oopsstart = i;
			if (strstr(c, "sysctl table check failed"))
				oopsstart = i;
			if (strstr(c, "------------[ cut here ]------------"))
				oopsstart = i;
			if (strstr(c, "list_del corruption."))
				oopsstart = i;
			if (strstr(c, "list_add corruption."))
				oopsstart = i;
			if (strstr(c, "Oops:") && i >= 3)
				oopsstart = i-3;
			if (oopsstart >= 0 && testmode) {
				printf("Found start of oops at line %i\n", oopsstart);
				printf("    start line is -%s-\n", lines_info[oopsstart].ptr);
				if (oopsstart != i)
					printf("    trigger line is -%s-\n", c);
			}

			/* try to find the end marker */
			if (oopsstart >= 0) {
				int i2;
				i2 = i+1;
				while (i2 < linecount && i2 < (i+50)) {
					if (strstr(lines_info[i2].ptr, "---[ end trace")) {
						inbacktrace = 1;
						i = i2;
						break;
					}
					i2++;
				}
			}
		}

		/* a calltrace starts with "Call Trace:" or with the " [<.......>] function+0xFF/0xAA" pattern */
		if (oopsstart >= 0 && strstr(lines_info[i].ptr, "Call Trace:"))
			inbacktrace = 1;

		else if (oopsstart >= 0 && inbacktrace == 0 && strlen(lines_info[i].ptr) > 8) {
			char *c1, *c2, *c3;
			c1 = strstr(lines_info[i].ptr, ">]");
			c2 = strstr(lines_info[i].ptr, "+0x");
			c3 = strstr(lines_info[i].ptr, "/0x");
			if (lines_info[i].ptr[0] == ' ' && lines_info[i].ptr[1] == '[' && lines_info[i].ptr[2] == '<' && c1 && c2 && c3)
				inbacktrace = 1;
		} else

		/* try to see if we're at the end of an oops */

		if (oopsstart >= 0 && inbacktrace > 0) {
			char c2, c3;
			c2 = lines_info[i].ptr[0];
			c3 = lines_info[i].ptr[1];

			/* line needs to start with " [" or have "] ["*/
			if ((c2 != ' ' || c3 != '[') &&
				strstr(lines_info[i].ptr, "] [") == NULL &&
				strstr(lines_info[i].ptr, "--- Exception") == NULL &&
				strstr(lines_info[i].ptr, "    LR =") == NULL &&
				strstr(lines_info[i].ptr, "<#DF>") == NULL &&
				strstr(lines_info[i].ptr, "<IRQ>") == NULL &&
				strstr(lines_info[i].ptr, "<EOI>") == NULL &&
				strstr(lines_info[i].ptr, "<<EOE>>") == NULL)
				oopsend = i-1;

			/* oops lines are always more than 8 long */
			if (strlen(lines_info[i].ptr) < 8)
				oopsend = i-1;
			/* single oopses are of the same loglevel */
			if (lines_info[i].level != prevlevel)
				oopsend = i-1;
			/* The Code: line means we're done with the backtrace */
			if (strstr(lines_info[i].ptr, "Code:") != NULL)
				oopsend = i;
			if (strstr(lines_info[i].ptr, "Instruction dump::") != NULL)
				oopsend = i;
			/* if a new oops starts, this one has ended */
			if (strstr(lines_info[i].ptr, "WARNING:") != NULL && oopsstart != i)
				oopsend = i-1;
			if (strstr(lines_info[i].ptr, "Unable to handle") != NULL && oopsstart != i)
				oopsend = i-1;
			/* kernel end-of-oops marker */
			if (strstr(lines_info[i].ptr, "---[ end trace") != NULL)
				oopsend = i;

			if (oopsend <= i) {
				int q;
				int len;
				char *oops;

				len = 2;
				for (q = oopsstart; q <= oopsend; q++)
					len += strlen(lines_info[q].ptr)+1;

				oops = calloc(len, 1);

				for (q = oopsstart; q <= oopsend; q++) {
					strcat(oops, lines_info[q].ptr);
					strcat(oops, "\n");
				}
				/* too short oopses are invalid */
				if (strlen(oops) > 100)
					queue_oops(oops);
				oopsstart = -1;
				inbacktrace = 0;
				oopsend = linecount;
				free(oops);
			}
		}
		prevlevel = lines_info[i].level;
		i++;
		if (oopsstart > 0 && i-oopsstart > 50) {
			oopsstart = -1;
			inbacktrace = 0;
			oopsend = linecount;
		}
		if (oopsstart > 0 && !inbacktrace && i-oopsstart > 30) {
			oopsstart = -1;
			inbacktrace = 0;
			oopsend = linecount;
		}
	}
	if (oopsstart >= 0)  {
		char *oops;
		int len;
		int q;

		oopsend = i-1;

		len = 2;
		while (oopsend > 0 && lines_info[oopsend].ptr == NULL)
			oopsend--;
		for (q = oopsstart; q <= oopsend; q++)
			len += strlen(lines_info[q].ptr)+1;

		oops = calloc(len, 1);

		for (q = oopsstart; q <= oopsend; q++) {
			strcat(oops, lines_info[q].ptr);
			strcat(oops, "\n");
		}
		/* too short oopses are invalid */
		if (strlen(oops) > 100)
			queue_oops(oops);
		oopsstart = -1;
		inbacktrace = 0;
		oopsend = linecount;
		free(oops);
	}
fail:
	free(lines_info);
	lines_info = NULL;
}

int scan_dmesg(void __unused *unused)
{
	char *buffer;

	buffer = calloc(getpagesize()+1, 1);

	syscall(__NR_syslog, 3, buffer, getpagesize());
	extract_oops(buffer, strlen(buffer), 0);
	free(buffer);
	if (opted_in >= 2)
		submit_queue();
	else if (opted_in >= 1)
		ask_permission();
	return 1;
}

void scan_filename(char *filename, int issyslog)
{
	char *buffer;
	struct stat statb;
	FILE *file;
	int ret;
	size_t buflen, nread;

	memset(&statb, 0, sizeof(statb));

	ret = stat(filename, &statb);

	if (statb.st_size < 1 || ret != 0)
		return;

	/*
	 * in theory there's a race here, since someone could spew
	 * to /var/log/messages before we read it in... we try to
	 * deal with it by reading at most 1023 bytes extra. If there's
	 * more than that.. any oops will be in dmesg anyway.
	 * Do not try to allocate an absurd amount of memory; ignore
	 * older log messages because they are unlikely to have
	 * sufficiently recent data to be useful.  32MB is more
	 * than enough; it's not worth looping through more log
	 * if the log is larger than that.
	 */
	buflen = MIN(statb.st_size+1024, 32*1024*1024);
	buffer = calloc(buflen, 1);
	assert(buffer != NULL);

	file = fopen(filename, "rm");
	if (!file) {
		free(buffer);
		return;
	}
	fseek(file, -buflen, SEEK_END);
	nread = fread(buffer, 1, buflen, file);
	fclose(file);

	if (nread > 0)
		extract_oops(buffer, nread, issyslog);
	free(buffer);
	if (opted_in >= 2)
		submit_queue();
	else if (opted_in >= 1)
		ask_permission();
}
