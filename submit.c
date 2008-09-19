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

#define _BSD_SOURCE
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>

#include <asm/unistd.h>

#include <curl/curl.h>

#include "kerneloops.h"


/*
 * we keep track of 16 checksums of the last submitted oopses; this allows us to
 * make sure we don't submit the same oops twice (this allows us to not have to do
 * really expensive things during non-destructive dmesg-scanning)
 *
 * This also limits the number of oopses we'll submit per session;
 * it's important that this is bounded to avoid feedback loops
 * for the scenario where submitting an oopses causes a warning/oops
 */
#define MAX_CHECKSUMS 16
static unsigned int checksums[MAX_CHECKSUMS];
static int submitted;

struct oops;

struct oops {
	struct oops *next;
	char *text;
	unsigned int checksum;
};

/* we queue up oopses, and then submit in a batch.
 * This is useful to be able to cancel all submissions, in case
 * we later find our marker indicating we submitted everything so far already
 * previously.
 */
static struct oops *queued_oopses;
static int newoops;

/* For communicating details to the applet, we write the
 * details in a file, and provide the filename to the applet
 */
static char *detail_filename;


static unsigned int checksum(char *ptr)
{
	unsigned int temp = 0;
	unsigned char *c;
	c = (unsigned char *) ptr;
	while (c && *c) {
		temp = (temp) + *c;
		c++;
	}
	return temp;
}

void queue_oops(char *oops)
{
	int i;
	unsigned int sum;
	struct oops *new;

	if (submitted >= MAX_CHECKSUMS-1)
		return;
	/* first, check if we haven't already submitted the oops */
	sum = checksum(oops);
	for (i = 0; i < submitted; i++) {
		if (checksums[i] == sum) {
			printf("Match with oops %i (%x)\n", i, sum);
			return;
		}
	}
	checksums[submitted++] = sum;

	new = malloc(sizeof(struct oops));
	memset(new, 0, sizeof(struct oops));
	new->next = queued_oopses;
	new->checksum = sum;
	new->text = strdup(oops);
	queued_oopses = new;
	newoops = 1;
}


void write_detail_file(void)
{
	int temp_fileno;
	FILE *tmpf;
	struct oops *oops;
	int count = 0;

	detail_filename = strdup("/tmp/kerneloops.XXXXXX");
	temp_fileno = mkstemp(detail_filename);
	if (temp_fileno < 0) {
		free(detail_filename);
		detail_filename = NULL;
		return;
	}
	/* regular user must be able to read this detail file to be
	 * useful; there is nothing worth doing if fchmod fails.
	 */
	fchmod(temp_fileno, 0644);
	tmpf = fdopen(temp_fileno, "w");
	oops = queued_oopses;
	while (oops) {
		count++; /* Users are not programmers, start at 1 */
		fprintf(tmpf, "Kernel failure message %d:\n", count);
		fprintf(tmpf, "%s", oops->text);
		fprintf(tmpf, "\n\n");
		oops = oops->next;
	}
	fclose(tmpf);
	close(temp_fileno);
}

void unlink_detail_file(void)
{
	if (detail_filename) {
		unlink(detail_filename);
		free(detail_filename);
	}
}


static void print_queue(void)
{
	struct oops *oops;
	struct oops *queue;
	int count = 0;

	queue = queued_oopses;
	queued_oopses = NULL;
	barrier();
	oops = queue;
	while (oops) {
		struct oops *next;

		printf("Submit text is:\n---[start of oops]---\n%s\n---[end of oops]---\n", oops->text);
		next = oops->next;
		free(oops->text);
		free(oops);
		oops = next;
		count++;
	}

}

static void write_logfile(int count)
{
	openlog("kerneloops", 0, LOG_KERN);
	syslog(LOG_WARNING, "Submitted %i kernel oopses to www.kerneloops.org", count);
	closelog();
}

char result_url[4096];

size_t writefunction( void *ptr, size_t size, size_t nmemb, void __attribute((unused)) *stream)
{
	char *c, *c1, *c2;
	c = malloc(size*nmemb + 1);
	memset(c, 0, size*nmemb + 1);
	memcpy(c, ptr, size*nmemb);
	printf("received %s \n", c);
	c1 = strstr(c, "201 ");
	if (c1) {
		c1+=4;
		c2 = strchr(c1, '\n');
		if (c2) *c2 = 0;
		strncpy(result_url, c1, 4095);
	}
	return size * nmemb;
}

void submit_queue(void)
{
	int result;
	struct oops *oops;
	struct oops *queue;
	int count = 0;

	memset(result_url, 0, 4096);

	if (testmode) {
		print_queue();
		return;
	}

	queue = queued_oopses;
	queued_oopses = NULL;
	barrier();
	oops = queue;
	while (oops) {
		CURL *handle;
		struct curl_httppost *post = NULL;
		struct curl_httppost *last = NULL;
		struct oops *next;

		handle = curl_easy_init();

		printf("DEBUG SUBMIT URL is %s \n", submit_url);
		curl_easy_setopt(handle, CURLOPT_URL, submit_url);

		/* set up the POST data */
		curl_formadd(&post, &last,
			CURLFORM_COPYNAME, "oopsdata",
			CURLFORM_COPYCONTENTS, oops->text, CURLFORM_END);

		if (allow_distro_to_pass_on) {
			curl_formadd(&post, &last,
				CURLFORM_COPYNAME, "pass_on_allowed",
				CURLFORM_COPYCONTENTS, "yes", CURLFORM_END);
		}

		curl_easy_setopt(handle, CURLOPT_HTTPPOST, post);
		curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writefunction);
		result = curl_easy_perform(handle);

		curl_formfree(post);
		curl_easy_cleanup(handle);
		next = oops->next;
		free(oops->text);
		free(oops);
		oops = next;
		count++;
	}

	if (count && !testmode)
		write_logfile(count);

	if (count)
		dbus_say_thanks(result_url);
	/*
	 * If we've reached the maximum count, we'll exit the program,
	 * the program won't do any useful work anymore going forward.
	 */
	if (submitted >= MAX_CHECKSUMS-1) {
		unlink_detail_file();
		exit(EXIT_SUCCESS);
	}
}

void clear_queue(void)
{
	struct oops *oops, *next;
	struct oops *queue;

	queue = queued_oopses;
	queued_oopses = NULL;
	barrier();
	oops = queue;
	while (oops) {
		next = oops->next;
		free(oops->text);
		free(oops);
		oops = next;
	}
	write_logfile(0);
}

void ask_permission(void)
{
	if (!newoops && !pinged)
		return;
	pinged = 0;
	newoops = 0;
	if (queued_oopses) {
		write_detail_file();
		dbus_ask_permission(detail_filename);
	}
}
