/*
 * gaim
 *
 * Copyright (C) 1998-1999, Mark Spencer <markster@marko.net>
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
 * ----------------
 * The Plug-in plug
 *
 * Plugin support is currently being maintained by Mike Saraf
 * msaraf@dwc.edu
 *
 * Well, I didn't see any work done on it for a while, so I'm going to try
 * my hand at it. - Eric warmenhoven@yahoo.com
 *
 */

#ifdef HAVE_CONFIG_H
#include "../config.h"
#endif

#include <string.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include "gaim.h"

#ifdef GAIM_PLUGINS

#include <dlfcn.h>

/* ------------------ Global Variables ----------------------- */

GList *plugins   = NULL;
GList *callbacks = NULL;

/* ------------------ Local Variables ------------------------ */

static GtkWidget *plugin_dialog = NULL;

static GtkWidget *pluglist;
static GtkWidget *plugtext;
static GtkWidget *plugwindow;

static GtkWidget *config = NULL;
static guint confighandle = 0;

/* --------------- Function Declarations --------------------- */

       void show_plugins (GtkWidget *, gpointer);
       void load_plugin  (char *);

       void gaim_signal_connect   (void *, enum gaim_event, void *, void *);
       void gaim_signal_disconnect(void *, enum gaim_event, void *);
       void gaim_plugin_unload    (void *);

static void destroy_plugins  (GtkWidget *, gpointer);
static void load_file        (GtkWidget *, gpointer);
static void load_which_plugin(GtkWidget *, gpointer);
static void unload           (GtkWidget *, gpointer);
static void list_clicked     (GtkWidget *, struct gaim_plugin *);
static void update_show_plugins();
static void hide_plugins     (GtkWidget *, gpointer);

/* ------------------ Code Below ---------------------------- */

static void destroy_plugins(GtkWidget *w, gpointer data) {
	if (plugin_dialog)
		gtk_widget_destroy(plugin_dialog);
	plugin_dialog = NULL;
}

static void load_file(GtkWidget *w, gpointer data)
{
	gchar *buf;
	FILE *fd;
 
	if (plugin_dialog) {
		gtk_widget_show(plugin_dialog);
		gdk_window_raise(plugin_dialog->window);
		return;
	}

	plugin_dialog = gtk_file_selection_new(_("Gaim - Plugin List"));

	gtk_file_selection_hide_fileop_buttons(
					GTK_FILE_SELECTION(plugin_dialog));

	buf = g_strconcat(g_get_home_dir(), G_DIR_SEPARATOR_S, PLUGIN_DIR, NULL);
	fd = fopen(buf, "r");
	if (!fd)
		mkdir(buf, S_IRUSR | S_IWUSR | S_IXUSR);
	else
		fclose(fd);

	gtk_file_selection_set_filename(GTK_FILE_SELECTION(plugin_dialog), buf);
	gtk_file_selection_complete(GTK_FILE_SELECTION(plugin_dialog), "*.so");
	gtk_signal_connect(GTK_OBJECT(plugin_dialog), "destroy",
			GTK_SIGNAL_FUNC(destroy_plugins), plugin_dialog);

	gtk_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(plugin_dialog)->ok_button),
			"clicked", GTK_SIGNAL_FUNC(load_which_plugin), NULL);
    
	gtk_signal_connect(GTK_OBJECT(GTK_FILE_SELECTION(plugin_dialog)->cancel_button),
			"clicked", GTK_SIGNAL_FUNC(destroy_plugins), NULL);

	g_free(buf);
	gtk_widget_show(plugin_dialog);
	gdk_window_raise(plugin_dialog->window);   
}

static void load_which_plugin(GtkWidget *w, gpointer data) {
	load_plugin(gtk_file_selection_get_filename(
					GTK_FILE_SELECTION(plugin_dialog)));

	if (plugin_dialog)
		gtk_widget_destroy(plugin_dialog);
	plugin_dialog = NULL;
}

