/*
 * Evolution integration plugin for Gaim
 *
 * Copyright (C) 2003 Christian Hammond.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */
#include "internal.h"
#include "gtkblist.h"
#include "gtkgaim.h"
#include "gtkutils.h"
#include "gtkimhtml.h"
#include "gaim-disclosure.h"

#include "debug.h"

#include "gevolution.h"

#include <stdlib.h>
#include <bonobo/bonobo-main.h>

enum
{
	COLUMN_NAME,
	COLUMN_DATA,
	NUM_COLUMNS
};

static gint
delete_win_cb(GtkWidget *w, GdkEvent *event, GevoAssociateBuddyDialog *dialog)
{
	gtk_widget_destroy(dialog->win);

	g_list_foreach(dialog->contacts, (GFunc)g_free, NULL);

	if (dialog->contacts != NULL)
		g_list_free(dialog->contacts);

	g_object_unref(dialog->book);

	g_free(dialog);

	return 0;
}

static void
populate_address_books(GevoAssociateBuddyDialog *dialog)
{
	GtkWidget *item;
	GtkWidget *menu;
#if notyet
	ESourceList *addressbooks;
	GSList *groups, *g;
#endif

	menu =
		gtk_option_menu_get_menu(GTK_OPTION_MENU(dialog->addressbooks_menu));

	item = gtk_menu_item_new_with_label(_("Local Addressbook"));
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show(item);

#if notyet
	if (!e_book_get_addressbooks(&addressbooks, NULL))
	{
		gaim_debug_error("evolution",
						 "Unable to fetch list of address books.\n");

		item = gtk_menu_item_new_with_label(_("None"));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show(item);

		return;
	}

	groups = e_source_list_peek_groups(addressbooks);

	if (groups == NULL)
	{
		item = gtk_menu_item_new_with_label(_("None"));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		gtk_widget_show(item);

		return;
	}

	for (g = groups; g != NULL; g = g->next)
	{
		GSList *sources, *s;

		sources = e_source_group_peek_sources(g->data);

		for (s = sources; s != NULL; s = s->next)
		{
			ESource *source = E_SOURCE(s->data);

			item = gtk_menu_item_new_with_label(e_source_peek_name(source));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			gtk_widget_show(item);
		}
	}
#endif
}

static void
search_changed_cb(GtkEntry *entry, GevoAssociateBuddyDialog *dialog)
{
	const char *text = gtk_entry_get_text(entry);
	GList *l;

	gtk_list_store_clear(dialog->model);

	for (l = dialog->contacts; l != NULL; l = l->next)
	{
		EContact *contact = E_CONTACT(l->data);
		const char *name;
		GtkTreeIter iter;

		name = e_contact_get_const(contact, E_CONTACT_FULL_NAME);

		if (text != NULL && *text != '\0' && name != NULL &&
			g_ascii_strncasecmp(name, text, strlen(text)))
		{
			continue;
		}

		gtk_list_store_append(dialog->model, &iter);

		gtk_list_store_set(dialog->model, &iter,
						   COLUMN_NAME, name,
						   COLUMN_DATA, contact,
						   -1);
	}
}

static void
clear_cb(GtkWidget *w, GevoAssociateBuddyDialog *dialog)
{
	static gboolean lock = FALSE;

	if (lock)
		return;

	lock = TRUE;
	gtk_entry_set_text(GTK_ENTRY(dialog->search_field), "");
	lock = FALSE;
}

static void
selected_cb(GtkTreeSelection *sel, GevoAssociateBuddyDialog *dialog)
{
	gtk_widget_set_sensitive(dialog->assoc_button, TRUE);
}

static void
add_columns(GevoAssociateBuddyDialog *dialog)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	/* Name column */
	column = gtk_tree_view_column_new();
	gtk_tree_view_column_set_title(column, _("Name"));
	gtk_tree_view_insert_column(GTK_TREE_VIEW(dialog->treeview), column, -1);
	gtk_tree_view_column_set_sort_column_id(column, COLUMN_NAME);

	renderer = gtk_cell_renderer_text_new();
	gtk_tree_view_column_pack_start(column, renderer, TRUE);
	gtk_tree_view_column_add_attribute(column, renderer,
									   "text", COLUMN_NAME);
}

