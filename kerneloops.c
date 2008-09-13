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
#include <sched.h>
#include <sys/prctl.h>
#include <asm/unistd.h>

#include <curl/curl.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>



/*
 * Debian etch has an ancient glib2 library, work around
 */
#if !GLIB_CHECK_VERSION(2, 14, 0)
#define g_timeout_add_seconds(a, b, c) g_timeout_add((a)*1000, b, c)
#endif


#include "kerneloops.h"

static DBusConnection *bus;

int pinged;
int testmode;

static DBusHandlerResult got_message(
		DBusConnection __unused *conn,
		DBusMessage *message,
		void __unused *user_data)
{
	if (dbus_message_is_signal(message,
		"org.kerneloops.submit.ping", "ping")) {
		pinged = 1;
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	if (dbus_message_is_signal(message,
		"org.kerneloops.submit.permission", "yes")) {
		submit_queue();
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.kerneloops.submit.permission", "always")) {
		submit_queue();
		opted_in = 2;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.kerneloops.submit.permission", "never")) {
		clear_queue();
		opted_in = 0;
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_signal(message,
		"org.kerneloops.submit.permission", "no")) {
		clear_queue();
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void dbus_ask_permission(char * detail_file_name)
{
	DBusMessage *message;
	if (!bus)
		return;
	message = dbus_message_new_signal("/org/kerneloops/submit/permission",
			"org.kerneloops.submit.permission", "ask");
	if (detail_file_name) {
		dbus_message_append_args(message,
			DBUS_TYPE_STRING, &detail_file_name,
			DBUS_TYPE_INVALID);
	}
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

void dbus_say_thanks(char *url)
{
	DBusMessage *message;
	if (!bus)
		return;
	if (url && strlen(url)) {
		message = dbus_message_new_signal("/org/kerneloops/submit/url",
			"org.kerneloops.submit.url", "url");
		dbus_message_append_args (message, DBUS_TYPE_STRING, &url, DBUS_TYPE_INVALID);
		dbus_connection_send(bus, message, NULL);
		dbus_message_unref(message);
	}

	message = dbus_message_new_signal("/org/kerneloops/submit/sent",
			"org.kerneloops.submit.sent", "sent");
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

int main(int argc, char**argv)
{
	GMainLoop *loop;
	DBusError error;
	int godaemon = 1;

/*
 * Signal the kernel that we're not timing critical
 */
#ifdef PR_SET_TIMERSLACK,1000
	prctl(PR_SET_TIMERSLACK,1000*1000*1000, 0, 0, 0);
#endif

	read_config_file("/etc/kerneloops.conf");

	if (argc > 1 && strstr(argv[1], "--nodaemon"))
		godaemon = 0;
	if (argc > 1 && strstr(argv[1], "--debug")) {
		printf("Starting kerneloops in debug mode\n");
		godaemon = 0;
		testmode = 1;
		opted_in = 2;
	}

	if (!opted_in && !testmode) {
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

	if (godaemon && daemon(0, 0)) {
		printf("kerneloops failed to daemonize.. exiting \n");
		return EXIT_FAILURE;
	}
	sched_yield();

	loop = g_main_loop_new(NULL, FALSE);
	dbus_error_init(&error);
	bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (bus) {
		dbus_connection_setup_with_g_main(bus, NULL);
		dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.ping'", &error);
		dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.permission'", &error);
		dbus_connection_add_filter(bus, got_message, NULL, NULL);
	}

	/* we scan dmesg before /var/log/messages; dmesg is a more accurate source normally */
	scan_dmesg(NULL);
	/* during boot... don't go too fast and slow the system down */
	if (!testmode)
		sleep(10);
	scan_filename("/var/log/messages", 1);

	if (argc > 2 && strstr(argv[1], "--file"))
		scan_filename(argv[2], 1);

	if (testmode && argc > 2) {
		int q;
		for (q = 2; q < argc; q++) {
			printf("Scanning %s\n", argv[q]);
			scan_filename(argv[q], 0);
		}
	}

	if (testmode) {
		g_main_loop_unref(loop);
		dbus_bus_remove_match(bus, "type='signal',interface='org.kerneloops.submit.ping'", &error);
		dbus_bus_remove_match(bus, "type='signal',interface='org.kerneloops.submit.permission'", &error);
		free(submit_url);
		return EXIT_SUCCESS;
	}

	/* now, start polling for oopses to occur */

	g_timeout_add_seconds(10, scan_dmesg, NULL);

	g_main_loop_run(loop);
	dbus_bus_remove_match(bus, "type='signal',interface='org.kerneloops.submit.ping'", &error);
	dbus_bus_remove_match(bus, "type='signal',interface='org.kerneloops.submit.permission'", &error);

	g_main_loop_unref(loop);
	free(submit_url);

	return EXIT_SUCCESS;
}