void load_plugin(char *filename) {
	struct gaim_plugin *plug;
	GList *c = plugins;
	int (*gaim_plugin_init)();
	char *(*gaim_plugin_error)(int);
	char *(*cfunc)();
	char *error;
	int retval;
	char *plugin_error;

	if (filename == NULL) return;
	/* i shouldn't be checking based solely on path, but i'm lazy */
	while (c) {
		plug = (struct gaim_plugin *)c->data;
		if (!strcmp(filename, plug->filename)) {
			sprintf(debug_buff, _("Already loaded %s, "
						"not reloading.\n"), filename);
			debug_print(debug_buff);
			return;
		}
		c = g_list_next(c);
	}
	plug = g_malloc(sizeof *plug);
	if (filename[0] != '/')
		plug->filename = g_strconcat(g_get_home_dir(), G_DIR_SEPARATOR_S,
			PLUGIN_DIR, filename, NULL);
	else
		plug->filename = g_strdup(filename);

	sprintf(debug_buff, "Loading %s\n", filename);
	debug_print(debug_buff);
	/* do NOT `OR' with RTLD_GLOBAL, otherwise plugins may conflict
	 * (it's really just a way to work around other people's bad
	 * programming, by not using RTLD_GLOBAL :P ) */
	plug->handle = dlopen(plug->filename, RTLD_LAZY);
	if (!plug->handle) {
		error = (char *)dlerror();
		do_error_dialog(error, _("Plugin Error"));
		g_free(plug->filename);
		g_free(plug);
		return;
	}

	gaim_plugin_init = dlsym(plug->handle, "gaim_plugin_init");
	if ((error = (char *)dlerror()) != NULL) {
		do_error_dialog(error, _("Plugin Error"));
		dlclose(plug->handle);
		g_free(plug->filename);
		g_free(plug);
		return;
	}

	retval = (*gaim_plugin_init)(plug->handle);
	sprintf(debug_buff, "loaded plugin returned %d\n", retval);
	debug_print(debug_buff);
	if (retval < 0) {
		GList *c = callbacks;
		struct gaim_callback *g;
		while (c) {
			g = (struct gaim_callback *)c->data;
			if (g->handle == plug->handle) {
				callbacks = g_list_remove(callbacks, c->data);
				sprintf(debug_buff, "Removing callback, %d remain\n",
						g_list_length(callbacks));
				debug_print(debug_buff);
				c = callbacks;
				if (c == NULL) {
					break;
				}
			} else {
				c = g_list_next(c);
			}
		}
		gaim_plugin_error = dlsym(plug->handle, "gaim_plugin_error");
		if ((error = (char *)dlerror()) == NULL) {
			plugin_error = (*gaim_plugin_error)(retval);
			if (plugin_error)
				do_error_dialog(plugin_error, _("Plugin Error"));
		}
		dlclose(plug->handle);
		g_free(plug->filename);
		g_free(plug);
		return;
	}

	plugins = g_list_append(plugins, plug);

	cfunc = dlsym(plug->handle, "name");
	if ((error = (char *)dlerror()) == NULL)
		plug->name = (*cfunc)();
	else
		plug->name = NULL;

	cfunc = dlsym(plug->handle, "description");
	if ((error = (char *)dlerror()) == NULL)
		plug->description = (*cfunc)();
	else
		plug->description = NULL;

	update_show_plugins();
	save_prefs();
}

