/*
 *
 * Kerneloops UI applet.
 *
 * Copyright (C) 2007 Intel Corporation
 *
 * Authors:
 * 	Arjan van de Ven <arjan@linux.intel.com>
 *
 *
 * Based on the very useful example from bluez-gnome; many thanks:
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2005-2007  Marcel Holtmann <marcel@holtmann.org>
 *  Copyright (C) 2006-2007  Bastien Nocera <hadess@hadess.net>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <libnotify/notify.h>

static DBusConnection *bus;

static GtkStatusIcon *statusicon = NULL;


static NotifyNotification *notify = NULL;

#define __unused  __attribute__ ((__unused__))


/* 0 = ask
   positive = always
   negative = never 
 */
static int user_preference = 0;


void write_config(char *permission)
{
	FILE *file;
	char filename[2*PATH_MAX];

	sprintf(filename, "%s/.kerneloops",getenv("HOME"));
	file = fopen(filename,"w");
	if (!file) {
		printf("error is %s \n", strerror(errno));
		return;
	}
	fprintf(file,"allow-submit = %s\n", permission);
	fclose(file);
}

/* 
 * send a dbus message to signal the users answer to the permission
 * question.
 */
static void send_permission(char *answer)
{
	DBusMessage *message;
	message = dbus_message_new_signal("/org/kerneloops/submit/permission",
			"org.kerneloops.submit.permission", answer);
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

/*
 * remove an existing notification
 */
static void close_notification(void)
{
	if (notify) {
		g_signal_handlers_destroy(notify);
		notify_notification_close(notify, NULL);
		notify = NULL;
	}
}


/*
 * the notify_action_* functions get called when the user clicks on 
 * the respective buttons we put in the notification window.
 * user_data contains the string we pass, so "yes" "no" "always" or "never".
 */
static void notify_action(NotifyNotification __unused *notify,
					gchar __unused *action, gpointer user_data)
{
	char *answer = (char *) user_data;

	send_permission(answer);
	if (strcmp(answer, "always")==0)
		write_config("alway");
	if (strcmp(answer, "never")==0)
		write_config("never");
	gtk_status_icon_set_visible(statusicon, FALSE);
}

static void got_a_message(void)
{
	char *summary = _("Your system had a kernel failure");
	char *message =
	       _("There is diagnostic information available for this failure."
		" Do you want to submit this information to the <a href=\"http://www.kerneloops.org/\">www.kerneloops.org</a>"
		" website for use by the Linux kernel developers?\n");

	NotifyActionCallback callback = notify_action;

	/* if there's a notification active already, close it first */
	close_notification();

	notify = notify_notification_new(summary, message,
				"/usr/share/kerneloops/icon.png", NULL);

	notify_notification_set_timeout(notify, 0);
	gtk_status_icon_set_visible(statusicon, TRUE);
	notify_notification_set_urgency(notify, NOTIFY_URGENCY_CRITICAL);


	/*
	 * add the buttons and default action
	 */

	notify_notification_add_action(notify, "default", "action",
						callback, "default", NULL);

	notify_notification_add_action(notify, "always", _("Always"),
						callback, "always", NULL);
	notify_notification_add_action(notify, "yes", _("Yes"),
						callback, "yes", NULL);
	notify_notification_add_action(notify, "no", _("No"),
						callback, "no", NULL);
	notify_notification_add_action(notify, "never", _("Never"),
						callback, "never", NULL);

	notify_notification_show(notify, NULL);
}

/*
 * open a notification window (expires in 5 seconds) to say thank you
 * to the user for his bug feedback.
 */
static void sent_an_oops(void)
{
	char *summary = _("Kernel bug diagnostic information sent");
	char *message =
		_("Diagnostic information from your Linux kernel has been "
		  "sent to <a href=\"http://www.kerneloops.org\">www.kerneloops.org</a> "
		  "for the Linux kernel developers to work on. \n"
		  "Thank you for contributing to improve the quality of the Linux kernel.\n");
	NotifyActionCallback callback = notify_action;

	close_notification();

	notify = notify_notification_new(summary, message,
				"/usr/share/kerneloops/icon.png", NULL);

	notify_notification_set_timeout(notify, 5000);
	gtk_status_icon_set_visible(statusicon, TRUE);
	notify_notification_set_urgency(notify, NOTIFY_URGENCY_LOW);


	notify_notification_add_action(notify, "default", "action",
						callback, "default", NULL);

	if (user_preference <= 0)
		notify_notification_add_action(notify, "always", _("Always"),
						callback, "always", NULL);
	notify_notification_add_action(notify, "never", _("Never again"),
						callback, "never", NULL);

	notify_notification_show(notify, NULL);
}



/*
 * When we start up, the daemon may already have collected some oopses
 * so we send it a ping message to let it know someone is listening 
 * now.
 */
static void trigger_daemon(void)
{
	DBusMessage *message;
	message = dbus_message_new_signal("/org/kerneloops/submit/ping",
			"org.kerneloops.submit.ping", "ping");
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}

/*
 * This function gets called if a dbus message arrives that we have
 * subscribed to. 
 */
static DBusHandlerResult dbus_gotmessage(DBusConnection __unused *connection,
		DBusMessage *message,
		void __unused *user_data)
{
	/* handle disconnect events first */
	if (dbus_message_is_signal(message, DBUS_ERROR_DISCONNECTED,
		"Disconnected")) {
		/* FIXME: need to exit the gtk main loop here */
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	/* check if it's the daemon that asks for permission */
	if (dbus_message_is_signal(message, 
			"org.kerneloops.submit.permission", "ask")) {

		if (user_preference > 0) {
			/* the user / config file says "always" */
			send_permission("always");
		} else if (user_preference < 0) {
			/* the user / config file says "never" */
			send_permission("never");
		} else {
			/* ok time to ask the user */
			gtk_status_icon_set_visible(statusicon, TRUE);
			got_a_message();
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	/* check if it's the daemon that asks for permission */
	if (dbus_message_is_signal(message, 
			"org.kerneloops.submit.sent", "sent")) {

		gtk_status_icon_set_visible(statusicon, TRUE);
		sent_an_oops();
		gtk_status_icon_set_visible(statusicon, FALSE);
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/*
 * read the ~/.kerneloops config file to see if the user pressed 
 * "always" or "never" before, and then honor that.
 */
static void read_config(void)
{
	char filename[2*PATH_MAX];
	size_t dummy;
	FILE *file;
	char *line = NULL;
	sprintf(filename, "%s/.kerneloops", getenv("HOME"));
	file = fopen(filename,"r");
	if (!file)
		return;
	if (getline(&line, &dummy, file) <=0) {
		free(line);
		fclose(file);
		return;
	}
	if (strstr(line, "always"))
		user_preference = 1;	
	if (strstr(line, "never"))
		user_preference = -1;	
	if (strstr(line, "ask"))
		user_preference = 0;	
	free(line);
	fclose(file);
}


int main(int argc, char *argv[])
{
	DBusError error;

	/* Initialize translation stuff */
	setlocale (LC_ALL, "");
	bindtextdomain ("kerneloops", "/usr/share/locale");
	textdomain ("kerneloops");


	gtk_init(&argc, &argv);

	/* read the config file early; we may be able to bug out of stuff */
	read_config();

	/* 
	 * initialize the dbus connection; we want to listen to the system
	 * bus (which is where all daemons send their messages
	 */

	dbus_error_init(&error);
	bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (bus == NULL) {
		g_printerr(_("Connecting to system bus failed: %s\n"),
							error.message);
		dbus_error_free(&error);
		exit(EXIT_FAILURE);
	}

	/* hook dbus into the main loop */
	dbus_connection_setup_with_g_main(bus, NULL);

	statusicon = gtk_status_icon_new_from_file("/usr/share/kerneloops/icon.png");

	gtk_status_icon_set_tooltip(statusicon, _("kerneloops client"));

	notify_init("kerneloops-ui");

	/* by default, don't show our icon */
	gtk_status_icon_set_visible(statusicon, FALSE);

	/* set the dbus message to listen for */
	dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.permission'", &error);
	dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.sent'", &error);
	dbus_connection_add_filter(bus, dbus_gotmessage, NULL, NULL);

	/* 
	 * if the user said always/never in the config file, let the daemon
	 * know right away 
	 */
	if (user_preference < 0)
		send_permission("never");
	if (user_preference > 0)
		send_permission("always");

	/* send a ping to the userspace daemon to see if it has pending oopses */
	trigger_daemon();


	gtk_main();

	close_notification();

	return 0;
}
