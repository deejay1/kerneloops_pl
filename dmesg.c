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

#include <asm/unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kerneloops.h"


static char** linepointer;

static int linecount;


/* 
 * This function splits the dmesg buffer data into lines
 * (null terminated). The linepointer array is assumed to be 
 * allocated already.
 */
static void fill_linepointers(char *buffer, int remove_syslog)
{
	char *c;
	linecount = 0;
	c = buffer;
	while (c) {
		/* in /var/log/messages, we need to strip the first part off, upto the 3rd ':' */
		if (remove_syslog) {
			c = strchr(c, ':');
			if (!c) break;
			c++;
			c = strchr(c, ':');
			if (!c) break;
			c++;
			c = strchr(c, ':');
			if (!c) break;
			c++;
			if (*c) c++;
		}

		linepointer[linecount] = c;
		if (*c=='<' && *(c+2)=='>') {
			c = c + 3;
			linepointer[linecount] = c;
		}
		c = strchr(c, '\n'); /* turn the \n into a string termination */
		if (c) {
			*c = 0;
			c = c+1;
		}
		/* if we see our own marker, we know we submitted everything upto here already */
		if (strstr(linepointer[linecount], "www.kerneloops.org")) {
			linecount = 0;
			linepointer[0] = NULL;
		}
		linecount++;
	}
}


/*
 * extract_oops tries to find oops signatures in a log 
 */
static void extract_oops(char *buffer, int remove_syslog)
{
	int i;
	
	int oopsstart = -1;
	int oopsend;
	int inbacktrace = 0;

	linepointer = calloc(strlen(buffer), sizeof(char*));
	if (linepointer==NULL)
		return;


	fill_linepointers(buffer, remove_syslog);

	oopsend = linecount;

	i = 0;
	while (i < linecount) {
		if (linepointer[i]==NULL) {
			i++;
			continue;
		}
		if (oopsstart < 0) { /* find start-of-oops markers */
			if (strstr(linepointer[i], "general protection fault:"))
				oopsstart = i;
			if (strstr(linepointer[i], "BUG:"))
				oopsstart = i;
			if (strstr(linepointer[i], "kernel BUG at"))
				oopsstart = i;
			if (strstr(linepointer[i], "WARNING:"))
				oopsstart = i;
			if (strstr(linepointer[i], "Unable to handle kernel"))
				oopsstart = i;
			if (strstr(linepointer[i], "------------[ cut here ]------------"))
				oopsstart = i;
			if (strstr(linepointer[i], "Modules linked in:") && i>=4)
				oopsstart = i-4;
			if (strstr(linepointer[i], "Oops:") && i>=3)
				oopsstart = i-3;
		}

		/* a calltrace starts with "Call Trace:" or with the " [<.......>] function+0xFF/0xAA" pattern */
		if (oopsstart >=0 && strstr(linepointer[i], "Call Trace:"))
			inbacktrace = 1;

		if (oopsstart >=0 && inbacktrace == 0 && strlen(linepointer[i])>8) {
			char *c1, *c2, *c3;
			c1 = strstr(linepointer[i], ">]");
			c2 = strstr(linepointer[i], "+0x");
			c3 = strstr(linepointer[i], "/0x");
			if (linepointer[i][0] == ' ' && linepointer[i][1]=='[' && linepointer[i][2]=='<' && c1 && c2 && c3)
				inbacktrace = 1;
		}

		if (oopsstart>=0 && inbacktrace>0) {
			char *c1, c2,c3;
			c1 = strstr(linepointer[i], "Code:");
			c2 = linepointer[i][0];
			c3 = linepointer[i][1];
			if (c1!=NULL || c2 != ' ' || c3 != '[' || strlen(linepointer[i])<8) {
				int len;
				char *oops;
				oopsend = i-1;


				len = 2;
				for (i=oopsstart; i<=oopsend; i++) 
					len += strlen(linepointer[i])+1;
				
				oops = calloc(len, 1);

				for (i=oopsstart; i<=oopsend; i++) {
					strcat(oops, linepointer[i]);
					strcat(oops, "\n");
				}
				queue_oops(oops);
				oopsstart = -1;
				inbacktrace = 0;
				oopsend=linecount;
				free(oops);
			}
		}
		i++;
	}
	if (oopsstart>=0)  {
		char *oops;
		int len;

		len = 2;
		for (i=oopsstart; i<=oopsend; i++) 
			len += strlen(linepointer[i])+1;
				
		oops = calloc(len, 1);

		for (i=oopsstart; i<=oopsend; i++) {	
			strcat(oops, linepointer[i]);
			strcat(oops, "\n");
		}
		queue_oops(oops);
		oopsstart = -1;
		inbacktrace = 0;
		free(oops);
	}
	free(linepointer);
	linepointer = NULL;
}

void scan_dmesg(void)
{
	
	char *buffer;

	buffer = calloc(getpagesize()+1, 1);

	syscall(__NR_syslog, 3, buffer, getpagesize());
	extract_oops(buffer, 0);
	free(buffer);
	submit_queue();
}

void scan_var_log_messages(void)
{
	char *buffer;
	struct stat statb;
	FILE *file;
	int ret;

	memset(&statb, 0, sizeof(statb));

	ret = stat("/var/log/messages", &statb);

	if (statb.st_size<1 || ret!=0)
		return;

	buffer = calloc(statb.st_size+1024,1);
	file = fopen("/var/log/messages", "rm");
	if (!file) {
		free(buffer);
		return;
	}
	ret = fread(buffer, 1, statb.st_size+1023, file);
	fclose(file);

	if (ret > 0)
		extract_oops(buffer, 1);
	free(buffer);
	submit_queue();

}