void show_plugins(GtkWidget *w, gpointer data) {
	/* most of this code was shamelessly stolen from prefs.c */
	GtkWidget *page;
	GtkWidget *topbox;
	GtkWidget *botbox;
	GtkWidget *sw;
	GtkWidget *label;
	GtkWidget *list_item;
	GtkWidget *sw2;
	GtkWidget *add;
	GtkWidget *remove;
	GtkWidget *close;
	GList     *plugs = plugins;
	struct gaim_plugin *p;

	if (plugwindow) return;

	plugwindow = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_wmclass(GTK_WINDOW(plugwindow), "plugins", "Gaim");
	gtk_widget_realize(plugwindow);
	aol_icon(plugwindow->window);
	gtk_container_border_width(GTK_CONTAINER(plugwindow), 10);
	gtk_window_set_title(GTK_WINDOW(plugwindow), _("Gaim - Plugins"));
	gtk_widget_set_usize(plugwindow, 400, 250);
	gtk_signal_connect(GTK_OBJECT(plugwindow), "destroy",
			   GTK_SIGNAL_FUNC(hide_plugins), NULL);

	page = gtk_vbox_new(FALSE, 0);
	topbox = gtk_hbox_new(FALSE, 0);
	botbox = gtk_hbox_new(FALSE, 0);

	/* Left side: list of plugin file names */
	sw2 = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw2),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	pluglist = gtk_list_new();
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sw2), pluglist);
	gtk_box_pack_start(GTK_BOX(topbox), sw2, TRUE, TRUE, 0);

	/* Right side: the text description of the plugin */
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	plugtext = gtk_text_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), plugtext);
	gtk_box_pack_start(GTK_BOX(topbox), sw, TRUE, TRUE, 0);
	gtk_text_set_word_wrap(GTK_TEXT(plugtext), TRUE);
	gtk_text_set_editable(GTK_TEXT(plugtext), FALSE);

	/* Build the bottom button bar */
	add = gtk_button_new_with_label(_("Load Plugin"));
	gtk_signal_connect(GTK_OBJECT(add), "clicked",
			   GTK_SIGNAL_FUNC(load_file), NULL);
	gtk_box_pack_start(GTK_BOX(botbox), add, TRUE, FALSE, 5);
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(add), GTK_RELIEF_NONE);

	config = gtk_button_new_with_label(_("Configure Plugin"));
	gtk_widget_set_sensitive(config, 0);
	gtk_box_pack_start(GTK_BOX(botbox), config, TRUE, FALSE, 5);
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(config), GTK_RELIEF_NONE);

	remove = gtk_button_new_with_label(_("Unload Plugin"));
	gtk_signal_connect(GTK_OBJECT(remove), "clicked",
			   GTK_SIGNAL_FUNC(unload), pluglist);
	gtk_box_pack_start(GTK_BOX(botbox), remove, TRUE, FALSE, 5);
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(remove), GTK_RELIEF_NONE);

	close = gtk_button_new_with_label(_("Close"));
	gtk_signal_connect(GTK_OBJECT(close), "clicked",
			   GTK_SIGNAL_FUNC(hide_plugins), NULL);
	gtk_box_pack_start(GTK_BOX(botbox), close, TRUE, FALSE, 5);
	if (display_options & OPT_DISP_COOL_LOOK)
		gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);

	gtk_box_pack_start(GTK_BOX(page), topbox, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(page), botbox, FALSE, FALSE, 0);

	while (plugs) {
		p = (struct gaim_plugin *)plugs->data;
		label = gtk_label_new(p->filename);
		list_item = gtk_list_item_new();
		gtk_container_add(GTK_CONTAINER(list_item), label);
		gtk_signal_connect(GTK_OBJECT(list_item), "select",
				   GTK_SIGNAL_FUNC(list_clicked), p);
		gtk_object_set_user_data(GTK_OBJECT(list_item), p);

		gtk_widget_show(label);
		gtk_container_add(GTK_CONTAINER(pluglist), list_item);
		gtk_widget_show(list_item);

		plugs = g_list_next(plugs);
	}
	
	/* Make the first item selected */
	if (plugins != NULL)
		gtk_list_select_item(GTK_LIST(pluglist), 0);

	gtk_widget_show(page);
	gtk_widget_show(topbox);
	gtk_widget_show(botbox);
	gtk_widget_show(sw);
	gtk_widget_show(sw2);
	gtk_widget_show(pluglist);
	gtk_widget_show(plugtext);
	gtk_widget_show(add);
	gtk_widget_show(config);
	gtk_widget_show(remove);
	gtk_widget_show(close);

	gtk_container_add(GTK_CONTAINER(plugwindow), page);
	gtk_widget_show(plugwindow);
}

