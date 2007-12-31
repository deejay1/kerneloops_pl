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
int user_preference = 0;


void write_config(char *permission)
{
	FILE *file;
	char filename[2*PATH_MAX];
	printf("Writing config file\n");

	sprintf(filename, "%s/.kerneloops",getenv("HOME"));
	file = fopen(filename,"w");
	if (!file) {
		printf("error is %s \n", strerror(errno));
		return;
	}
	fprintf(file,"allow-submit = %s\n", permission);
	fclose(file);
	printf("Written config file\n");
}

static void send_permission(char *answer)
{
	DBusMessage *message;
	message = dbus_message_new_signal("/org/kerneloops/submit/permission",
			"org.kerneloops.submit.permission", answer);
	dbus_connection_send(bus, message, NULL);
	dbus_message_unref(message);
}


static void notify_action_yes(NotifyNotification __unused *notify,
					gchar __unused *action, gpointer __unused user_data)
{
	gtk_status_icon_set_visible(statusicon, FALSE);
	send_permission("yes");
}
static void notify_action_no(NotifyNotification __unused *notify,
					gchar __unused *action, gpointer __unused user_data)
{
	gtk_status_icon_set_visible(statusicon, FALSE);
	send_permission("no");
}
static void notify_action_always(NotifyNotification __unused *notify,
					gchar __unused *action, gpointer __unused user_data)
{
	send_permission("always");
	gtk_status_icon_set_visible(statusicon, FALSE);
	write_config("always");
}
static void notify_action_never(NotifyNotification __unused *notify,
					gchar __unused *action, gpointer __unused user_data)
{
	gtk_status_icon_set_visible(statusicon, FALSE);
	send_permission("never");
	write_config("never");
}

static void show_notification(const gchar *summary, const gchar *message,
					gint timeout)
{
	NotifyActionCallback callback_yes;
	NotifyActionCallback callback_no;
	NotifyActionCallback callback_always;
	NotifyActionCallback callback_never;

	if (notify) {
		g_signal_handlers_destroy(notify);
		notify_notification_close(notify, NULL);
	}

	notify = notify_notification_new(summary, message,
						"/usr/share/kerneloops/icon.png", NULL);

	notify_notification_set_timeout(notify, timeout);
	gtk_status_icon_set_visible(statusicon, TRUE);
	notify_notification_set_urgency(notify, NOTIFY_URGENCY_CRITICAL);

	callback_yes = notify_action_yes;
	callback_no = notify_action_no;
	callback_always = notify_action_always;
	callback_never = notify_action_never;

	notify_notification_add_action(notify, "default", "action",
						callback_no, NULL, NULL);

	notify_notification_add_action(notify, "always", "Always",
						callback_always, NULL, NULL);
	notify_notification_add_action(notify, "yes", "Yes",
						callback_yes, NULL, NULL);
	notify_notification_add_action(notify, "no", "No",
						callback_no, NULL, NULL);
	notify_notification_add_action(notify, "never", "Never",
						callback_never, NULL, NULL);

	notify_notification_show(notify, NULL);
}

static void close_notification(void)
{
	if (notify) {
		g_signal_handlers_destroy(notify);
		notify_notification_close(notify, NULL);
		notify = NULL;
	}
}


void got_a_message(void)
{
	show_notification(_("Your system had a kernel failure"),
		"There is diagnostic information available for this failure."
		" Do you want to submit this information to the <a href=\"http://www.kerneloops.org/\">www.kerneloops.org</a>"
		" website for use by the Linux kernel developers?", 0);


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
	if (dbus_message_is_signal(message, 
			"org.kerneloops.submit.permission", "ask")) {

		if (user_preference > 0) {
			send_permission("always");
		} else if (user_preference < 0) {
			send_permission("never");
		} else {
			gtk_status_icon_set_visible(statusicon, TRUE);
			got_a_message();
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void read_config(void)
{
	char filename[2*PATH_MAX];
	size_t dummy;
	FILE *file;
	char *line = NULL;
	sprintf(filename, "%s/.kerneloops", getenv("HOME"));
	file = fopen(filename,"r");
	if (!file)
		return;
	if (getline(&line, &dummy, file) <=0)
		return;
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

	gtk_init(&argc, &argv);

	read_config();


	dbus_error_init(&error);
	bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (bus == NULL) {
		g_printerr("Connecting to system bus failed: %s\n",
							error.message);
		dbus_error_free(&error);
		exit(EXIT_FAILURE);
	}

	/* hook dbus into the main loop */
	dbus_connection_setup_with_g_main(bus, NULL);

	statusicon = gtk_status_icon_new_from_file("/usr/share/kerneloops/icon.png");

	gtk_status_icon_set_tooltip(statusicon, _("kerneloops client"));

	notify_init("kerneloops-ui");

	gtk_status_icon_set_visible(statusicon, FALSE);

	/* set the dbus message to listen for */
	dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.permission'", &error);
	dbus_connection_add_filter(bus, dbus_gotmessage, NULL, NULL);

	trigger_daemon();

	if (user_preference < 0)
		send_permission("never");
	if (user_preference > 0)
		send_permission("always");

	gtk_main();

	close_notification();

	return 0;
}
