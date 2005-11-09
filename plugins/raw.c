/*
 * Gaim - Replace certain misspelled words with their correct form.
 *
 * Signification changes were made by Benjamin Kahn ("xkahn") and
 * Richard Laager ("rlaager") in April 2005--you may want to contact
 * them if you have questions.
 *
 * Gaim is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "internal.h"
#include "gtkgaim.h"

#include "conversation.h"
#include "debug.h"
#include "prpl.h"
#include "version.h"

#include "gtkplugin.h"
#include "gtkutils.h"

#ifdef MAX
# undef MAX
# undef MIN
#endif

#define RAW_PLUGIN_ID "gtk-raw"

static GtkWidget *window = NULL;
static GaimAccount *account = NULL;
static GaimPlugin *my_plugin = NULL;

static int
window_closed_cb()
{
	gaim_plugin_unload(my_plugin);

	return FALSE;
}

static void
text_sent_cb(GtkEntry *entry)
{
	const char *txt;
	GaimConnection *gc;
	const char *prpl_id;

	if (account == NULL)
		return;

	gc = gaim_account_get_connection(account);

	txt = gtk_entry_get_text(entry);

	prpl_id = gaim_account_get_protocol_id(account);

	gaim_debug_misc("raw", "prpl_id = %s\n", prpl_id);

	if (strcmp(prpl_id, "prpl-toc") == 0) {
		int *a = (int *)gc->proto_data;
		unsigned short seqno = htons(a[1]++ & 0xffff);
		unsigned short len = htons(strlen(txt) + 1);
		write(*a, "*\002", 2);
		write(*a, &seqno, 2);
		write(*a, &len, 2);
		write(*a, txt, ntohs(len));
		gaim_debug(GAIM_DEBUG_MISC, "raw", "TOC C: %s\n", txt);

	} else if (strcmp(prpl_id, "prpl-irc") == 0) {
		write(*(int *)gc->proto_data, txt, strlen(txt));
		write(*(int *)gc->proto_data, "\r\n", 2);
		gaim_debug(GAIM_DEBUG_MISC, "raw", "IRC C: %s\n", txt);

	} else {
		gaim_debug_error("raw", "Unknown protocol ID %s\n", prpl_id);
	}

	gtk_entry_set_text(entry, "");
}

static void
account_changed_cb(GtkWidget *dropdown, GaimAccount *new_account,
				   void *user_data)
{
	account = new_account;
}

static gboolean
plugin_load(GaimPlugin *plugin)
{
	GtkWidget *hbox;
	GtkWidget *entry;
	GtkWidget *dropdown;

	/* Setup the window. */
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 6);

	g_signal_connect(G_OBJECT(window), "delete_event",
					 G_CALLBACK(window_closed_cb), NULL);

	/* Main hbox */
	hbox = gtk_hbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(window), hbox);

	/* Account drop-down menu. */
	dropdown = gaim_gtk_account_option_menu_new(NULL, FALSE,
			G_CALLBACK(account_changed_cb), NULL, NULL);

	if (gaim_connections_get_all())
		account = (GaimAccount *)gaim_connections_get_all()->data;

	gtk_box_pack_start(GTK_BOX(hbox), dropdown, FALSE, FALSE, 0);

	/* Entry box */
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);

	g_signal_connect(G_OBJECT(entry), "activate",
					 G_CALLBACK(text_sent_cb), NULL);

	gtk_widget_show_all(window);

	return TRUE;
}

static gboolean
plugin_unload(GaimPlugin *plugin)
{
	if (window)
		gtk_widget_destroy(window);

	window = NULL;

	return TRUE;
}

static GaimPluginInfo info =
{
	GAIM_PLUGIN_MAGIC,
	GAIM_MAJOR_VERSION,
	GAIM_MINOR_VERSION,
	GAIM_PLUGIN_STANDARD,
	GAIM_GTK_PLUGIN_TYPE,
	0,
	NULL,
	GAIM_PRIORITY_DEFAULT,
	RAW_PLUGIN_ID,
	N_("Raw"),
	VERSION,
	N_("Lets you send raw input to text-based protocols."),
	N_("Lets you send raw input to text-based protocols (IRC, "
	   "TOC). Hit 'Enter' in the entry box to send. Watch the debug window."),
	"Eric Warmenhoven <eric@warmenhoven.org>",
	GAIM_WEBSITE,
	plugin_load,
	plugin_unload,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

static void
init_plugin(GaimPlugin *plugin)
{
	my_plugin = plugin;
}

GAIM_INIT_PLUGIN(raw, init_plugin, info)