void update_show_plugins() {
	GList *plugs = plugins;
	struct gaim_plugin *p;
	GtkWidget *label;
	GtkWidget *list_item;

	if (plugwindow == NULL) return;

	gtk_list_clear_items(GTK_LIST(pluglist), 0, -1);
	while (plugs) {
		p = (struct gaim_plugin *)plugs->data;
		label = gtk_label_new(p->filename);
		list_item = gtk_list_item_new();
		gtk_container_add(GTK_CONTAINER(list_item), label);
		gtk_signal_connect(GTK_OBJECT(list_item), "select",
				   GTK_SIGNAL_FUNC(list_clicked), p);
		gtk_object_set_user_data(GTK_OBJECT(list_item), p);

		gtk_widget_show(label);
		gtk_container_add(GTK_CONTAINER(pluglist), list_item);
		gtk_widget_show(list_item);
		plugs = g_list_next(plugs);
	}
	if (plugins != NULL)
		gtk_list_select_item(GTK_LIST(pluglist), 0);
	else {
		gtk_text_set_point(GTK_TEXT(plugtext), 0);
		gtk_text_forward_delete(GTK_TEXT(plugtext),
			gtk_text_get_length(GTK_TEXT(plugtext)));
	}
}

void unload(GtkWidget *w, gpointer data) {
	GList *i;
	struct gaim_plugin *p;
	void (*gaim_plugin_remove)();
	char *error;

	i = GTK_LIST(pluglist)->selection;

	if (i == NULL) return;

	p = gtk_object_get_user_data(GTK_OBJECT(i->data));

	/* Attempt to call the plugin's remove function (if there) */
	gaim_plugin_remove = dlsym(p->handle, "gaim_plugin_remove");
	if ((error = (char *)dlerror()) == NULL)
		(*gaim_plugin_remove)();

	gaim_plugin_unload(p->handle);
}

static void remove_callback(struct gaim_plugin *p) {
	gtk_timeout_remove(p->remove);
	dlclose(p->handle);
	g_free(p);
}

/* gaim_plugin_unload serves 2 purposes: 1. so plugins can unload themselves
 * 					 2. to make my life easier */
void gaim_plugin_unload(void *handle) {
	GList *i;
	struct gaim_plugin *p = NULL;
	GList *c = callbacks;
	struct gaim_callback *g;

	i = plugins;
	while (i) {
		p = (struct gaim_plugin *)i->data;
		if (handle == p->handle)
			break;
		p = NULL;
		i = g_list_next(i);
	}

	if (!p)
		return;

	sprintf(debug_buff, "Unloading %s\n", p->filename);
	debug_print(debug_buff);

	sprintf(debug_buff, "%d callbacks to search\n", g_list_length(callbacks));
	debug_print(debug_buff);

	while (c) {
		g = (struct gaim_callback *)c->data;
		if (g->handle == p->handle) {
			callbacks = g_list_remove(callbacks, c->data);
			g_free(g);
			sprintf(debug_buff, "Removing callback, %d remain\n",
					g_list_length(callbacks));
			debug_print(debug_buff);
			c = callbacks;
			if (c == NULL) {
				break;
			}
		} else {
			c = g_list_next(c);
		}
	}
	/* remove callbacks later (this will g_free p) */
	p->remove = gtk_timeout_add(5000, (GtkFunction)remove_callback, p);

	plugins = g_list_remove(plugins, p);
	g_free(p->filename);
	if (config) gtk_widget_set_sensitive(config, 0);
	update_show_plugins();
	save_prefs();
}

void list_clicked(GtkWidget *w, struct gaim_plugin *p) {
	gchar *temp;
	guint text_len;
	void (*gaim_plugin_config)();
	char *error;

	if (confighandle != 0)
		gtk_signal_disconnect(GTK_OBJECT(config), confighandle);
	text_len = gtk_text_get_length(GTK_TEXT(plugtext));
	gtk_text_set_point(GTK_TEXT(plugtext), 0);
	gtk_text_forward_delete(GTK_TEXT(plugtext), text_len);

	temp = g_strdup_printf("%s\n%s", p->name, p->description);
	gtk_text_insert(GTK_TEXT(plugtext), NULL, NULL, NULL, temp, -1);
	g_free(temp);

	/* Find out if this plug-in has a configuration function */
	gaim_plugin_config = dlsym(p->handle, "gaim_plugin_config");
	if ((error = (char *)dlerror()) == NULL) {
		confighandle = gtk_signal_connect(GTK_OBJECT(config), "clicked",
				   GTK_SIGNAL_FUNC(gaim_plugin_config), NULL);
		gtk_widget_set_sensitive(config, 1);
	} else {
		confighandle = 0;
		gtk_widget_set_sensitive(config, 0);
	}
}

