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
#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "kerneloops.h"

/* 0 =  No
   1 =  Ask
   2 =  Yes
 */
int opted_in;
int allow_distro_to_pass_on;
char *submit_url;
char *log_file;


void read_config_file(char *filename)
{
	FILE *file;
	char *line = NULL;
	size_t dummy;
	file = fopen(filename, "r");
	if (!file)
		return;
	while (!feof(file)) {
		char *c;
		line = NULL;
		if (getline(&line, &dummy, file) <= 0) {
			free(line);
			break;
		}
		if (line[0] == '#') {
			free(line);
			continue;
		}
		c = strstr(line, "allow-submit ");
		if (c) {
			c += 13;
			if (strstr(c, "yes"))
				opted_in = 2;
			if (strstr(c, "ask"))
				opted_in = 1;
		}
		c = strstr(line, "allow-pass-on ");
		if (c) {
			c += 14;
			if (strstr(c, "yes"))
				allow_distro_to_pass_on = 1;
		}
		c = strstr(line, "submit-url ");
		if (c) {
			c += 11;
			c = strstr(c, "http:");
			if (c)
				submit_url = strdup(c);
		}
		c = strstr(line, "log-file ");
		if (c) {
			c += 9;
			while(*c) {
				if ( !isspace(*c) && *c != '=')
					break;
				c++;
			}
			if (*c)
				log_file = strdup(c);
		}
		free(line);
	}
	fclose(file);
	if (!submit_url)
		submit_url = strdup("http://submit.kerneloops.org/submitoops.php");
	if (!log_file)
		log_file = strdup("/var/log/messages");
}
