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
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <dbus/dbus-glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib/gi18n.h>

#include <gtk/gtk.h>

#include <libnotify/notify.h>

static DBusConnection *bus;

static GtkStatusIcon *statusicon;


static NotifyNotification *notify;

#define __unused  __attribute__ ((__unused__))


/* 0 = ask
   positive = always
   negative = never
 */


int user_preference;
static char *detail_file_name;

static void write_config(char *permission)
{
	FILE *file;
	char filename[2*PATH_MAX];

	sprintf(filename, "%s/.kerneloops", getenv("HOME"));
	file = fopen(filename, "w");
	if (!file) {
		printf("error is %s \n", strerror(errno));
		return;
	}
	fprintf(file, "allow-submit = %s\n", permission);
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
	detail_file_name = NULL;
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
	detail_file_name = NULL;
	if (strcmp(answer, "always") == 0)
		write_config("always");
	if (strcmp(answer, "never") == 0)
		write_config("never");
	gtk_status_icon_set_visible(statusicon, FALSE);
}

/* Called only from the detail window */
static void send_action(NotifyNotification __unused *notify,
			gchar __unused *action, gpointer __unused user_data)
{
	send_permission("yes");
}


/* Called only to display details */
static void detail_action(NotifyNotification __unused *notify,
			  gchar __unused *action, gpointer __unused user_data)
{
	GtkWidget *dialog;
	GtkWidget *scrollwindow;
	GtkWidget *view;
	GtkTextBuffer *buffer;
	GtkWidget *button_cancel;
	GtkWidget *button_send;
	char *detail_data;
	struct stat statb;
	int detail_fd;
	int ret;

	/* If anything goes wrong, return as early as possible... */

	if (!detail_file_name)
		return;

        memset(&statb, 0, sizeof(statb));
	ret = stat(detail_file_name, &statb);
	if (statb.st_size < 1 || ret != 0)
		return;

	detail_fd = open(detail_file_name, O_RDONLY);
	if (detail_fd < 0)
		return;

	detail_data = malloc(statb.st_size+1);
	if (!detail_data)
		return;
	
	if (read(detail_fd, detail_data, statb.st_size) != statb.st_size) {
		free(detail_data);
		return;
	}
	close(detail_fd);

	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), _("Kernel failure details"));
	gtk_widget_set_size_request(dialog, 600, 400);
	scrollwindow = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrollwindow),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), scrollwindow, 
			   TRUE, TRUE, 0);
	view = gtk_text_view_new();
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW (view));
	gtk_text_buffer_set_text(buffer, detail_data, -1);
	free(detail_data);
	gtk_scrolled_window_add_with_viewport(
		GTK_SCROLLED_WINDOW(scrollwindow), view);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
	button_send = gtk_button_new_with_label (_("Send"));
	GTK_WIDGET_SET_FLAGS(button_send, GTK_CAN_DEFAULT);
	gtk_widget_grab_default(button_send);
	button_cancel = gtk_button_new_with_label (_("Cancel"));

	g_signal_connect(G_OBJECT(dialog), "delete_event",
			 G_CALLBACK(gtk_widget_destroy), dialog);
	g_signal_connect_swapped(G_OBJECT(button_cancel), "clicked",
		         G_CALLBACK(gtk_widget_destroy),
			 G_OBJECT(dialog));
	g_signal_connect(G_OBJECT(dialog), "destroy",
			 G_CALLBACK(gtk_widget_destroy),
			 G_OBJECT(dialog));
	g_signal_connect(G_OBJECT(button_send), "clicked",
			 G_CALLBACK(send_action), NULL);
  
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
		button_send, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->action_area),
		button_cancel, TRUE, TRUE, 0);

	gtk_widget_show(view);
	gtk_widget_show(button_send);
	gtk_widget_show(button_cancel);
	gtk_widget_show(scrollwindow);
	gtk_widget_show(dialog);
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
	if (detail_file_name) {
		notify_notification_add_action(notify,
			"details", _("Show Details"),
			detail_action, "details", NULL);
	}

	notify_notification_show(notify, NULL);
}

char url_to_oops[4095];

/*
 * open a notification window (expires in 5 seconds) to say thank you
 * to the user for his bug feedback.
 */
static void sent_an_oops(void)
{
	char *summary = _("Kernel bug diagnostic information sent");
	char message[8200];
	char *message_1 =
		_("Diagnostic information from your Linux kernel has been "
		  "sent to <a href=\"http://www.kerneloops.org\">www.kerneloops.org</a> "
		  "for the Linux kernel developers to work on. \n"
		  "Thank you for contributing to improve the quality of the Linux kernel.\n");

	char *message_2 =
		_("Diagnostic information from your Linux kernel has been "
		  "sent to <a href=\"http://www.kerneloops.org\">www.kerneloops.org</a> "
		  "for the Linux kernel developers to work on. \n"
		  "Thank you for contributing to improve the quality of the Linux kernel.\n"
		"You can watch your submitted oops <a href=\"%s\">here</a>\n");
	NotifyActionCallback callback = notify_action;

	close_notification();


	if (strlen(url_to_oops)==0)
		sprintf(message, message_1);
	else
		sprintf(message, message_2, url_to_oops);


	url_to_oops[0] = 0;

	notify = notify_notification_new(summary, message,
				"/usr/share/kerneloops/icon.png", NULL);

	notify_notification_set_timeout(notify, 5000);
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
 * store the URL for the user
 */
static void got_an_url(DBusMessage *message)
{
	char *string = NULL;
	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &string, DBUS_TYPE_INVALID);
	if (string)
		strncpy(url_to_oops, string, 4095);

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
			dbus_message_get_args(message, NULL,
			        DBUS_TYPE_STRING, &detail_file_name,
			        DBUS_TYPE_INVALID);
			got_a_message();
			gtk_status_icon_set_visible(statusicon, FALSE);
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
	/* check if it's the daemon that asks for permission */
	if (dbus_message_is_signal(message,
			"org.kerneloops.submit.url", "url")) {

		got_an_url(message);
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
	file = fopen(filename, "r");
	if (!file)
		return;
	if (getline(&line, &dummy, file) <= 0) {
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
	setlocale(LC_ALL, "");
	bindtextdomain("kerneloops", "/usr/share/locale");
	textdomain("kerneloops");


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
	dbus_bus_add_match(bus, "type='signal',interface='org.kerneloops.submit.url'", &error);
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