void hide_plugins(GtkWidget *w, gpointer data) {
	if (plugwindow)
		gtk_widget_destroy(plugwindow);
	plugwindow = NULL;
	config = NULL;
	confighandle = 0;
}

void gaim_signal_connect(void *handle, enum gaim_event which,
			 void *func, void *data) {
	struct gaim_callback *call = g_malloc(sizeof *call);
	call->handle = handle;
	call->event = which;
	call->function = func;
	call->data = data;

	callbacks = g_list_append(callbacks, call);
	sprintf(debug_buff, "Adding callback %d\n", g_list_length(callbacks));
	debug_print(debug_buff);
}

void gaim_signal_disconnect(void *handle, enum gaim_event which, void *func) {
	GList *c = callbacks;
	struct gaim_callback *g = NULL;

	while (c) {
		g = (struct gaim_callback *)c->data;
		if (handle == g->handle && func == g->function) {
			callbacks = g_list_remove(callbacks, c->data);
			g_free(g);
			c = callbacks;
			if (c == NULL) break;
		}
		c = g_list_next(c);
	}
}

#endif /* GAIM_PLUGINS */

char *event_name(enum gaim_event event)
{
	static char buf[128];
	switch(event) {
		case event_signon:
			sprintf(buf, "event_signon");
			break;
		case event_signoff:
			sprintf(buf, "event_signoff");
			break;
		case event_away:
			sprintf(buf, "event_away");
			break;
		case event_back:
			sprintf(buf, "event_back");
			break;
		case event_im_recv:
			sprintf(buf, "event_im_recv");
			break;
		case event_im_send:
			sprintf(buf, "event_im_send");
			break;
		case event_buddy_signon:
			sprintf(buf, "event_buddy_signon");
			break;
		case event_buddy_signoff:
			sprintf(buf, "event_buddy_signoff");
			break;
		case event_buddy_away:
			sprintf(buf, "event_buddy_away");
			break;
		case event_buddy_back:
			sprintf(buf, "event_buddy_back");
			break;
		case event_blist_update:
			sprintf(buf, "event_blist_update");
			break;
		case event_chat_invited:
			sprintf(buf, "event_chat_invited");
			break;
		case event_chat_join:
			sprintf(buf, "event_chat_join");
			break;
		case event_chat_leave:
			sprintf(buf, "event_chat_leave");
			break;
		case event_chat_buddy_join:
			sprintf(buf, "event_chat_buddy_join");
			break;
		case event_chat_buddy_leave:
			sprintf(buf, "event_chat_buddy_leave");
			break;
		case event_chat_recv:
			sprintf(buf, "event_chat_recv");
			break;
		case event_chat_send:
			sprintf(buf, "event_chat_send");
			break;
		case event_warned:
			sprintf(buf, "event_warned");
			break;
		case event_error:
			sprintf(buf, "event_error");
			break;
		case event_quit:
			sprintf(buf, "event_quit");
			break;
		case event_new_conversation:
			sprintf(buf, "event_new_conversaion");
			break;
		default:
			sprintf(buf, "event_unknown");
			break;
	}
	return buf;
}