static void
populate_treeview(GevoAssociateBuddyDialog *dialog)
{
	EBookQuery *query;
	EBook *book;
	const char *prpl_id;
	gboolean status;
	GList *cards, *c;

	if (!gevo_load_addressbook(&book, NULL))
	{
		gaim_debug_error("evolution",
						 "Error retrieving default addressbook\n");

		return;
	}

	query = e_book_query_field_exists(E_CONTACT_FULL_NAME);

	if (query == NULL)
	{
		gaim_debug_error("evolution", "Error in creating query\n");

		g_object_unref(book);

		return;
	}

	status = e_book_get_contacts(book, query, &cards, NULL);

	e_book_query_unref(query);

	if (!status)
	{
		gaim_debug_error("evolution", "Error %d in getting card list\n",
						 status);

		g_object_unref(book);

		return;
	}

	prpl_id = gaim_account_get_protocol_id(dialog->buddy->account);

	for (c = cards; c != NULL; c = c->next)
	{
		EContact *contact = E_CONTACT(c->data);
		const char *name;
		GtkTreeIter iter;
		EContactField protocol_field = 0;

		name = e_contact_get_const(contact, E_CONTACT_FULL_NAME);

		gtk_list_store_append(dialog->model, &iter);

		gtk_list_store_set(dialog->model, &iter,
						   COLUMN_NAME, name,
						   COLUMN_DATA, contact,
						   -1);

		/* See if this user has the buddy in its list. */
		protocol_field = gevo_prpl_get_field(dialog->buddy->account,
											 dialog->buddy);

		if (protocol_field > 0)
		{
			GList *ims, *l;

			ims = e_contact_get(contact, protocol_field);

			for (l = ims; l != NULL; l = l->next)
			{
				if (!strcmp(l->data, dialog->buddy->name))
				{
					GtkTreeSelection *selection;

					/* This is it. Select it. */
					selection = gtk_tree_view_get_selection(
						GTK_TREE_VIEW(dialog->treeview));

					gtk_tree_selection_select_iter(selection, &iter);
					break;
				}
			}
		}
	}

	dialog->contacts = cards;
	dialog->book = book;
}

static void
new_person_cb(GtkWidget *w, GevoAssociateBuddyDialog *dialog)
{
	gevo_new_person_dialog_show(NULL, dialog->buddy->account,
								dialog->buddy->name, NULL, dialog->buddy,
								TRUE);

	delete_win_cb(NULL, NULL, dialog);
}

static void
cancel_cb(GtkWidget *w, GevoAssociateBuddyDialog *dialog)
{
	delete_win_cb(NULL, NULL, dialog);
}

static void
assoc_buddy_cb(GtkWidget *w, GevoAssociateBuddyDialog *dialog)
{
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	GList *list;
	const char *fullname;
	EContactField protocol_field;
	EContact *contact;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dialog->treeview));

	gtk_tree_selection_get_selected(selection, NULL, &iter);

	gtk_tree_model_get(GTK_TREE_MODEL(dialog->model), &iter,
					   COLUMN_NAME, &fullname,
					   COLUMN_DATA, &contact,
					   -1);

	protocol_field = gevo_prpl_get_field(dialog->buddy->account, dialog->buddy);

	if (protocol_field == 0)
		return; /* XXX */

	list = e_contact_get(contact, protocol_field);

	list = g_list_append(list, g_strdup(dialog->buddy->name));

	e_contact_set(contact, protocol_field, list);
	if (!e_book_commit_contact(dialog->book, contact, NULL))
	{
		gaim_debug_error("evolution", "Error adding contact to book\n");
	}

	/* Free the list. */
	g_list_foreach(list, (GFunc)g_free, NULL);
	g_list_free(list);

	delete_win_cb(NULL, NULL, dialog);
}

