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

#include <curl/curl.h>

#include "kerneloops.h"

int testmode = 0;
int main(int argc, char**argv)
{
	int godaemon = 1;

	read_config_file("/etc/kerneloops.org");


	if (!opted_in) {
		fprintf(stderr, " [Inactive by user preference]");
		return EXIT_SUCCESS;
	}

	/* 
	 * the curl docs say that we "should" call curl_global_init early, 
	 * even though it'll be called later on via curl_easy_init().
	 * We ignore this advice, since 99.99% of the time this program
	 * will not use http at all, but the curl code does consume
	 * memory.
	 */
   
/*
	curl_global_init(CURL_GLOBAL_ALL);
*/

	if (argc>1 && strstr(argv[1], "--nodaemon"))
		godaemon = 0;
	if (argc>1 && strstr(argv[1], "--debug")) {
		printf("Starting kerneloops in debug mode\n");
		godaemon = 0;
		testmode = 1;
	}

	if (godaemon && daemon(0,0)) {
		printf("kerneloops failed to daemonize.. exiting \n");
		return EXIT_FAILURE;
	}

	/* we scan dmesg before /var/log/messages; dmesg is a more accurate source normally */
	scan_dmesg();
	scan_filename("/var/log/messages", 1);
	if (testmode && argc>2 && argv[2]) {
		printf("Scanning %s\n", argv[2]);
		scan_filename(argv[2], 0);
	}

	if (testmode)
		return EXIT_SUCCESS;

	/* now, start polling for oopses to occur */
	while (1) {
		sleep(10);
		scan_dmesg();
	}

	return EXIT_SUCCESS;
}