void plugin_event(enum gaim_event event, void *arg1, void *arg2, void *arg3) {
#ifdef USE_PERL
	char buf[BUF_LONG];
	char *tmp;
#endif
#ifdef GAIM_PLUGINS
	GList *c = callbacks;
	struct gaim_callback *g;

	while (c) {
		g = (struct gaim_callback *)c->data;
		if (g->event == event && g->function != NULL) {
			switch(event) {

			/* no args */
			case event_signon:
			case event_signoff:
			case event_away:
			case event_back:
			case event_blist_update:
			case event_quit:
				{
					void (*function)(void *) = g->function;
					(*function)(g->data);
				}
				break;

			/* char **, char ** */
			case event_im_recv:
				{
					void (*function)(char **, char **, void *) = g->function;
					(*function)(arg1, arg2, g->data);
				}
				break;

			/* char *, char ** */
			case event_im_send:
			case event_chat_send:
				{
					void (*function)(char *, char **, void *) = g->function;
					(*function)(arg1, arg2, g->data);
				}
				break;

			/* char * */
			case event_buddy_signon:
			case event_buddy_signoff:
			case event_buddy_away:
			case event_buddy_back:
			case event_chat_join:
			case event_chat_leave:
			case event_new_conversation:
				{
					void (*function)(char *, void *) = g->function;
					(*function)(arg1, g->data);
				}
				break;

			/* char *, char *, char * */
			case event_chat_invited:
			case event_chat_recv:
				{
					void (*function)(char *, char *, char *, void *) = g->function;
					(*function)(arg1, arg2, arg3, g->data);
				}
				break;

			/* char *, char * */
			case event_chat_buddy_join:
			case event_chat_buddy_leave:
				{
					void (*function)(char *, char *, void *) = g->function;
					(*function)(arg1, arg2, g->data);
				}
				break;

			/* char *, int */
			case event_warned:
				{
					void (*function)(char *, int, void *) = g->function;
					(*function)(arg1, (int)arg2, g->data);
				}
				break;

			/* int */
			case event_error:
				{
					void (*function)(int, void *) = g->function;
					(*function)((int)arg1, g->data);
				}
				break;

			default:
				sprintf(debug_buff, "unknown event %d\n", event);
				debug_print(debug_buff);
				break;
			}
		}
		c = c->next;
	}
#endif /* GAIM_PLUGINS */
#ifdef USE_PERL
	switch (event) {
		case event_signon:
			buf[0] = 0;
			break;
		case event_signoff:
			buf[0] = 0;
			break;
		case event_away:
			buf[0] = 0;
			break;
		case event_back:
			buf[0] = 0;
			break;
		case event_im_recv:
			g_snprintf(buf, sizeof buf, "\"%s\" %s", *(char **)arg1, *(char **)arg2);
			break;
		case event_im_send:
			g_snprintf(buf, sizeof buf, "\"%s\" %s", (char *)arg1, *(char **)arg2);
			break;
		case event_buddy_signon:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_buddy_signoff:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_buddy_away:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_buddy_back:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_blist_update:
			buf[0] = 0;
			break;
		case event_chat_invited:
			g_snprintf(buf, sizeof buf, "\"%s\" \"%s\" %s", (char *)arg1, (char *)arg2, (char *)arg3);
			break;
		case event_chat_join:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_chat_leave:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		case event_chat_buddy_join:
			g_snprintf(buf, sizeof buf, "\"%s\" \"%s\"", (char *)arg1, (char *)arg2);
			break;
		case event_chat_buddy_leave:
			g_snprintf(buf, sizeof buf, "\"%s\" \"%s\"", (char *)arg1, (char *)arg2);
			break;
		case event_chat_recv:
			g_snprintf(buf, sizeof buf, "\"%s\" \"%s\" %s", (char *)arg1, (char *)arg2, (char *)arg3);
			break;
		case event_chat_send:
			g_snprintf(buf, sizeof buf, "\"%s\" %s", (char *)arg1, *(char **)arg2);
			break;
		case event_warned:
			g_snprintf(buf, sizeof buf, "\"%s\" %d", (char *)arg1, (int)arg2);
			break;
		case event_error:
			g_snprintf(buf, sizeof buf, "%d", (int)arg1);
			break;
		case event_quit:
			buf[0] = 0;
			break;
		case event_new_conversation:
			g_snprintf(buf, sizeof buf, "\"%s\"", (char *)arg1);
			break;
		default:
			break;
	}
	tmp = event_name(event);
	g_snprintf(debug_buff, sizeof debug_buff, "%s: %s\n", tmp, buf);
	debug_print(debug_buff);
	perl_event(tmp, buf);
#endif
}