GevoAssociateBuddyDialog *
gevo_associate_buddy_dialog_new(GaimBuddy *buddy)
{
	GevoAssociateBuddyDialog *dialog;
	GtkWidget *button;
	GtkWidget *sw;
	GtkWidget *label;
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *bbox;
	GtkWidget *menu;
	GtkWidget *sep;
	GtkWidget *disclosure;
	GtkTreeSelection *selection;

	g_return_val_if_fail(buddy != NULL, NULL);

	dialog = g_new0(GevoAssociateBuddyDialog, 1);

	dialog->buddy = buddy;

	dialog->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_role(GTK_WINDOW(dialog->win), "assoc_buddy");
	gtk_container_set_border_width(GTK_CONTAINER(dialog->win), 12);

	g_signal_connect(G_OBJECT(dialog->win), "delete_event",
					 G_CALLBACK(delete_win_cb), dialog);

	/* Setup the vbox */
	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(dialog->win), vbox);
	gtk_widget_show(vbox);

	/* Add the label. */
	label = gtk_label_new(_("Select a person from your address book to "
							"add this buddy to, or create a new person."));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);
	gtk_widget_show(label);

	/* Add the search hbox */
	hbox = gtk_hbox_new(FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);
	gtk_widget_show(hbox);

	/* "Search" */
	label = gtk_label_new(_("Search"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
	gtk_widget_show(label);

	/* Addressbooks */
	dialog->addressbooks_menu = gtk_option_menu_new();
	menu = gtk_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(dialog->addressbooks_menu), menu);

	populate_address_books(dialog);

	gtk_option_menu_set_history(GTK_OPTION_MENU(dialog->addressbooks_menu), 0);

	gtk_box_pack_start(GTK_BOX(hbox), dialog->addressbooks_menu,
					   FALSE, FALSE, 0);
	gtk_widget_show(dialog->addressbooks_menu);

	/* Search field */
	dialog->search_field = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), dialog->search_field, TRUE, TRUE, 0);
	gtk_widget_show(dialog->search_field);

	g_signal_connect(G_OBJECT(dialog->search_field), "changed",
					 G_CALLBACK(search_changed_cb), dialog);

	/* Clear button */
	button = gtk_button_new_from_stock(GTK_STOCK_CLEAR);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(clear_cb), dialog);

	/* Scrolled Window */
	sw = gtk_scrolled_window_new(0, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
								   GTK_POLICY_AUTOMATIC,
								   GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
										GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
	gtk_widget_show(sw);

	/* Create the list model for the treeview. */
	dialog->model = gtk_list_store_new(NUM_COLUMNS,
									   G_TYPE_STRING, G_TYPE_POINTER);

	/* Now for the treeview */
	dialog->treeview =
		gtk_tree_view_new_with_model(GTK_TREE_MODEL(dialog->model));
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(dialog->treeview), TRUE);
	gtk_container_add(GTK_CONTAINER(sw), dialog->treeview);
	gtk_widget_show(dialog->treeview);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(dialog->treeview));

	gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

	g_signal_connect(G_OBJECT(selection), "changed",
					 G_CALLBACK(selected_cb), dialog);

	add_columns(dialog);

	populate_treeview(dialog);

	/* Add the disclosure */
	disclosure = gaim_disclosure_new(_("Show user details"),
									 _("Hide user details"));
	gtk_box_pack_start(GTK_BOX(vbox), disclosure, FALSE, FALSE, 0);
	gtk_widget_show(disclosure);

	/*
	 * User details
	 */

	/* Scrolled Window */
	sw = gtk_scrolled_window_new(0, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
								   GTK_POLICY_NEVER,
								   GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
										GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 0);
	gaim_disclosure_set_container(GAIM_DISCLOSURE(disclosure), sw);

	/* Textview */
	dialog->imhtml = gtk_imhtml_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), dialog->imhtml);
	gtk_widget_show(dialog->imhtml);

	/* Separator. */
	sep = gtk_hseparator_new();
	gtk_box_pack_start(GTK_BOX(vbox), sep, FALSE, FALSE, 0);
	gtk_widget_show(sep);

	/* Button box */
	bbox = gtk_hbutton_box_new();
	gtk_box_set_spacing(GTK_BOX(bbox), 6);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
	gtk_box_pack_end(GTK_BOX(vbox), bbox, FALSE, TRUE, 0);
	gtk_widget_show(bbox);

	/* "New Person" button */
	button = gaim_pixbuf_button_from_stock(_("New Person"), GTK_STOCK_NEW,
										   GAIM_BUTTON_HORIZONTAL);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(new_person_cb), dialog);

	/* "Cancel" button */
	button = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(cancel_cb), dialog);

	/* "Associate Buddy" button */
	button = gaim_pixbuf_button_from_stock(_("_Associate Buddy"),
										   GTK_STOCK_APPLY,
										   GAIM_BUTTON_HORIZONTAL);
	dialog->assoc_button = button;
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_widget_set_sensitive(button, FALSE);
	gtk_widget_show(button);

	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(assoc_buddy_cb), dialog);

	/* Show it. */
	gtk_widget_show(dialog->win);

	return dialog;
}
