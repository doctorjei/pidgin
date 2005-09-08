/**
 * @file gtkconv.c GTK+ Conversation API
 * @ingroup gtkui
 *
 * gaim
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

#ifndef _WIN32
# include <X11/Xlib.h>
#endif

#ifdef USE_GTKSPELL
# include <gtkspell/gtkspell.h>
# ifdef _WIN32
#  include "wspell.h"
# endif
#endif

#include <gdk/gdkkeysyms.h>

#include "account.h"
#include "cmds.h"
#include "debug.h"
#include "imgstore.h"
#include "log.h"
#include "notify.h"
#include "prpl.h"
#include "request.h"
#include "sound.h"
#include "util.h"

#include "gtkdnd-hints.h"
#include "gtkblist.h"
#include "gtkconv.h"
#include "gtkdialogs.h"
#include "gtkimhtml.h"
#include "gtkimhtmltoolbar.h"
#include "gtklog.h"
#include "gtkpounce.h"
#include "gtkprefs.h"
#include "gtkprivacy.h"
#include "gtkutils.h"
#include "gtkstock.h"

#define AUTO_RESPONSE "&lt;AUTO-REPLY&gt; : "

#define SEND_COLOR "#16569E"
#define RECV_COLOR "#A82F2F"

#define LUMINANCE(c) (float)((0.3*(c.red))+(0.59*(c.green))+(0.11*(c.blue)))

/* These colors come from the default GNOME palette */
static GdkColor nick_colors[] = {
	{0, 47616, 46336, 43776},       /* Basic 3D Medium */
	{0, 32768, 32000, 29696},       /* Basic 3D Dark */
	{0, 22016, 20992, 18432},       /* 3D Shadow */
	{0, 33536, 42496, 32512},       /* Green Medium */
	{0, 23808, 29952, 21760},       /* Green Dark */
	{0, 17408, 22016, 12800},       /* Green Shadow */
	{0, 57344, 46592, 44800},       /* Red Hilight */
	{0, 49408, 26112, 23040},       /* Red Medium */
	{0, 34816, 17920, 12544},       /* Red Dark */
	{0, 49408, 14336,  8704},       /* Red Shadow */
	{0, 34816, 32512, 41728},       /* Purple Medium */
	{0, 25088, 23296, 33024},       /* Purple Dark */
	{0, 18688, 16384, 26112},       /* Purple Shadow */
	{0, 40192, 47104, 53760},       /* Blue Hilight */
	{0, 29952, 36864, 44544},       /* Blue Medium */
	{0, 57344, 49920, 40448},       /* Face Skin Medium */
	{0, 45824, 37120, 26880},       /* Face skin Dark */
	{0, 33280, 26112, 18176},       /* Face Skin Shadow */
	{0, 57088, 16896,  7680},       /* Accent Red */
	{0, 39168,     0,     0},       /* Accent Red Dark */
	{0, 60928, 54784, 32768},       /* Accent Yellow */
	{0, 17920, 40960, 17920},       /* Accent Green */
	{0,  9728, 50944,  9728}        /* Accent Green Dark */
};

#define NUM_NICK_COLORS (sizeof(nick_colors) / sizeof(*nick_colors))

typedef struct
{
	GtkWidget *window;

	GtkWidget *entry;
	GtkWidget *message;

	GaimConversation *conv;

} InviteBuddyInfo;

static GtkWidget *invite_dialog = NULL;
static GtkWidget *warn_close_dialog = NULL;

/* Prototypes. <-- because Paco-Paco hates this comment. */
static void got_typing_keypress(GaimGtkConversation *gtkconv, gboolean first);
static GList *generate_invite_user_names(GaimConnection *gc);
static void add_chat_buddy_common(GaimConversation *conv, const char *name,
								  const char *alias);
static gboolean tab_complete(GaimConversation *conv);
static void update_typing_icon(GaimGtkConversation *gtkconv);
static gboolean update_send_as_selection(GaimConvWindow *win);
static char *item_factory_translate_func (const char *path, gpointer func_data);

static GdkColor *get_nick_color(GaimGtkConversation *gtkconv, const char *name) {
	static GdkColor col;
	GtkStyle *style = gtk_widget_get_style(gtkconv->imhtml);
	float scale = ((1-(LUMINANCE(style->base[GTK_STATE_NORMAL]) / LUMINANCE(style->white))) *
			   (LUMINANCE(style->white)/MAX(MAX(col.red, col.blue), col.green)));

	col = nick_colors[g_str_hash(name) % NUM_NICK_COLORS];

	/* The colors are chosen to look fine on white; we should never have to darken */
	if (scale > 1) {
		col.red   *= scale;
		col.green *= scale;
		col.blue  *= scale;
	}

	return &col;
}

static void
do_close(GtkWidget *w, int resp, GaimConvWindow *win)
{
	gtk_widget_destroy(warn_close_dialog);
	warn_close_dialog = NULL;

	if (resp == GTK_RESPONSE_OK)
		gaim_conv_window_destroy(win);
}

static void build_warn_close_dialog(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	GtkWidget *label;
	GtkWidget *vbox, *hbox;
	GtkWidget *img;

	g_return_if_fail(warn_close_dialog == NULL);

	gtkwin = GAIM_GTK_WINDOW(win);

	warn_close_dialog = gtk_dialog_new_with_buttons(
		_("Confirm close"),
		GTK_WINDOW(gtkwin->window), GTK_DIALOG_MODAL,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_CLOSE, GTK_RESPONSE_OK, NULL);

	gtk_dialog_set_default_response(GTK_DIALOG(warn_close_dialog),
					GTK_RESPONSE_OK);

	gtk_container_set_border_width(GTK_CONTAINER(warn_close_dialog),
				       6);
	gtk_window_set_resizable(GTK_WINDOW(warn_close_dialog), FALSE);
	gtk_dialog_set_has_separator(GTK_DIALOG(warn_close_dialog),
				     FALSE);

	/* Setup the outside spacing. */
	vbox = GTK_DIALOG(warn_close_dialog)->vbox;

	gtk_box_set_spacing(GTK_BOX(vbox), 12);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);

	img = gtk_image_new_from_stock(GAIM_STOCK_DIALOG_WARNING,
				       GTK_ICON_SIZE_DIALOG);
	/* Setup the inner hbox and put the dialog's icon in it. */
	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

	/* Setup the right vbox. */
	vbox = gtk_vbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(hbox), vbox);

	label = gtk_label_new(_("You have unread messages. Are you sure you want to close the window?"));
	gtk_widget_set_size_request(label, 350, -1);
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

	/* Connect the signals. */
	g_signal_connect(G_OBJECT(warn_close_dialog), "response",
			 G_CALLBACK(do_close), win);

}

/**************************************************************************
 * Callbacks
 **************************************************************************/
static gint
close_win_cb(GtkWidget *w, GdkEventAny *e, gpointer d)
{
	GaimConvWindow *win = (GaimConvWindow *)d;
	GList *l;

	/* If there are unread messages then show a warning dialog */
	for (l = gaim_conv_window_get_conversations(win);
	     l != NULL; l = l->next)
	{
		GaimConversation *conv = l->data;
		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM &&
		    gaim_conversation_get_unseen(conv) == GAIM_UNSEEN_TEXT)
		{
			build_warn_close_dialog(win);
			gtk_widget_show_all(warn_close_dialog);

			return TRUE;
		}
	}

	gaim_conv_window_destroy(win);

	return TRUE;
}

/*
 * When a conversation window is focused, we know the user
 * has looked at it so we know there are no longer unseen
 * messages.
 */
static gint
focus_win_cb(GtkWidget *w, GdkEventFocus *e, gpointer d)
{
	GaimConvWindow *win = (GaimConvWindow *)d;
	GaimConversation *conv = gaim_conv_window_get_active_conversation(win);

	gaim_conversation_set_unseen(conv, GAIM_UNSEEN_NONE);

	return FALSE;
}

static gint
close_conv_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GList *list = g_list_copy(gtkconv->convs), *l;
	
	l = list;
	while (l) {
		GaimConversation *conv = l->data;
		gaim_conversation_destroy(conv);
		l = l->next;
	}
	
	g_list_free(list);
	
	return TRUE;
}

static gboolean
size_allocate_cb(GtkWidget *w, GtkAllocation *allocation, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	
	if (!GTK_WIDGET_VISIBLE(w))
		return FALSE;

	if (!GAIM_IS_GTK_CONVERSATION(conv))
		return FALSE;

	/* I find that I resize the window when it has a bunch of conversations in it, mostly so that the tab bar
	 * will fit, but then I don't want new windows taking up the entire screen.  I check to see if there is only one
	 * conversation in the window.  This way we'll be setting new windows to the size of the last resized new window. */
	/* I think that the above justification is not the majority, and that the new tab resizing should negate it anyway.  --luke*/
	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
	{
		if (w == gtkconv->imhtml) {
			gaim_prefs_set_int("/gaim/gtk/conversations/im/default_width", allocation->width);
			gaim_prefs_set_int("/gaim/gtk/conversations/im/default_height", allocation->height);
		}
		if (w == gtkconv->entry)
			gaim_prefs_set_int("/gaim/gtk/conversations/im/entry_height", allocation->height);
	}
	else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
	{
		if (w == gtkconv->imhtml) {
			gaim_prefs_set_int("/gaim/gtk/conversations/chat/default_width", allocation->width);
			gaim_prefs_set_int("/gaim/gtk/conversations/chat/default_height", allocation->height);
		}
		if (w == gtkconv->entry)
			gaim_prefs_set_int("/gaim/gtk/conversations/chat/entry_height", allocation->height);
	}

	return FALSE;
}

/* Courtesy of Galeon! */
static void
tab_close_button_state_changed_cb(GtkWidget *widget, GtkStateType prev_state)
{
	if (GTK_WIDGET_STATE(widget) == GTK_STATE_ACTIVE)
		gtk_widget_set_state(widget, GTK_STATE_NORMAL);
}

static void
default_formatize(GaimGtkConversation *c)
{
	GaimConversation *conv = c->active_conv;
		
	if (conv->features & GAIM_CONNECTION_HTML)
	{
		char *color;
		GdkColor fg_color, bg_color;

		if (gaim_prefs_get_bool("/gaim/gtk/conversations/send_bold") != GTK_IMHTML(c->entry)->edit.bold)
			gtk_imhtml_toggle_bold(GTK_IMHTML(c->entry));

		if (gaim_prefs_get_bool("/gaim/gtk/conversations/send_italic") != GTK_IMHTML(c->entry)->edit.italic)
			gtk_imhtml_toggle_italic(GTK_IMHTML(c->entry));

		if (gaim_prefs_get_bool("/gaim/gtk/conversations/send_underline") != GTK_IMHTML(c->entry)->edit.underline)
			gtk_imhtml_toggle_underline(GTK_IMHTML(c->entry));

		gtk_imhtml_toggle_fontface(GTK_IMHTML(c->entry),
			gaim_prefs_get_string("/gaim/gtk/conversations/font_face"));

		if (!(conv->features & GAIM_CONNECTION_NO_FONTSIZE))
			gtk_imhtml_font_set_size(GTK_IMHTML(c->entry),
				gaim_prefs_get_int("/gaim/gtk/conversations/font_size"));

		if(strcmp(gaim_prefs_get_string("/gaim/gtk/conversations/fgcolor"), "") != 0)
		{
			gdk_color_parse(gaim_prefs_get_string("/gaim/gtk/conversations/fgcolor"),
							&fg_color);
			color = g_strdup_printf("#%02x%02x%02x",
									fg_color.red   / 256,
									fg_color.green / 256,
									fg_color.blue  / 256);
		}
		else
			color = g_strdup("");

		gtk_imhtml_toggle_forecolor(GTK_IMHTML(c->entry), color);
		g_free(color);

		if(!(conv->features & GAIM_CONNECTION_NO_BGCOLOR) &&
		   strcmp(gaim_prefs_get_string("/gaim/gtk/conversations/bgcolor"), "") != 0)
		{
			gdk_color_parse(gaim_prefs_get_string("/gaim/gtk/conversations/bgcolor"),
							&bg_color);
			color = g_strdup_printf("#%02x%02x%02x",
									bg_color.red   / 256,
									bg_color.green / 256,
									bg_color.blue  / 256);
		}
		else
			color = g_strdup("");

		gtk_imhtml_toggle_background(GTK_IMHTML(c->entry), color);
		g_free(color);


		if (conv->features & GAIM_CONNECTION_FORMATTING_WBFO)
			gtk_imhtml_set_whole_buffer_formatting_only(GTK_IMHTML(c->entry), TRUE);
		else
			gtk_imhtml_set_whole_buffer_formatting_only(GTK_IMHTML(c->entry), FALSE);
	}
}

static void
clear_formatting_cb(GtkIMHtml *imhtml, GaimGtkConversation *gtkconv)
{
	default_formatize(gtkconv);
}

static const char *
gaim_gtk_get_cmd_prefix(void)
{
	return "/";
}

static GaimCmdRet
say_command_cb(GaimConversation *conv,
              const char *cmd, char **args, char **error, void *data)
{
	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		gaim_conv_im_send(GAIM_CONV_IM(conv), args[0]);
	else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		gaim_conv_chat_send(GAIM_CONV_CHAT(conv), args[0]);

	return GAIM_CMD_RET_OK;
}

static GaimCmdRet
me_command_cb(GaimConversation *conv,
              const char *cmd, char **args, char **error, void *data)
{
	char *tmp;

	tmp = g_strdup_printf("/me %s", args[0]);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		gaim_conv_im_send(GAIM_CONV_IM(conv), tmp);
	else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		gaim_conv_chat_send(GAIM_CONV_CHAT(conv), tmp);

	g_free(tmp);
	return GAIM_CMD_RET_OK;
}

static GaimCmdRet
debug_command_cb(GaimConversation *conv,
                 const char *cmd, char **args, char **error, void *data)
{
	char *tmp, *markup;
	GaimCmdStatus status;

	if (!g_ascii_strcasecmp(args[0], "version")) {
		tmp = g_strdup_printf(_("me is using Gaim v%s."), VERSION);
		markup = g_markup_escape_text(tmp, -1);

		status = gaim_cmd_do_command(conv, tmp, markup, error);

		g_free(tmp);
		g_free(markup);
		return status;
	} else {
		gaim_conversation_write(conv, NULL, _("Supported debug options are:  version"),
		                        GAIM_MESSAGE_NO_LOG|GAIM_MESSAGE_ERROR, time(NULL));
		return GAIM_CMD_STATUS_OK;
	}
}

static GaimCmdRet
clear_command_cb(GaimConversation *conv,
                 const char *cmd, char **args, char **error, void *data)
{
	GaimGtkConversation *gtkconv = NULL;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtk_imhtml_clear(GTK_IMHTML(gtkconv->imhtml));

	return GAIM_CMD_STATUS_OK;
}

static GaimCmdRet
help_command_cb(GaimConversation *conv,
                 const char *cmd, char **args, char **error, void *data)
{
	GList *l, *text;
	GString *s;

	if (args[0] != NULL) {
		s = g_string_new("");
		text = gaim_cmd_help(conv, args[0]);

		if (text) {
			for (l = text; l; l = l->next)
				if (l->next)
					g_string_append_printf(s, "%s\n", (char *)l->data);
				else
					g_string_append_printf(s, "%s", (char *)l->data);
		} else {
			g_string_append(s, _("No such command (in this context)."));
		}
	} else {
		s = g_string_new(_("Use \"/help &lt;command&gt;\" for help on a specific command.\n"
											 "The following commands are available in this context:\n"));

		text = gaim_cmd_list(conv);
		for (l = text; l; l = l->next)
			if (l->next)
				g_string_append_printf(s, "%s, ", (char *)l->data);
			else
				g_string_append_printf(s, "%s.", (char *)l->data);
	}

	gaim_conversation_write(conv, NULL, s->str, GAIM_MESSAGE_NO_LOG, time(NULL));
	g_string_free(s, TRUE);

	return GAIM_CMD_STATUS_OK;
}

static void
send_history_add(GaimConversation *conv, const char *message)
{
	GList *first;

	first = g_list_first(conv->send_history);

	if (first->data)
		g_free(first->data);

	first->data = g_strdup(message);

	conv->send_history = g_list_prepend(first, NULL);
}

static gboolean
check_for_and_do_command(GaimConversation *conv)
{
	GaimGtkConversation *gtkconv;
	char *cmd;
	const char *prefix;
	GaimAccount *account;
	GtkTextIter start;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	account = gaim_conversation_get_account(conv);
	prefix = gaim_gtk_get_cmd_prefix();

	cmd = gtk_imhtml_get_text(GTK_IMHTML(gtkconv->entry), NULL, NULL);
	gtk_text_buffer_get_start_iter(GTK_IMHTML(gtkconv->entry)->text_buffer, &start);

	if (cmd && (strncmp(cmd, prefix, strlen(prefix)) == 0)
	   && !gtk_text_iter_get_child_anchor(&start)) {
		GaimCmdStatus status;
		char *error, *cmdline, *markup, *send_history;
		GtkTextIter end;

		send_history = gtk_imhtml_get_markup(GTK_IMHTML(gtkconv->entry));
		send_history_add(conv, send_history);
		g_free(send_history);

		cmdline = cmd + strlen(prefix);

		gtk_text_iter_forward_chars(&start, g_utf8_strlen(prefix, -1));
		gtk_text_buffer_get_end_iter(GTK_IMHTML(gtkconv->entry)->text_buffer, &end);
		markup = gtk_imhtml_get_markup_range(GTK_IMHTML(gtkconv->entry), &start, &end);
		status = gaim_cmd_do_command(conv, cmdline, markup, &error);
		g_free(cmd);
		g_free(markup);

		switch (status) {
			case GAIM_CMD_STATUS_OK:
				return TRUE;
			case GAIM_CMD_STATUS_NOT_FOUND:
				if (!gaim_prefs_get_bool("/gaim/gtk/conversations/passthrough_unknown_commands")) {
					gaim_conversation_write(conv, "", _("No such command."),
							GAIM_MESSAGE_NO_LOG, time(NULL));

					return TRUE;
				}
				return FALSE;
			case GAIM_CMD_STATUS_WRONG_ARGS:
				gaim_conversation_write(conv, "", _("Syntax Error:  You typed the wrong number of arguments "
								    "to that command."),
						GAIM_MESSAGE_NO_LOG, time(NULL));
				return TRUE;
			case GAIM_CMD_STATUS_FAILED:
				gaim_conversation_write(conv, "", error ? error : _("Your command failed for an unknown reason."),
						GAIM_MESSAGE_NO_LOG, time(NULL));
				if(error)
					g_free(error);
				return TRUE;
			case GAIM_CMD_STATUS_WRONG_TYPE:
				if(gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
					gaim_conversation_write(conv, "", _("That command only works in chats, not IMs."),
							GAIM_MESSAGE_NO_LOG, time(NULL));
				else
					gaim_conversation_write(conv, "", _("That command only works in IMs, not chats."),
							GAIM_MESSAGE_NO_LOG, time(NULL));
				return TRUE;
			case GAIM_CMD_STATUS_WRONG_PRPL:
				gaim_conversation_write(conv, "", _("That command doesn't work on this protocol."),
						GAIM_MESSAGE_NO_LOG, time(NULL));
				return TRUE;
		}
	}

	g_free(cmd);
	return FALSE;
}

static void
send_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimAccount *account;
	GaimConnection *gc;
	char *buf, *clean;

	account = gaim_conversation_get_account(conv);

	if (!gaim_account_is_connected(account))
		return;

	if ((gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) &&
		gaim_conv_chat_has_left(GAIM_CONV_CHAT(conv)))
		return;

	if (check_for_and_do_command(conv)) {
		gtk_imhtml_clear(GTK_IMHTML(gtkconv->entry));
		return;
	}

	buf = gtk_imhtml_get_markup(GTK_IMHTML(gtkconv->entry));
	clean = gtk_imhtml_get_text(GTK_IMHTML(gtkconv->entry), NULL, NULL);

	gtk_widget_grab_focus(gtkconv->entry);

	if (strlen(clean) == 0) {
		g_free(clean);
		return;
	}

	gc = gaim_account_get_connection(account);
	if (gc && (conv->features & GAIM_CONNECTION_NO_NEWLINES)) {
		char **bufs;
		int i;

		bufs = gtk_imhtml_get_markup_lines(GTK_IMHTML(gtkconv->entry));
		for (i = 0; bufs[i]; i++) {
			send_history_add(conv, bufs[i]);
			if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
				gaim_conv_im_send(GAIM_CONV_IM(conv), bufs[i]);
			else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
				gaim_conv_chat_send(GAIM_CONV_CHAT(conv), bufs[i]);
		}

		g_strfreev(bufs);

	} else {
		send_history_add(conv, buf);
		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
			gaim_conv_im_send(GAIM_CONV_IM(conv), buf);
		else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
			gaim_conv_chat_send(GAIM_CONV_CHAT(conv), buf);
	}

	g_free(clean);
	g_free(buf);

	gtk_imhtml_clear(GTK_IMHTML(gtkconv->entry));
}

static void
add_remove_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimAccount *account;
	const char *name;
	GaimConversation *conv = gtkconv->active_conv;

	account = gaim_conversation_get_account(conv);
	name    = gaim_conversation_get_name(conv);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		GaimBuddy *b;

		b = gaim_find_buddy(account, name);
		if (b != NULL)
			gaim_gtkdialogs_remove_buddy(b);
		else if (account != NULL && gaim_account_is_connected(account))
			gaim_blist_request_add_buddy(account, (char *)name, NULL, NULL);
	} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		GaimChat *c;

		c = gaim_blist_find_chat(account, name);
		if (c != NULL)
			gaim_gtkdialogs_remove_chat(c);
		else if (account != NULL && gaim_account_is_connected(account))
			gaim_blist_request_add_chat(account, NULL, NULL, name);
	}

	gtk_widget_grab_focus(GAIM_GTK_CONVERSATION(conv)->entry);
}

static void chat_do_info(GaimGtkConversation *gtkconv, const char *who)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimConnection *gc;

	if ((gc = gaim_conversation_get_gc(conv))) {
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

		/*
		 * If there are special needs for getting info on users in
		 * buddy chat "rooms"...
		 */
		if (prpl_info->get_cb_info != NULL)
		{
			prpl_info->get_cb_info(gc,
				gaim_conv_chat_get_id(GAIM_CONV_CHAT(conv)), who);
		}
		else
			prpl_info->get_info(gc, who);
	}
}


static void
info_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		serv_get_info(gaim_conversation_get_gc(conv),
					  gaim_conversation_get_name(conv));

		gtk_widget_grab_focus(gtkconv->entry);
	} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		/* Get info of the person currently selected in the GtkTreeView */
		GaimGtkChatPane *gtkchat;
		GtkTreeIter iter;
		GtkTreeModel *model;
		GtkTreeSelection *sel;
		char *name;

		gtkchat = gtkconv->u.chat;

		model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));
		sel   = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkchat->list));

		if (gtk_tree_selection_get_selected(sel, NULL, &iter))
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &name, -1);
		else
			return;

		chat_do_info(gtkconv, name);
		g_free(name);
	}
}

static void
block_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimAccount *account;

	account = gaim_conversation_get_account(conv);

	if (account != NULL && gaim_account_is_connected(account))
		gaim_gtk_request_add_block(account, gaim_conversation_get_name(conv));

	gtk_widget_grab_focus(GAIM_GTK_CONVERSATION(conv)->entry);
}

static void
do_invite(GtkWidget *w, int resp, InviteBuddyInfo *info)
{
	const char *buddy, *message;
	GaimGtkConversation *gtkconv;

	gtkconv = GAIM_GTK_CONVERSATION(info->conv);

	if (resp == GTK_RESPONSE_OK) {
		buddy   = gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(info->entry)->entry));
		message = gtk_entry_get_text(GTK_ENTRY(info->message));

		if (!g_ascii_strcasecmp(buddy, ""))
			return;

		serv_chat_invite(gaim_conversation_get_gc(info->conv),
						 gaim_conv_chat_get_id(GAIM_CONV_CHAT(info->conv)),
						 message, buddy);
	}

	gtk_widget_destroy(invite_dialog);
	invite_dialog = NULL;

	g_free(info);
}

static void
invite_dnd_recv(GtkWidget *widget, GdkDragContext *dc, gint x, gint y,
				GtkSelectionData *sd, guint inf, guint t, gpointer data)
{
	InviteBuddyInfo *info = (InviteBuddyInfo *)data;
	const char *convprotocol;

	convprotocol = gaim_account_get_protocol_id(gaim_conversation_get_account(info->conv));

	if (sd->target == gdk_atom_intern("GAIM_BLIST_NODE", FALSE))
	{
		GaimBlistNode *node = NULL;
		GaimBuddy *buddy;

		memcpy(&node, sd->data, sizeof(node));

		if (GAIM_BLIST_NODE_IS_CONTACT(node))
			buddy = gaim_contact_get_priority_buddy((GaimContact *)node);
		else if (GAIM_BLIST_NODE_IS_BUDDY(node))
			buddy = (GaimBuddy *)node;
		else
			return;

		if (strcmp(convprotocol, gaim_account_get_protocol_id(buddy->account)))
		{
			gaim_notify_error(NULL, NULL,
							  _("That buddy is not on the same protocol as this "
								"chat."), NULL);
		}
		else
			gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(info->entry)->entry), buddy->name);

		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
	else if (sd->target == gdk_atom_intern("application/x-im-contact", FALSE))
	{
		char *protocol = NULL;
		char *username = NULL;
		GaimAccount *account;

		if (gaim_gtk_parse_x_im_contact((const char *)sd->data, FALSE, &account,
										&protocol, &username, NULL))
		{
			if (account == NULL)
			{
				gaim_notify_error(NULL, NULL,
					_("You are not currently signed on with an account that "
					  "can invite that buddy."), NULL);
			}
			else if (strcmp(convprotocol, gaim_account_get_protocol_id(account)))
			{
				gaim_notify_error(NULL, NULL,
								  _("That buddy is not on the same protocol as this "
									"chat."), NULL);
			}
			else
			{
				gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(info->entry)->entry), username);
			}
		}

		if (username != NULL) g_free(username);
		if (protocol != NULL) g_free(protocol);

		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
}

static const GtkTargetEntry dnd_targets[] =
{
	{"GAIM_BLIST_NODE", GTK_TARGET_SAME_APP, 0},
	{"application/x-im-contact", 0, 1}
};

static void
invite_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	InviteBuddyInfo *info = NULL;

	if (invite_dialog == NULL) {
		GaimConnection *gc;
		GaimConvWindow *win;
		GaimGtkWindow *gtkwin;
		GtkWidget *label;
		GtkWidget *vbox, *hbox;
		GtkWidget *table;
		GtkWidget *img;

		img = gtk_image_new_from_stock(GAIM_STOCK_DIALOG_QUESTION,
									   GTK_ICON_SIZE_DIALOG);

		info = g_new0(InviteBuddyInfo, 1);
		info->conv = conv;

		gc     = gaim_conversation_get_gc(conv);
		win    = gaim_conversation_get_window(conv);
		gtkwin = GAIM_GTK_WINDOW(win);

		/* Create the new dialog. */
		invite_dialog = gtk_dialog_new_with_buttons(
			_("Invite Buddy Into Chat Room"),
			GTK_WINDOW(gtkwin->window), 0,
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
			GAIM_STOCK_INVITE, GTK_RESPONSE_OK, NULL);

		gtk_dialog_set_default_response(GTK_DIALOG(invite_dialog),
										GTK_RESPONSE_OK);
		gtk_container_set_border_width(GTK_CONTAINER(invite_dialog), GAIM_HIG_BOX_SPACE);
		gtk_window_set_resizable(GTK_WINDOW(invite_dialog), FALSE);
		gtk_dialog_set_has_separator(GTK_DIALOG(invite_dialog), FALSE);

		info->window = GTK_WIDGET(invite_dialog);

		/* Setup the outside spacing. */
		vbox = GTK_DIALOG(invite_dialog)->vbox;

		gtk_box_set_spacing(GTK_BOX(vbox), GAIM_HIG_BORDER);
		gtk_container_set_border_width(GTK_CONTAINER(vbox), GAIM_HIG_BOX_SPACE);

		/* Setup the inner hbox and put the dialog's icon in it. */
		hbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
		gtk_container_add(GTK_CONTAINER(vbox), hbox);
		gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
		gtk_misc_set_alignment(GTK_MISC(img), 0, 0);

		/* Setup the right vbox. */
		vbox = gtk_vbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(hbox), vbox);

		/* Put our happy label in it. */
		label = gtk_label_new(_("Please enter the name of the user you wish "
								"to invite, along with an optional invite "
								"message."));
		gtk_widget_set_size_request(label, 350, -1);
		gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
		gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
		gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

		/* hbox for the table, and to give it some spacing on the left. */
		hbox = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
		gtk_container_add(GTK_CONTAINER(vbox), hbox);

		/* Setup the table we're going to use to lay stuff out. */
		table = gtk_table_new(2, 2, FALSE);
		gtk_table_set_row_spacings(GTK_TABLE(table), GAIM_HIG_BOX_SPACE);
		gtk_table_set_col_spacings(GTK_TABLE(table), GAIM_HIG_BOX_SPACE);
		gtk_container_set_border_width(GTK_CONTAINER(table), GAIM_HIG_BORDER);
		gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);

		/* Now the Buddy label */
		label = gtk_label_new(NULL);
		gtk_label_set_markup_with_mnemonic(GTK_LABEL(label), _("_Buddy:"));
		gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

		/* Now the Buddy drop-down entry field. */
		info->entry = gtk_combo_new();
		gtk_combo_set_case_sensitive(GTK_COMBO(info->entry), FALSE);
		gtk_entry_set_activates_default(
				GTK_ENTRY(GTK_COMBO(info->entry)->entry), TRUE);

		gtk_table_attach_defaults(GTK_TABLE(table), info->entry, 1, 2, 0, 1);
		gtk_label_set_mnemonic_widget(GTK_LABEL(label), info->entry);

		/* Fill in the names. */
		gtk_combo_set_popdown_strings(GTK_COMBO(info->entry),
									  generate_invite_user_names(gc));


		/* Now the label for "Message" */
		label = gtk_label_new(NULL);
		gtk_label_set_markup_with_mnemonic(GTK_LABEL(label), _("_Message:"));
		gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 1, 2);


		/* And finally, the Message entry field. */
		info->message = gtk_entry_new();
		gtk_entry_set_activates_default(GTK_ENTRY(info->message), TRUE);

		gtk_table_attach_defaults(GTK_TABLE(table), info->message, 1, 2, 1, 2);
		gtk_label_set_mnemonic_widget(GTK_LABEL(label), info->message);

		/* Connect the signals. */
		g_signal_connect(G_OBJECT(invite_dialog), "response",
						 G_CALLBACK(do_invite), info);
		/* Setup drag-and-drop */
		gtk_drag_dest_set(info->window,
						  GTK_DEST_DEFAULT_MOTION |
						  GTK_DEST_DEFAULT_DROP,
						  dnd_targets,
						  sizeof(dnd_targets) / sizeof(GtkTargetEntry),
						  GDK_ACTION_COPY);
		gtk_drag_dest_set(info->entry,
						  GTK_DEST_DEFAULT_MOTION |
						  GTK_DEST_DEFAULT_DROP,
						  dnd_targets,
						  sizeof(dnd_targets) / sizeof(GtkTargetEntry),
						  GDK_ACTION_COPY);

		g_signal_connect(G_OBJECT(info->window), "drag_data_received",
						 G_CALLBACK(invite_dnd_recv), info);
		g_signal_connect(G_OBJECT(info->entry), "drag_data_received",
						 G_CALLBACK(invite_dnd_recv), info);

	}

	gtk_widget_show_all(invite_dialog);

	if (info != NULL)
		gtk_widget_grab_focus(GTK_COMBO(info->entry)->entry);
}

static void
menu_new_conv_cb(gpointer data, guint action, GtkWidget *widget)
{
	gaim_gtkdialogs_im();
}

static void
savelog_writefile_cb(void *user_data, const char *filename)
{
	GaimConversation *conv = (GaimConversation *)user_data;
	FILE *fp;
	const char *name;
	gchar *text;

	if ((fp = g_fopen(filename, "w+")) == NULL) {
		gaim_notify_error(conv, NULL, _("Unable to open file."), NULL);
		return;
	}

	name = gaim_conversation_get_name(conv);
	fprintf(fp, "<html>\n<head><title>%s</title></head>\n<body>", name);
	fprintf(fp, _("<h1>Conversation with %s</h1>\n"), name);

	text = gtk_imhtml_get_markup(
		GTK_IMHTML(GAIM_GTK_CONVERSATION(conv)->imhtml));
	fprintf(fp, "%s", text);
	g_free(text);

	fprintf(fp, "\n</body>\n</html>\n");
	fclose(fp);
}

/*
 * It would be kinda cool if this gave the option of saving a
 * plaintext v. HTML file.
 */
static void
menu_save_as_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv = gaim_conv_window_get_active_conversation(win);
	gchar *buf;

	buf = g_strdup_printf("%s.html", gaim_normalize(conv->account, conv->name));

	gaim_request_file(conv, _("Save Conversation"), gaim_escape_filename(buf),
					  TRUE, G_CALLBACK(savelog_writefile_cb), NULL, conv);

	g_free(buf);
}

static void
menu_view_log_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimLogType type;
	const char *name;
	GaimAccount *account;
	GSList *buddies, *cur;

	conv = gaim_conv_window_get_active_conversation(win);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		type = GAIM_LOG_IM;
	else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		type = GAIM_LOG_CHAT;
	else
		return;

	name = gaim_conversation_get_name(conv);
	account = gaim_conversation_get_account(conv);

	buddies = gaim_find_buddies(account, name);
	for (cur = buddies; cur != NULL; cur = cur->next)
	{
		GaimBlistNode *node = cur->data;
		if ((node != NULL) && ((node->prev != NULL) || (node->next != NULL)))
		{
			gaim_gtk_log_show_contact((GaimContact *)node->parent);
			g_slist_free(buddies);
			return;
		}
	}
	g_slist_free(buddies);

	gaim_gtk_log_show(type, name, account);
}

static void
menu_clear_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	conv = gaim_conv_window_get_active_conversation(win);
	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtk_imhtml_clear(GTK_IMHTML(gtkconv->imhtml));
}

struct _search {
	GaimGtkConversation *gtkconv;
	GtkWidget *entry;
};

static void do_search_cb(GtkWidget *widget, gint resp, struct _search *s)
{
	switch (resp) {
	case GTK_RESPONSE_OK:
	    gtk_imhtml_search_find(GTK_IMHTML(s->gtkconv->imhtml),
							   gtk_entry_get_text(GTK_ENTRY(s->entry)));
		break;

	case GTK_RESPONSE_DELETE_EVENT:
	case GTK_RESPONSE_CLOSE:
		gtk_imhtml_search_clear(GTK_IMHTML(s->gtkconv->imhtml));
		gtk_widget_destroy(s->gtkconv->dialogs.search);
		s->gtkconv->dialogs.search = NULL;
		g_free(s);
		break;
	}
}

static void
menu_find_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv = gaim_conv_window_get_active_conversation(win);
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(win);
	GaimGtkConversation *gtkconv = GAIM_GTK_CONVERSATION(conv);
	GtkWidget *hbox;
	GtkWidget *img = gtk_image_new_from_stock(GAIM_STOCK_DIALOG_QUESTION,
											  GTK_ICON_SIZE_DIALOG);
	GtkWidget *label;
	struct _search *s;

	if (gtkconv->dialogs.search) {
		gtk_window_present(GTK_WINDOW(gtkconv->dialogs.search));
		return;
	}

	s = g_malloc(sizeof(struct _search));
	s->gtkconv = gtkconv;

	gtkconv->dialogs.search = gtk_dialog_new_with_buttons(_("Find"),
			GTK_WINDOW(gtkwin->window), GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
			GTK_STOCK_FIND, GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(gtkconv->dialogs.search),
									GTK_RESPONSE_OK);
	g_signal_connect(G_OBJECT(gtkconv->dialogs.search), "response",
					 G_CALLBACK(do_search_cb), s);

	gtk_container_set_border_width(GTK_CONTAINER(gtkconv->dialogs.search), GAIM_HIG_BOX_SPACE);
	gtk_window_set_resizable(GTK_WINDOW(gtkconv->dialogs.search), FALSE);
	gtk_dialog_set_has_separator(GTK_DIALOG(gtkconv->dialogs.search), FALSE);
	gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(gtkconv->dialogs.search)->vbox), GAIM_HIG_BORDER);
	gtk_container_set_border_width(
		GTK_CONTAINER(GTK_DIALOG(gtkconv->dialogs.search)->vbox), GAIM_HIG_BOX_SPACE);

	hbox = gtk_hbox_new(FALSE, GAIM_HIG_BORDER);
	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(gtkconv->dialogs.search)->vbox),
					  hbox);
	gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);

	gtk_misc_set_alignment(GTK_MISC(img), 0, 0);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(gtkconv->dialogs.search),
									  GTK_RESPONSE_OK, FALSE);

	label = gtk_label_new(NULL);
	gtk_label_set_markup_with_mnemonic(GTK_LABEL(label), _("_Search for:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

	s->entry = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(s->entry), TRUE);
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), GTK_WIDGET(s->entry));
	g_signal_connect(G_OBJECT(s->entry), "changed",
					 G_CALLBACK(gaim_gtk_set_sensitive_if_input),
					 gtkconv->dialogs.search);
	gtk_box_pack_start(GTK_BOX(hbox), s->entry, FALSE, FALSE, 0);

	gtk_widget_show_all(gtkconv->dialogs.search);
	gtk_widget_grab_focus(s->entry);
}

static void
menu_send_file_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv = gaim_conv_window_get_active_conversation(win);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		serv_send_file(gaim_conversation_get_gc(conv), gaim_conversation_get_name(conv), NULL);
	}

}

static void
menu_add_pounce_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	gaim_gtkpounce_dialog_show(gaim_conversation_get_account(conv),
							   gaim_conversation_get_name(conv), NULL);
}

static void
menu_insert_link_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GtkIMHtmlToolbar *toolbar;

	conv    = gaim_conv_window_get_active_conversation(win);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	toolbar = GTK_IMHTMLTOOLBAR(gtkconv->toolbar);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbar->link),
		!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toolbar->link)));
}

static void
menu_insert_image_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GtkIMHtmlToolbar *toolbar;

	conv    = gaim_conv_window_get_active_conversation(win);
	gtkconv = GAIM_GTK_CONVERSATION(gaim_conv_window_get_active_conversation(win));
	toolbar = GTK_IMHTMLTOOLBAR(gtkconv->toolbar);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toolbar->image),
		!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toolbar->image)));
}

static void
menu_alias_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimAccount *account;
	const char *name;

	conv    = gaim_conv_window_get_active_conversation(win);
	account = gaim_conversation_get_account(conv);
	name    = gaim_conversation_get_name(conv);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		GaimBuddy *b;

		b = gaim_find_buddy(account, name);
		if (b != NULL)
			gaim_gtkdialogs_alias_buddy(b);
	} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		GaimChat *c;

		c = gaim_blist_find_chat(account, name);
		if (c != NULL)
			gaim_gtkdialogs_alias_chat(c);
	}
}

static void
menu_get_info_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	info_cb(NULL, GAIM_GTK_CONVERSATION(conv));
}

static void
menu_invite_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	invite_cb(NULL, GAIM_GTK_CONVERSATION(conv));
}

static void
menu_block_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	block_cb(NULL, GAIM_GTK_CONVERSATION(conv));
}

static void
menu_add_remove_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	add_remove_cb(NULL, GAIM_GTK_CONVERSATION(conv));
}

static void
menu_close_conv_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;

	close_conv_cb(NULL, GAIM_GTK_CONVERSATION(gaim_conv_window_get_active_conversation(win)));
}

static void
menu_logging_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;

	conv = gaim_conv_window_get_active_conversation(win);

	if (conv == NULL)
		return;

	gaim_conversation_set_logging(conv,
			gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)));
}

static void
menu_toolbar_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	conv = gaim_conv_window_get_active_conversation(win);

	if (conv == NULL)
		return;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gaim_prefs_set_bool("/gaim/gtk/conversations/show_formatting_toolbar",
			    gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)));
}

static void
menu_sounds_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	conv = gaim_conv_window_get_active_conversation(win);

	if (!conv)
		return;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtkconv->make_sound =
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
}

static void
menu_timestamps_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	conv = gaim_conv_window_get_active_conversation(win);

	if (!conv)
		return;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtkconv->show_timestamps =
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	gtk_imhtml_show_comments(GTK_IMHTML(gtkconv->imhtml),
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget)));
}

static void
chat_do_im(GaimGtkConversation *gtkconv, const char *who)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimAccount *account;
	GaimConnection *gc;
	GaimPluginProtocolInfo *prpl_info = NULL;
	char *real_who;

	account = gaim_conversation_get_account(conv);
	g_return_if_fail(account != NULL);

	gc = gaim_account_get_connection(account);
	g_return_if_fail(gc != NULL);

	prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	if (prpl_info && prpl_info->get_cb_real_name)
		real_who = prpl_info->get_cb_real_name(gc,
				gaim_conv_chat_get_id(GAIM_CONV_CHAT(conv)), who);
	else
		real_who = g_strdup(who);

	if(!real_who)
		return;

	gaim_gtkdialogs_im_with_user(account, real_who);

	g_free(real_who);
}

static void
chat_im_button_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimGtkChatPane *gtkchat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	char *name;

	gtkchat = gtkconv->u.chat;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));
	sel   = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkchat->list));

	if (gtk_tree_selection_get_selected(sel, NULL, &iter))
		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &name, -1);
	else
		return;

	chat_do_im(gtkconv, name);
	g_free(name);
}

static void
ignore_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimGtkChatPane *gtkchat;
	GaimConvChat *chat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	char *name;
	char *alias;

	chat    = GAIM_CONV_CHAT(conv);
	gtkchat = gtkconv->u.chat;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));
	sel   = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkchat->list));

	if (gtk_tree_selection_get_selected(sel, NULL, &iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
						   CHAT_USERS_NAME_COLUMN, &name,
						   CHAT_USERS_ALIAS_COLUMN, &alias,
						   -1);
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
	}
	else
		return;

	if (gaim_conv_chat_is_user_ignored(chat, name))
		gaim_conv_chat_unignore(chat, name);
	else
		gaim_conv_chat_ignore(chat, name);

	add_chat_buddy_common(conv, name, alias);
	g_free(name);
	g_free(alias);
}

static void
menu_chat_im_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	const char *who = g_object_get_data(G_OBJECT(w), "user_data");

	chat_do_im(gtkconv, who);
}

static void
menu_chat_send_file_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	const char *who = g_object_get_data(G_OBJECT(w), "user_data");
	GaimConnection *gc  = gaim_conversation_get_gc(conv);

	serv_send_file(gc, who, NULL);
}

static void
menu_chat_info_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	char *who;

	who = g_object_get_data(G_OBJECT(w), "user_data");

	chat_do_info(gtkconv, who);
}

static void
menu_chat_get_away_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimConnection *gc;
	char *who;

	gc  = gaim_conversation_get_gc(conv);
	who = g_object_get_data(G_OBJECT(w), "user_data");

	if (gc != NULL) {
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

		/*
		 * May want to expand this to work similarly to menu_info_cb?
		 */

		if (prpl_info->get_cb_away != NULL)
		{
			prpl_info->get_cb_away(gc,
				gaim_conv_chat_get_id(GAIM_CONV_CHAT(conv)), who);
		}
	}
}

static void
menu_chat_add_remove_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimAccount *account;
	GaimBuddy *b;
	char *name;

	account = gaim_conversation_get_account(conv);
	name    = g_object_get_data(G_OBJECT(w), "user_data");
	b       = gaim_find_buddy(account, name);

	if (b != NULL)
		gaim_gtkdialogs_remove_buddy(b);
	else if (account != NULL && gaim_account_is_connected(account))
		gaim_blist_request_add_buddy(account, name, NULL, NULL);

	gtk_widget_grab_focus(GAIM_GTK_CONVERSATION(conv)->entry);
}

static GtkWidget *
create_chat_menu(GaimConversation *conv, const char *who,
				 GaimPluginProtocolInfo *prpl_info, GaimConnection *gc)
{
	static GtkWidget *menu = NULL;
	gboolean is_me = FALSE;
	GtkWidget *button;

	/*
	 * If a menu already exists, destroy it before creating a new one,
	 * thus freeing-up the memory it occupied.
	 */
	if (menu)
		gtk_widget_destroy(menu);

	if (!(prpl_info->options & OPT_PROTO_UNIQUE_CHATNAME)) {
		char *tmp;

		if (prpl_info->options & OPT_PROTO_USE_DISPLAY_NAME_FOR_ME_IN_CHATS)
			tmp = g_strdup(gaim_normalize(conv->account, gc->display_name));
		else
			tmp = g_strdup(gaim_normalize(conv->account, conv->account->username));

		if (!strcmp(tmp, gaim_normalize(conv->account, who)))
			is_me = TRUE;
		g_free(tmp);
	}

	menu = gtk_menu_new();

	if (!is_me) {
		button = gaim_new_item_from_stock(menu, _("IM"), GAIM_STOCK_IM,
					G_CALLBACK(menu_chat_im_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);

		if (prpl_info && prpl_info->send_file
				&& (!prpl_info->can_receive_file || prpl_info->can_receive_file(gc, who))) {
			button = gaim_new_item_from_stock(menu, _("Send File"), 
				GAIM_STOCK_FILE_TRANSFER, G_CALLBACK(menu_chat_send_file_cb),
				GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
			g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);
		}

		if (gaim_conv_chat_is_user_ignored(GAIM_CONV_CHAT(conv), who))
			button = gaim_new_item_from_stock(menu, _("Un-Ignore"), GAIM_STOCK_IGNORE,
							G_CALLBACK(ignore_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		else
			button = gaim_new_item_from_stock(menu, _("Ignore"), GAIM_STOCK_IGNORE,
							G_CALLBACK(ignore_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);
	}

	if (prpl_info->get_info || prpl_info->get_cb_info) {
		button = gaim_new_item_from_stock(menu, _("Info"), GAIM_STOCK_INFO,
						G_CALLBACK(menu_chat_info_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);
	}

	if (prpl_info->get_cb_away) {
		button = gaim_new_item_from_stock(menu, _("Get Away Message"), GAIM_STOCK_AWAY,
					G_CALLBACK(menu_chat_get_away_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);
	}

	if (!is_me && !(prpl_info->options & OPT_PROTO_UNIQUE_CHATNAME)) {
		if (gaim_find_buddy(gc->account, who))
			button = gaim_new_item_from_stock(menu, _("Remove"), GTK_STOCK_REMOVE,
						G_CALLBACK(menu_chat_add_remove_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		else
			button = gaim_new_item_from_stock(menu, _("Add"), GTK_STOCK_ADD,
						G_CALLBACK(menu_chat_add_remove_cb), GAIM_GTK_CONVERSATION(conv), 0, 0, NULL);
		g_object_set_data_full(G_OBJECT(button), "user_data", g_strdup(who), g_free);
	}

	return menu;
}


static gint
gtkconv_chat_popup_menu_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimGtkChatPane *gtkchat;
	GaimConnection *gc;
	GaimAccount *account;
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkWidget *menu;
	gchar *who;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;
	account = gaim_conversation_get_account(conv);
	gc      = account->gc;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

	if (gc != NULL)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkchat->list));
	if(!gtk_tree_selection_get_selected(sel, NULL, &iter))
		return FALSE;

	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &who, -1);
	menu = create_chat_menu (conv, who, prpl_info, gc);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
				   gaim_gtk_treeview_popup_menu_position_func, widget,
				   0, GDK_CURRENT_TIME);
	g_free(who);

	return TRUE;
}


static gint
right_click_chat_cb(GtkWidget *widget, GdkEventButton *event,
					GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimGtkChatPane *gtkchat;
	GaimConnection *gc;
	GaimAccount *account;
	GtkTreePath *path;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GtkTreeViewColumn *column;
	gchar *who;
	int x, y;

	gtkchat = gtkconv->u.chat;
	account = gaim_conversation_get_account(conv);
	gc      = account->gc;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

	gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(gtkchat->list),
								  event->x, event->y, &path, &column, &x, &y);

	if (path == NULL)
		return FALSE;

	if (gc != NULL)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	gtk_tree_selection_select_path(GTK_TREE_SELECTION(
			gtk_tree_view_get_selection(GTK_TREE_VIEW(gtkchat->list))), path);

	gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &who, -1);

	if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {
		chat_do_im(gtkconv, who);
	} else if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
		GtkWidget *menu = create_chat_menu (conv, who, prpl_info, gc);
		gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
					   event->button, event->time);
	}

	g_free(who);
	gtk_tree_path_free(path);

	return TRUE;
}

static void
move_to_next_unread_tab(GaimGtkConversation *gtkconv, gboolean forward)
{
	GaimGtkConversation *next_gtkconv = NULL;
	GaimConvWindow *win;
	GList *l;
	int index, i, total, found = 0;

	win   = gaim_conversation_get_window(gtkconv->active_conv);
	index = gtk_notebook_page_num(GTK_NOTEBOOK(GAIM_GTK_WINDOW(win)->notebook), gtkconv->tab_cont);
	total = gaim_conv_window_get_conversation_count(win);

	/* First check the tabs after (forward) or before (!forward) this position. */
	for (i = index;
		 !found && (next_gtkconv = gaim_gtk_get_gtkconv_at_index(win, i));
		 forward ? i++ : i--) {
		if (i == -1) {
			break;
		}
		for (l = next_gtkconv->convs; l; l = forward ? l->next : l->prev) {
			GaimConversation *c = l->data;
			if (gaim_conversation_get_unseen(c) > 0)
			{
				found = 1;
				break;
			}
		}
	}

	if (!found) {
		/* Now check from the beginning up to (forward) or end back to (!forward) this position. */
		for (i = forward ? 0 : total - 1;
			 !found && (forward ? i < index : i >= 0) && (next_gtkconv = gaim_gtk_get_gtkconv_at_index(win, i));
			 forward ? i++ : i--) {
			for (l = next_gtkconv->convs; l; l = forward ? l->next : l->prev) {
				GaimConversation *c = l->data;
				if (gaim_conversation_get_unseen(c) > 0) {
					found = 1;
					break;
				}
			}
		}

		if (!found) {
			/* Okay, just grab the next (forward) or previous (!forward) conversation tab. */
			if (forward) {
				index++;
			}
			else {
				index = (index == 0) ? total - 1 : index - 1;
			}
			if (!(next_gtkconv = gaim_gtk_get_gtkconv_at_index(win, index)))
				next_gtkconv = gaim_gtk_get_gtkconv_at_index(win, 0);
		}
	}

	if (next_gtkconv != NULL && next_gtkconv != gtkconv)
		gaim_conv_window_switch_conversation(win,next_gtkconv->active_conv);
}

static gboolean
entry_key_press_cb(GtkWidget *entry, GdkEventKey *event, gpointer data)
{
	GaimConvWindow *win;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GaimGtkWindow *gtkwin;
	int curconv;

	gtkconv  = (GaimGtkConversation *)data;
	conv     = gtkconv->active_conv;
	win      = gaim_conversation_get_window(conv);
	gtkwin   = GAIM_GTK_WINDOW(win);
	curconv = gtk_notebook_get_current_page(GTK_NOTEBOOK(gtkwin->notebook));

	/* If CTRL was held down... */
	if (event->state & GDK_CONTROL_MASK) {
		switch (event->keyval) {
			case GDK_Up:
				if (!conv->send_history)
					break;

				if (!conv->send_history->prev) {
					GtkTextIter start, end;

					if (conv->send_history->data)
						g_free(conv->send_history->data);

					gtk_text_buffer_get_start_iter(gtkconv->entry_buffer,
												   &start);
					gtk_text_buffer_get_end_iter(gtkconv->entry_buffer, &end);

					conv->send_history->data =
						gtk_imhtml_get_markup(GTK_IMHTML(gtkconv->entry));
				}

				if (conv->send_history->next && conv->send_history->next->data) {
					GObject *object;
					GtkTextIter iter;
					GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtkconv->entry));

					conv->send_history = conv->send_history->next;

					/* Block the signal to prevent application of default formatting. */
					object = g_object_ref(G_OBJECT(gtkconv->entry));
					g_signal_handlers_block_matched(object, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
													NULL, gtkconv);
					/* Clear the formatting. */
					gtk_imhtml_clear_formatting(GTK_IMHTML(gtkconv->entry));
					/* Unblock the signal. */
					g_signal_handlers_unblock_matched(object, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
													  NULL, gtkconv);
					g_object_unref(object);

					gtk_imhtml_clear(GTK_IMHTML(gtkconv->entry));
					gtk_imhtml_append_text_with_images(
						GTK_IMHTML(gtkconv->entry), conv->send_history->data,
						0, NULL);
					/* this is mainly just a hack so the formatting at the
					 * cursor gets picked up. */
					gtk_text_buffer_get_end_iter(buffer, &iter);
					gtk_text_buffer_move_mark_by_name(buffer, "insert", &iter);
				}

				return TRUE;
				break;

			case GDK_Down:
				if (!conv->send_history)
					break;

				if (conv->send_history->prev &&	conv->send_history->prev->data) {
					GObject *object;
					GtkTextIter iter;
					GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtkconv->entry));

					conv->send_history = conv->send_history->prev;

					/* Block the signal to prevent application of default formatting. */
					object = g_object_ref(G_OBJECT(gtkconv->entry));
					g_signal_handlers_block_matched(object, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
													NULL, gtkconv);
					/* Clear the formatting. */
					gtk_imhtml_clear_formatting(GTK_IMHTML(gtkconv->entry));
					/* Unblock the signal. */
					g_signal_handlers_unblock_matched(object, G_SIGNAL_MATCH_DATA, 0, 0, NULL,
													  NULL, gtkconv);
					g_object_unref(object);

					gtk_imhtml_clear(GTK_IMHTML(gtkconv->entry));
					gtk_imhtml_append_text_with_images(
						GTK_IMHTML(gtkconv->entry), conv->send_history->data,
						0, NULL);
					/* this is mainly just a hack so the formatting at the
					 * cursor gets picked up. */
					if (*(char *)conv->send_history->data) {
						gtk_text_buffer_get_end_iter(buffer, &iter);
						gtk_text_buffer_move_mark_by_name(buffer, "insert", &iter);
					} else {
						/* Restore the default formatting */
						default_formatize(gtkconv);
					}
				}

				return TRUE;
				break;

			case GDK_Page_Down:
			case ']':
				if (!gaim_gtk_get_gtkconv_at_index(win, curconv + 1))
					gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), 0);
				else
					gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), curconv + 1);
				return TRUE;
				break;

			case GDK_Page_Up:
			case '[':
				if (!gaim_gtk_get_gtkconv_at_index(win, curconv - 1))
					gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), -1);
				else
					gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), curconv - 1);
				return TRUE;
				break;

			case GDK_Tab:
			case GDK_ISO_Left_Tab:
				if (event->state & GDK_SHIFT_MASK) {
					move_to_next_unread_tab(gtkconv, FALSE);
				} else {
					move_to_next_unread_tab(gtkconv, TRUE);
				}

				return TRUE;
				break;

			case GDK_comma:
				gtk_notebook_reorder_child(GTK_NOTEBOOK(gtkwin->notebook),
						gtk_notebook_get_nth_page(GTK_NOTEBOOK(gtkwin->notebook), curconv),
						curconv - 1);
				break;

			case GDK_period:
				gtk_notebook_reorder_child(GTK_NOTEBOOK(gtkwin->notebook),
						gtk_notebook_get_nth_page(GTK_NOTEBOOK(gtkwin->notebook), curconv),
						(curconv + 1) % gtk_notebook_get_n_pages(GTK_NOTEBOOK(gtkwin->notebook)));
				break;

		} /* End of switch */
	}

	/* If ALT (or whatever) was held down... */
	else if (event->state & GDK_MOD1_MASK)
	{
		if (event->keyval > '0' && event->keyval <= '9')
		{
			int switchto = event->keyval - '1';
			if (switchto < gaim_conv_window_get_conversation_count(win))
				gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), switchto);

			return TRUE;
		}
	}

	/* If neither CTRL nor ALT were held down... */
	else
	{
		switch (event->keyval)
		{
			case GDK_Tab:
				return tab_complete(conv);
				break;

			case GDK_Page_Up:
				gtk_imhtml_page_up(GTK_IMHTML(gtkconv->imhtml));
				return TRUE;
				break;

			case GDK_Page_Down:
				gtk_imhtml_page_down(GTK_IMHTML(gtkconv->imhtml));
				return TRUE;
				break;

		}
	}
	return FALSE;
}

/*
 * NOTE:
 *   This guy just kills a single right click from being propagated any
 *   further.  I  have no idea *why* we need this, but we do ...  It
 *   prevents right clicks on the GtkTextView in a convo dialog from
 *   going all the way down to the notebook.  I suspect a bug in
 *   GtkTextView, but I'm not ready to point any fingers yet.
 */
static gboolean
entry_stop_rclick_cb(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
		/* Right single click */
		g_signal_stop_emission_by_name(G_OBJECT(widget), "button_press_event");

		return TRUE;
	}

	return FALSE;
}

/*
 * If someone tries to type into the conversation backlog of a
 * conversation window then we yank focus from the conversation backlog
 * and give it to the text entry box so that people can type
 * all the live long day and it will get entered into the entry box.
 */
static gboolean
refocus_entry_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	GaimGtkConversation *gtkconv = data;

	/* If we have a valid key for the conversation display, then exit */
	if ((event->state & GDK_CONTROL_MASK) ||
		(event->keyval == GDK_F10) ||
		(event->keyval == GDK_Shift_L) ||
		(event->keyval == GDK_Shift_R) ||
		(event->keyval == GDK_Escape) ||
		(event->keyval == GDK_Up) ||
		(event->keyval == GDK_Down) ||
		(event->keyval == GDK_Left) ||
		(event->keyval == GDK_Right) ||
		(event->keyval == GDK_Home) ||
		(event->keyval == GDK_End) ||
		(event->keyval == GDK_Tab) ||
		(event->keyval == GDK_ISO_Left_Tab))
			return FALSE;

	if (event->type == GDK_KEY_RELEASE)
		gtk_widget_grab_focus(gtkconv->entry);

	gtk_widget_event(gtkconv->entry, (GdkEvent *)event);

	return TRUE;
}

static void
menu_conv_sel_send_cb(GObject *m, gpointer data)
{
	GaimConvWindow *win = g_object_get_data(m, "user_data");
	GaimAccount *account = g_object_get_data(m, "gaim_account");
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	if (gtk_check_menu_item_get_active((GtkCheckMenuItem*) m) == FALSE)
		return;

	conv = gaim_conv_window_get_active_conversation(win);

	gaim_conversation_set_account(conv, account);

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtk_imhtml_set_protocol_name(GTK_IMHTML(gtkconv->entry),
	                             gaim_account_get_protocol_name(conv->account));
}

static void
insert_text_cb(GtkTextBuffer *textbuffer, GtkTextIter *position,
			   gchar *new_text, gint new_text_length, gpointer user_data)
{
	GaimGtkConversation *gtkconv = (GaimGtkConversation *)user_data;
	GaimConversation *conv;

	g_return_if_fail(gtkconv != NULL);
	
	conv = gtkconv->active_conv;

	if (!gaim_prefs_get_bool("/core/conversations/im/send_typing"))
		return;

	got_typing_keypress(gtkconv, (gtk_text_iter_is_start(position) &&
	                    gtk_text_iter_is_end(position)));
}

static void
delete_text_cb(GtkTextBuffer *textbuffer, GtkTextIter *start_pos,
			   GtkTextIter *end_pos, gpointer user_data)
{
	GaimGtkConversation *gtkconv = (GaimGtkConversation *)user_data;
	GaimConversation *conv;
	GaimConvIm *im;

	g_return_if_fail(gtkconv != NULL);
	
	conv = gtkconv->active_conv;

	if (!gaim_prefs_get_bool("/core/conversations/im/send_typing"))
		return;

	im = GAIM_CONV_IM(conv);

	if (gtk_text_iter_is_start(start_pos) && gtk_text_iter_is_end(end_pos)) {

		/* We deleted all the text, so turn off typing. */
		if (gaim_conv_im_get_type_again_timeout(im))
			gaim_conv_im_stop_type_again_timeout(im);

		serv_send_typing(gaim_conversation_get_gc(conv),
						 gaim_conversation_get_name(conv),
						 GAIM_NOT_TYPING);
	}
	else {
		/* We're deleting, but not all of it, so it counts as typing. */
		got_typing_keypress(gtkconv, FALSE);
	}
}

static void
notebook_init_grab(GaimGtkWindow *gtkwin, GtkWidget *widget)
{
	static GdkCursor *cursor = NULL;

	gtkwin->in_drag = TRUE;

	if (gtkwin->drag_leave_signal) {
		g_signal_handler_disconnect(G_OBJECT(widget),
									gtkwin->drag_leave_signal);

		gtkwin->drag_leave_signal = 0;
	}

	if (cursor == NULL)
		cursor = gdk_cursor_new(GDK_FLEUR);

	/* Grab the pointer */
	gtk_grab_add(gtkwin->notebook);
#ifndef _WIN32
	/* Currently for win32 GTK+ (as of 2.2.1), gdk_pointer_is_grabbed will
	   always be true after a button press. */
	if (!gdk_pointer_is_grabbed())
#endif
		gdk_pointer_grab(gtkwin->notebook->window, FALSE,
						 GDK_BUTTON1_MOTION_MASK | GDK_BUTTON_RELEASE_MASK,
						 NULL, cursor, GDK_CURRENT_TIME);
}

static gboolean
notebook_motion_cb(GtkWidget *widget, GdkEventButton *e, GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;

	gtkwin = GAIM_GTK_WINDOW(win);

	/*
	 * Make sure the user moved the mouse far enough for the
	 * drag to be initiated.
	 */
	if (gtkwin->in_predrag) {
		if (e->x_root <  gtkwin->drag_min_x ||
			e->x_root >= gtkwin->drag_max_x ||
			e->y_root <  gtkwin->drag_min_y ||
			e->y_root >= gtkwin->drag_max_y) {

			gtkwin->in_predrag = FALSE;
			notebook_init_grab(gtkwin, widget);
		}
	}
	else { /* Otherwise, draw the arrows. */
		GaimConvWindow *dest_win;
		GaimGtkWindow *dest_gtkwin;
		GtkNotebook *dest_notebook;
		GtkWidget *tab;
		gint nb_x, nb_y, page_num;
		gint arrow1_x, arrow1_y, arrow2_x, arrow2_y;
		gboolean horiz_tabs = FALSE;

		/* Get the window that the cursor is over. */
		dest_win = gaim_gtkwin_get_at_xy(e->x_root, e->y_root);

		if (dest_win == NULL) {
			dnd_hints_hide_all();

			return TRUE;
		}

		dest_gtkwin = GAIM_GTK_WINDOW(dest_win);

		dest_notebook = GTK_NOTEBOOK(dest_gtkwin->notebook);

		gdk_window_get_origin(GTK_WIDGET(dest_notebook)->window, &nb_x, &nb_y);

		arrow1_x = arrow2_x = nb_x;
		arrow1_y = arrow2_y = nb_y;

		page_num = gaim_gtkconv_get_tab_at_xy(dest_win,
												   e->x_root, e->y_root);

		if (gtk_notebook_get_tab_pos(dest_notebook) == GTK_POS_TOP ||
			gtk_notebook_get_tab_pos(dest_notebook) == GTK_POS_BOTTOM) {

			horiz_tabs = TRUE;
		}

		tab = gaim_gtk_get_gtkconv_at_index(dest_win, page_num)->tabby;

		if (horiz_tabs) {
			arrow1_x = arrow2_x = nb_x + tab->allocation.x;

			if ((gpointer)gtkwin == (gpointer)dest_gtkwin && gtkwin->drag_tab < page_num) {
				arrow1_x += tab->allocation.width;
				arrow2_x += tab->allocation.width;
			}
			
			arrow1_y = nb_y + tab->allocation.y;
			arrow2_y = nb_y + tab->allocation.y +
							  tab->allocation.height;
		}
		else {
			arrow1_x = nb_x + tab->allocation.x;
			arrow2_x = nb_x + tab->allocation.x +
							  tab->allocation.width;
			arrow1_y = arrow2_y = nb_y + tab->allocation.y + tab->allocation.height/2;

			if ((gpointer)gtkwin == (gpointer)dest_win && gtkwin->drag_tab < page_num) {
				arrow1_y += tab->allocation.height;
				arrow2_y += tab->allocation.height;
			}
		}

		if (horiz_tabs) {
			dnd_hints_show(HINT_ARROW_DOWN, arrow1_x, arrow1_y);
			dnd_hints_show(HINT_ARROW_UP,   arrow2_x, arrow2_y);
		}
		else {
			dnd_hints_show(HINT_ARROW_RIGHT, arrow1_x, arrow1_y);
			dnd_hints_show(HINT_ARROW_LEFT,  arrow2_x, arrow2_y);
		}
	}

	return TRUE;
}

static gboolean
notebook_leave_cb(GtkWidget *widget, GdkEventCrossing *e, GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;

	gtkwin = GAIM_GTK_WINDOW(win);

	if (gtkwin->in_drag)
		return FALSE;

	if (e->x_root <  gtkwin->drag_min_x ||
		e->x_root >= gtkwin->drag_max_x ||
		e->y_root <  gtkwin->drag_min_y ||
		e->y_root >= gtkwin->drag_max_y) {

		gtkwin->in_predrag = FALSE;
		notebook_init_grab(gtkwin, widget);
	}

	return TRUE;
}

/*
 * THANK YOU GALEON!
 */
static gboolean
notebook_press_cb(GtkWidget *widget, GdkEventButton *e, GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	gint nb_x, nb_y, x_rel, y_rel;
	int tab_clicked;
	GtkWidget *page;
	GtkWidget *tab;

	if (e->button != 1 || e->type != GDK_BUTTON_PRESS)
		return FALSE;

	gtkwin = GAIM_GTK_WINDOW(win);

	if (gtkwin->in_drag) {
		gaim_debug(GAIM_DEBUG_WARNING, "gtkconv",
				   "Already in the middle of a window drag at tab_press_cb\n");
		return TRUE;
	}

	/*
	 * Make sure a tab was actually clicked. The arrow buttons
	 * mess things up.
	 */
	tab_clicked = gaim_gtkconv_get_tab_at_xy(win, e->x_root, e->y_root);

	if (tab_clicked == -1)
		return FALSE;

	/*
	 * Get the relative position of the press event, with regards to
	 * the position of the notebook.
	 */
	gdk_window_get_origin(gtkwin->notebook->window, &nb_x, &nb_y);

	x_rel = e->x_root - nb_x;
	y_rel = e->y_root - nb_y;

	/* Reset the min/max x/y */
	gtkwin->drag_min_x = 0;
	gtkwin->drag_min_y = 0;
	gtkwin->drag_max_x = 0;
	gtkwin->drag_max_y = 0;

	/* Find out which tab was dragged. */
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gtkwin->notebook), tab_clicked);
	tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(gtkwin->notebook), page);
	
	gtkwin->drag_min_x = tab->allocation.x      + nb_x;
	gtkwin->drag_min_y = tab->allocation.y      + nb_y;
	gtkwin->drag_max_x = tab->allocation.width  + gtkwin->drag_min_x;
	gtkwin->drag_max_y = tab->allocation.height + gtkwin->drag_min_y;

	/* Make sure the click occurred in the tab. */
	if (e->x_root <  gtkwin->drag_min_x ||
		e->x_root >= gtkwin->drag_max_x ||
		e->y_root <  gtkwin->drag_min_y ||
		e->y_root >= gtkwin->drag_max_y) {

		return FALSE;
	}

	gtkwin->in_predrag = TRUE;
	gtkwin->drag_tab = tab_clicked;

	/* Connect the new motion signals. */
	gtkwin->drag_motion_signal =
		g_signal_connect(G_OBJECT(widget), "motion_notify_event",
						 G_CALLBACK(notebook_motion_cb), win);

	gtkwin->drag_leave_signal =
		g_signal_connect(G_OBJECT(widget), "leave_notify_event",
						 G_CALLBACK(notebook_leave_cb), win);

	return FALSE;
}

static gboolean
notebook_release_cb(GtkWidget *widget, GdkEventButton *e, GaimConvWindow *win)
{
	GaimConvWindow *dest_win;
	GaimGtkWindow *gtkwin;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	gint dest_page_num = 0;
	gboolean new_window = FALSE;

	/*
	 * Don't check to make sure that the event's window matches the
	 * widget's, because we may be getting an event passed on from the
	 * close button.
	 */
	if (e->button != 1 && e->type != GDK_BUTTON_RELEASE)
		return FALSE;

	if (gdk_pointer_is_grabbed()) {
		gdk_pointer_ungrab(GDK_CURRENT_TIME);
		gtk_grab_remove(widget);
	}

	gtkwin = GAIM_GTK_WINDOW(win);

	if (!gtkwin->in_predrag && !gtkwin->in_drag)
		return FALSE;

	/* Disconnect the motion signal. */
	if (gtkwin->drag_motion_signal) {
		g_signal_handler_disconnect(G_OBJECT(widget),
			gtkwin->drag_motion_signal);

		gtkwin->drag_motion_signal = 0;
	}

	/*
	 * If we're in a pre-drag, we'll also need to disconnect the leave
	 * signal.
	 */
	if (gtkwin->in_predrag) {
		gtkwin->in_predrag = FALSE;

		if (gtkwin->drag_leave_signal) {
			g_signal_handler_disconnect(G_OBJECT(widget),
				gtkwin->drag_leave_signal);

			gtkwin->drag_leave_signal = 0;
		}
	}

	/* If we're not in drag...        */
	/* We're perfectly normal people! */
	if (!gtkwin->in_drag)
		return FALSE;

	gtkwin->in_drag = FALSE;

	dnd_hints_hide_all();

	dest_win = gaim_gtkwin_get_at_xy(e->x_root, e->y_root);

	conv = gaim_conv_window_get_active_conversation(win);

	if (dest_win == NULL) {
		/* If the current window doesn't have any other conversations,
		 * there isn't much point transferring the conv to a new window. */
		if (gaim_conv_window_get_conversation_count(win) > 1) {
			/* Make a new window to stick this to. */
			dest_win = gaim_conv_window_new();
			new_window = TRUE;
		}
	}

	if (dest_win == NULL)
		return FALSE;

	gaim_signal_emit(gaim_gtk_conversations_get_handle(),
		"conversation-dragging", win, dest_win);

	/* Get the destination page number. */
	if (!new_window)
		dest_page_num = gaim_gtkconv_get_tab_at_xy(dest_win,
			e->x_root, e->y_root);

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	if (win == dest_win) {
		gtk_notebook_reorder_child(GTK_NOTEBOOK(gtkwin->notebook), gtkconv->tab_cont, dest_page_num);
	}
	else {
		gaim_conv_window_remove_conversation(win, conv);
		gaim_conv_window_add_conversation(dest_win, conv);
		gtk_notebook_reorder_child(GTK_NOTEBOOK(GAIM_GTK_WINDOW(dest_win)->notebook), gtkconv->tab_cont, dest_page_num);
		gaim_conv_window_switch_conversation(dest_win, conv);
		if (new_window) {
			GaimGtkWindow *dest_gtkwin = GAIM_GTK_WINDOW(dest_win);
			gint win_width, win_height;

			gtk_window_get_size(GTK_WINDOW(dest_gtkwin->window),
				&win_width, &win_height);

			gtk_window_move(GTK_WINDOW(dest_gtkwin->window),
				e->x_root - (win_width  / 2),
				e->y_root - (win_height / 2));

			gaim_conv_window_show(dest_win);
		}
	}

	gtk_widget_grab_focus(GAIM_GTK_CONVERSATION(conv)->entry);

	return TRUE;
}

/**************************************************************************
 * A bunch of buddy icon functions
 **************************************************************************/
static GdkPixbuf *
get_tab_icon(GaimConversation *conv, gboolean small_icon)
{
	GaimAccount *account = NULL;
	const char *name = NULL;
	GdkPixbuf *status = NULL;

	g_return_val_if_fail(conv != NULL, NULL);

	account = gaim_conversation_get_account(conv);
	name = gaim_conversation_get_name(conv);

	g_return_val_if_fail(account != NULL, NULL);
	g_return_val_if_fail(name != NULL, NULL);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		GaimBuddy *b = gaim_find_buddy(account, name);
		if (b != NULL) {
			status = gaim_gtk_blist_get_status_icon((GaimBlistNode*)b,
				(small_icon ? GAIM_STATUS_ICON_SMALL : GAIM_STATUS_ICON_LARGE));
		}
	}

	if (!status) {
		GdkPixbuf *pixbuf;
		pixbuf = gaim_gtk_create_prpl_icon(account);

		if (small_icon && pixbuf != NULL)
		{
			status = gdk_pixbuf_scale_simple(pixbuf, 15, 15,
					GDK_INTERP_BILINEAR);
			g_object_unref(pixbuf);
		}
		else
			status = pixbuf;
	}
	return status;
}

static void
update_tab_icon(GaimConversation *conv)
{
	GaimGtkConversation *gtkconv;
	GaimConvWindow *win = gaim_conversation_get_window(conv);
	GaimAccount *account;
	const char *name;
	GdkPixbuf *status = NULL;

	g_return_if_fail(conv != NULL);

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	name = gaim_conversation_get_name(conv);
	account = gaim_conversation_get_account(conv);

	status = get_tab_icon(conv, TRUE);

	g_return_if_fail(status != NULL);

	gtk_image_set_from_pixbuf(GTK_IMAGE(gtkconv->icon), status);
	gtk_image_set_from_pixbuf(GTK_IMAGE(gtkconv->menu_icon), status);

	if (status != NULL)
		g_object_unref(status);

	if (gaim_conv_window_get_active_conversation(win) == conv &&
		(gaim_conversation_get_type(conv) != GAIM_CONV_TYPE_IM ||
		 gtkconv->u.im->anim == NULL))
	{
		status = get_tab_icon(conv, FALSE);

		gtk_window_set_icon(GTK_WINDOW(GAIM_GTK_WINDOW(win)->window), status);

		if (status != NULL)
			g_object_unref(status);
	}
}

static gboolean
redraw_icon(gpointer data)
{
	GaimGtkConversation *gtkconv = (GaimGtkConversation *)data;
	GaimConversation *conv = gtkconv->active_conv;
	GaimAccount *account;
	GaimPluginProtocolInfo *prpl_info = NULL;

	GdkPixbuf *buf;
	GdkPixbuf *scale;
	GdkPixmap *pm;
	GdkBitmap *bm;
	gint delay;
	int scale_width, scale_height;

	if (!g_list_find(gaim_get_ims(), conv)) {
		gaim_debug(GAIM_DEBUG_WARNING, "gtkconv",
				   "Conversation not found in redraw_icon. I think this "
				   "is a bug.\n");
		return FALSE;
	}

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	account = gaim_conversation_get_account(conv);
	if(account && account->gc)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(account->gc->prpl);

	gdk_pixbuf_animation_iter_advance(gtkconv->u.im->iter, NULL);
	buf = gdk_pixbuf_animation_iter_get_pixbuf(gtkconv->u.im->iter);

	gaim_gtk_buddy_icon_get_scale_size(buf, prpl_info ? &prpl_info->icon_spec :
			NULL, &scale_width, &scale_height);

	/* this code is ugly, and scares me */
	scale = gdk_pixbuf_scale_simple(buf,
		MAX(gdk_pixbuf_get_width(buf) * scale_width /
		    gdk_pixbuf_animation_get_width(gtkconv->u.im->anim), 1),
		MAX(gdk_pixbuf_get_height(buf) * scale_height /
		    gdk_pixbuf_animation_get_height(gtkconv->u.im->anim), 1),
		GDK_INTERP_BILINEAR);

	gdk_pixbuf_render_pixmap_and_mask(scale, &pm, &bm, 100);
	g_object_unref(G_OBJECT(scale));
	gtk_image_set_from_pixmap(GTK_IMAGE(gtkconv->u.im->icon), pm, bm);
	g_object_unref(G_OBJECT(pm));
	gtk_widget_queue_draw(gtkconv->u.im->icon);

	if (bm)
		g_object_unref(G_OBJECT(bm));

	delay = gdk_pixbuf_animation_iter_get_delay_time(gtkconv->u.im->iter);

	if (delay < 100)
		delay = 100;

	gtkconv->u.im->icon_timer = g_timeout_add(delay, redraw_icon, conv);

	return FALSE;
}

static void
start_anim(GtkObject *obj, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	int delay;

	if (gtkconv->u.im->anim == NULL)
		return;

	if (gtkconv->u.im->icon_timer != 0)
		return;

	if (gdk_pixbuf_animation_is_static_image(gtkconv->u.im->anim))
		return;

	delay = gdk_pixbuf_animation_iter_get_delay_time(gtkconv->u.im->iter);

	if (delay < 100)
		delay = 100;

	gtkconv->u.im->icon_timer = g_timeout_add(delay, redraw_icon, conv);
}

static void
stop_anim(GtkObject *obj, GaimGtkConversation *gtkconv)
{
	if (gtkconv->u.im->icon_timer != 0)
		g_source_remove(gtkconv->u.im->icon_timer);

	gtkconv->u.im->icon_timer = 0;
}

static void
toggle_icon_animate_cb(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	gtkconv->u.im->animate =
		gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w));

	if (gtkconv->u.im->animate)
		start_anim(NULL, gtkconv);
	else
		stop_anim(NULL, gtkconv);
}

static void
remove_icon(GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimGtkWindow *gtkwin;

	g_return_if_fail(conv != NULL);

	if (gtkconv->u.im->icon_container != NULL)
		gtk_widget_destroy(gtkconv->u.im->icon_container);

	if (gtkconv->u.im->anim != NULL)
		g_object_unref(G_OBJECT(gtkconv->u.im->anim));

	if (gtkconv->u.im->icon_timer != 0)
		g_source_remove(gtkconv->u.im->icon_timer);

	if (gtkconv->u.im->iter != NULL)
		g_object_unref(G_OBJECT(gtkconv->u.im->iter));

	gtkconv->u.im->icon_timer = 0;
	gtkconv->u.im->icon = NULL;
	gtkconv->u.im->anim = NULL;
	gtkconv->u.im->iter = NULL;
	gtkconv->u.im->icon_container = NULL;
	gtkconv->u.im->show_icon = FALSE;

	gtkwin = GAIM_GTK_WINDOW(gaim_conversation_get_window(conv));
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtkwin->menu.show_icon), FALSE);
}

static void
saveicon_writefile_cb(void *user_data, const char *filename)
{
	GaimGtkConversation *gtkconv = (GaimGtkConversation *)user_data;
	GaimConversation *conv = gtkconv->active_conv;
	FILE *fp;
	GaimBuddyIcon *icon;
	const void *data;
	size_t len;

	if ((fp = g_fopen(filename, "wb")) == NULL) {
		gaim_notify_error(conv, NULL, _("Unable to open file."), NULL);
		return;
	}

	icon = gaim_conv_im_get_icon(GAIM_CONV_IM(conv));
	data = gaim_buddy_icon_get_data(icon, &len);

	if ((len <= 0) || (data == NULL)) {
		gaim_notify_error(conv, NULL, _("Unable to save icon file to disk."), NULL);
		return;
	}

	fwrite(data, 1, len, fp);
	fclose(fp);
}

static void
icon_menu_save_cb(GtkWidget *widget, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	const gchar *ext;
	gchar *buf;

	g_return_if_fail(conv != NULL);

	ext = gaim_buddy_icon_get_type(gaim_conv_im_get_icon(GAIM_CONV_IM(conv)));
	if (ext == NULL)
		ext = "icon";

	buf = g_strdup_printf("%s.%s", gaim_normalize(conv->account, conv->name), ext);

	gaim_request_file(conv, _("Save Icon"), buf, TRUE,
					 G_CALLBACK(saveicon_writefile_cb), NULL, conv);

	g_free(buf);
}

static gboolean
icon_menu(GtkObject *obj, GdkEventButton *e, GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	static GtkWidget *menu = NULL;
	GtkWidget *button;

	if (e->button != 3 || e->type != GDK_BUTTON_PRESS)
		return FALSE;

	/*
	 * If a menu already exists, destroy it before creating a new one,
	 * thus freeing-up the memory it occupied.
	 */
	if (menu != NULL)
		gtk_widget_destroy(menu);

	menu = gtk_menu_new();

	if (gtkconv->u.im->anim &&
		!(gdk_pixbuf_animation_is_static_image(gtkconv->u.im->anim)))
	{
		gaim_new_check_item(menu, _("Animate"),
							G_CALLBACK(toggle_icon_animate_cb), conv,
							gtkconv->u.im->icon_timer);
	}

	button = gtk_menu_item_new_with_label(_("Hide Icon"));
	g_signal_connect_swapped(G_OBJECT(button), "activate",
							 G_CALLBACK(remove_icon), gtkconv);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), button);
	gtk_widget_show(button);

	gaim_new_item_from_stock(menu, _("Save Icon As..."), GTK_STOCK_SAVE_AS,
							 G_CALLBACK(icon_menu_save_cb), gtkconv,
							 0, 0, NULL);

	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, e->button, e->time);

	return TRUE;
}

static void
menu_buddyicon_cb(gpointer data, guint action, GtkWidget *widget)
{
	GaimConvWindow *win = (GaimConvWindow *)data;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	gboolean active;

	conv = gaim_conv_window_get_active_conversation(win);

	if (!conv)
		return;

	g_return_if_fail(gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM);

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
	gtkconv->u.im->show_icon = active;
	if (active)
		gaim_gtkconv_update_buddy_icon(conv);
	else
		remove_icon(gtkconv);
}

/**************************************************************************
 * End of the bunch of buddy icon functions
 **************************************************************************/


/*
 * Makes sure all the menu items and all the buttons are hidden/shown and
 * sensitive/insensitive.  This is called after changing tabs and when an
 * account signs on or off.
 */
static void
gray_stuff_out(GaimGtkConversation *gtkconv)
{
	GaimConvWindow *win;
	GaimGtkWindow *gtkwin;
	GaimConversation *conv = gtkconv->active_conv;
	GaimConnection *gc;
	GaimPluginProtocolInfo *prpl_info = NULL;
	GdkPixbuf *window_icon = NULL;
	GtkIMHtmlButtons buttons;
	GaimAccount *account;

	win     = gaim_conversation_get_window(conv);
	gtkwin  = GAIM_GTK_WINDOW(win);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gc      = gaim_conversation_get_gc(conv);
	account = gaim_conversation_get_account(conv);

	if (gc != NULL)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	if (gtkwin->menu.send_as != NULL)
		g_timeout_add(0, (GSourceFunc)update_send_as_selection, win);

	/*
	 * Handle hiding and showing stuff based on what type of conv this is.
	 * Stuff that Gaim IMs support in general should be shown for IM
	 * conversations.  Stuff that Gaim chats support in general should be
	 * shown for chat conversations.  It doesn't matter whether the PRPL
	 * supports it or not--that only affects if the button or menu item
	 * is sensitive or not.
	 */
	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		/* Show stuff that applies to IMs, hide stuff that applies to chats */

		/* Deal with menu items */
		gtk_widget_show(gtkwin->menu.view_log);
		gtk_widget_show(gtkwin->menu.send_file);
		gtk_widget_show(gtkwin->menu.add_pounce);
		gtk_widget_show(gtkwin->menu.get_info);
		gtk_widget_hide(gtkwin->menu.invite);
		gtk_widget_show(gtkwin->menu.alias);
		gtk_widget_show(gtkwin->menu.block);

		if (gaim_find_buddy(account, gaim_conversation_get_name(conv)) == NULL) {
			gtk_widget_show(gtkwin->menu.add);
			gtk_widget_hide(gtkwin->menu.remove);
		} else {
			gtk_widget_show(gtkwin->menu.remove);
			gtk_widget_hide(gtkwin->menu.add);
		}

		gtk_widget_show(gtkwin->menu.insert_link);
		gtk_widget_show(gtkwin->menu.insert_image);
		gtk_widget_show(gtkwin->menu.show_icon);
	} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		/* Show stuff that applies to Chats, hide stuff that applies to IMs */

		/* Deal with menu items */
		gtk_widget_show(gtkwin->menu.view_log);
		gtk_widget_hide(gtkwin->menu.send_file);
		gtk_widget_hide(gtkwin->menu.add_pounce);
		gtk_widget_hide(gtkwin->menu.get_info);
		gtk_widget_show(gtkwin->menu.invite);
		gtk_widget_show(gtkwin->menu.alias);
		gtk_widget_hide(gtkwin->menu.block);
		gtk_widget_hide(gtkwin->menu.show_icon);

		if (gaim_blist_find_chat(account, gaim_conversation_get_name(conv)) == NULL) {
			/* If the chat is NOT in the buddy list */
			gtk_widget_show(gtkwin->menu.add);
			gtk_widget_hide(gtkwin->menu.remove);
		} else {
			/* If the chat IS in the buddy list */
			gtk_widget_hide(gtkwin->menu.add);
			gtk_widget_show(gtkwin->menu.remove);
		}

		gtk_widget_show(gtkwin->menu.insert_link);
		gtk_widget_hide(gtkwin->menu.insert_image);
	}

	/*
	 * Handle graying stuff out based on whether an account is connected
	 * and what features that account supports.
	 */
	if ((gc != NULL) &&
	   ( (gaim_conversation_get_type(conv) != GAIM_CONV_TYPE_CHAT) ||
	    !gaim_conv_chat_has_left(GAIM_CONV_CHAT(conv)) )) {
		/* Account is online */
		/* Deal with the toolbar */
		if (conv->features & GAIM_CONNECTION_HTML) {
			buttons = GTK_IMHTML_ALL;    /* Everything on */
			if (!(prpl_info->options & OPT_PROTO_IM_IMAGE) ||
			    conv->features & GAIM_CONNECTION_NO_IMAGES)
				buttons &= ~GTK_IMHTML_IMAGE;
			if (conv->features & GAIM_CONNECTION_NO_BGCOLOR)
				buttons &= ~GTK_IMHTML_BACKCOLOR;
			if (conv->features & GAIM_CONNECTION_NO_FONTSIZE) {
				buttons &= ~GTK_IMHTML_GROW;
				buttons &= ~GTK_IMHTML_SHRINK;
			}
			if (conv->features & GAIM_CONNECTION_NO_URLDESC)
				buttons &= ~GTK_IMHTML_LINKDESC;
		} else {
			buttons = GTK_IMHTML_SMILEY;
		}
		gtk_imhtml_set_format_functions(GTK_IMHTML(gtkconv->entry), buttons);
		gtk_imhtmltoolbar_associate_smileys(GTK_IMHTMLTOOLBAR(gtkconv->toolbar), gaim_account_get_protocol_id(account));

		/* Deal with menu items */
		gtk_widget_set_sensitive(gtkwin->menu.view_log, TRUE);
		gtk_widget_set_sensitive(gtkwin->menu.add_pounce, TRUE);
		gtk_widget_set_sensitive(gtkwin->menu.get_info, (prpl_info->get_info != NULL));
		gtk_widget_set_sensitive(gtkwin->menu.invite, (prpl_info->chat_invite != NULL));
		gtk_widget_set_sensitive(gtkwin->menu.block, (prpl_info->add_deny != NULL));
		gtk_widget_set_sensitive(gtkwin->menu.insert_link, (conv->features & GAIM_CONNECTION_HTML));
		gtk_widget_set_sensitive(gtkwin->menu.insert_image, (prpl_info->options & OPT_PROTO_IM_IMAGE));

		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
			gtk_widget_set_sensitive(gtkwin->menu.add, (prpl_info->add_buddy != NULL));
			gtk_widget_set_sensitive(gtkwin->menu.remove, (prpl_info->remove_buddy != NULL));
			gtk_widget_set_sensitive(gtkwin->menu.send_file,
					(prpl_info->send_file != NULL && (!prpl_info->can_receive_file ||
					 prpl_info->can_receive_file(gc, gaim_conversation_get_name(conv)))));
			gtk_widget_set_sensitive(gtkwin->menu.alias,
					(gaim_find_buddy(account, gaim_conversation_get_name(conv)) != NULL));
		} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
			gtk_widget_set_sensitive(gtkwin->menu.add, (prpl_info->join_chat != NULL));
			gtk_widget_set_sensitive(gtkwin->menu.remove, (prpl_info->join_chat != NULL));
			gtk_widget_set_sensitive(gtkwin->menu.alias,
					(gaim_blist_find_chat(account, gaim_conversation_get_name(conv)) != NULL));
		}

		/* Deal with chat userlist buttons */
		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		{
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_im, TRUE);
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_ignore, TRUE);
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_info, (prpl_info->get_info != NULL));
		}
	} else {
		/* Account is offline */
		/* Or it's a chat that we've left. */

		/* Then deal with menu items */
		gtk_widget_set_sensitive(gtkwin->menu.view_log, TRUE);
		gtk_widget_set_sensitive(gtkwin->menu.send_file, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.add_pounce, TRUE);
		gtk_widget_set_sensitive(gtkwin->menu.get_info, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.invite, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.alias, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.block, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.add, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.remove, FALSE);
		gtk_widget_set_sensitive(gtkwin->menu.insert_link, TRUE);
		gtk_widget_set_sensitive(gtkwin->menu.insert_image, FALSE);

		/* Deal with chat userlist buttons */
		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		{
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_im, FALSE);
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_ignore, FALSE);
			gtk_widget_set_sensitive(gtkconv->u.chat->userlist_info, FALSE);
		}
	}

	/*
	 * Update the window's icon
	 */
	if ((gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) &&
		(gtkconv->u.im->anim))
	{
		window_icon =
			gdk_pixbuf_animation_get_static_image(gtkconv->u.im->anim);
		g_object_ref(window_icon);
	} else {
		window_icon = get_tab_icon(conv, FALSE);
	}
	gtk_window_set_icon(GTK_WINDOW(gtkwin->window), window_icon);
	if (window_icon != NULL)
		g_object_unref(G_OBJECT(window_icon));
}

static void
before_switch_conv_cb(GtkNotebook *notebook, GtkWidget *page, gint page_num,
				gpointer user_data)
{
	GaimConvWindow *win;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	win = (GaimConvWindow *)user_data;
	conv = gaim_conv_window_get_active_conversation(win);

	g_return_if_fail(conv != NULL);

	if (gaim_conversation_get_type(conv) != GAIM_CONV_TYPE_IM)
		return;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	stop_anim(NULL, gtkconv);
}

static void
switch_conv_cb(GtkNotebook *notebook, GtkWidget *page, gint page_num,
				gpointer user_data)
{
	GaimConvWindow *win;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GaimGtkWindow *gtkwin;

	win = (GaimConvWindow *)user_data;
	gtkconv = gaim_gtk_get_gtkconv_at_index(win, page_num);
	conv = gtkconv->active_conv;

	g_return_if_fail(conv != NULL);

	gtkwin  = GAIM_GTK_WINDOW(win);

	/*
	 * Only set "unseen" to "none" if the window has focus
	 */
	if (gaim_conv_window_has_focus(win))
		gaim_conversation_set_unseen(conv, GAIM_UNSEEN_NONE);

	/* Update the menubar */
	gray_stuff_out(gtkconv);

	update_typing_icon(gtkconv);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtkwin->menu.logging),
				       gaim_conversation_is_logging(conv));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtkwin->menu.sounds),
				       gtkconv->make_sound);

	gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(gtkwin->menu.show_formatting_toolbar),
			gaim_prefs_get_bool("/gaim/gtk/conversations/show_formatting_toolbar"));

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtkwin->menu.show_timestamps),
				       gtkconv->show_timestamps);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(gtkwin->menu.show_icon),
								       gtkconv->u.im->show_icon);
	/*
	 * We pause icons when they are not visible.  If this icon should
	 * be animated then start it back up again.
	 */
	if ((gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) &&
		(gtkconv->u.im->animate))
		start_anim(NULL, gtkconv);

	gtk_window_set_title(GTK_WINDOW(gtkwin->window),
			     gtk_label_get_text(GTK_LABEL(gtkconv->tab_label)));
}

/**************************************************************************
 * Utility functions
 **************************************************************************/

static void
got_typing_keypress(GaimGtkConversation *gtkconv, gboolean first)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimConvIm *im;

	/*
	 * We know we got something, so we at least have to make sure we don't
	 * send GAIM_TYPED any time soon.
	 */

	im = GAIM_CONV_IM(conv);

	if (gaim_conv_im_get_type_again_timeout(im))
		gaim_conv_im_stop_type_again_timeout(im);

	gaim_conv_im_start_type_again_timeout(im);

	if (first || (gaim_conv_im_get_type_again(im) != 0 &&
				  time(NULL) > gaim_conv_im_get_type_again(im))) {

		int timeout = serv_send_typing(gaim_conversation_get_gc(conv),
									   (char *)gaim_conversation_get_name(conv),
									   GAIM_TYPING);

		if (timeout)
			gaim_conv_im_set_type_again(im, time(NULL) + timeout);
		else
			gaim_conv_im_set_type_again(im, 0);
	}
}

static void
update_typing_icon(GaimGtkConversation *gtkconv)
{
	GaimGtkWindow *gtkwin;
	GaimConvIm *im = NULL;
	GaimConversation *conv = gtkconv->active_conv;
	
	gtkwin = GAIM_GTK_WINDOW(gaim_conversation_get_window(conv));

	if(gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		im = GAIM_CONV_IM(conv);

	if(gtkwin->menu.typing_icon) {
		gtk_widget_destroy(gtkwin->menu.typing_icon);
		gtkwin->menu.typing_icon = NULL;
	}
	if(im && gaim_conv_im_get_typing_state(im) == GAIM_TYPING) {
		gtkwin->menu.typing_icon = gtk_image_new_from_stock(GAIM_STOCK_TYPING,
															GTK_ICON_SIZE_MENU);
		gtk_tooltips_set_tip(gtkconv->tooltips, gtkwin->menu.typing_icon,
				_("User is typing..."), NULL);
	} else if(im && gaim_conv_im_get_typing_state(im) == GAIM_TYPED) {
		gtkwin->menu.typing_icon = gtk_image_new_from_stock(GAIM_STOCK_TYPED,
															GTK_ICON_SIZE_MENU);
		gtk_tooltips_set_tip(gtkconv->tooltips, gtkwin->menu.typing_icon,
				_("User has typed something and paused"), NULL);
	}

	if(gtkwin->menu.typing_icon) {
		gtk_widget_show(gtkwin->menu.typing_icon);
		gtk_box_pack_end(GTK_BOX(gtkwin->menu.menubox), gtkwin->menu.typing_icon,
						 FALSE, FALSE, 0);
	}
}

static gboolean
update_send_as_selection(GaimConvWindow *win)
{
	GaimAccount *account;
	GaimConversation *conv;
	GaimGtkWindow *gtkwin;
	GtkWidget *menu;
	GList *child;

	if (g_list_find(gaim_get_windows(), win) == NULL)
		return FALSE;

	conv = gaim_conv_window_get_active_conversation(win);

	if (conv == NULL)
		return FALSE;

	account = gaim_conversation_get_account(conv);
	gtkwin  = GAIM_GTK_WINDOW(win);

	if (gtkwin->menu.send_as == NULL)
		return FALSE;

	gtk_widget_show(gtkwin->menu.send_as);

	menu = gtk_menu_item_get_submenu(
		GTK_MENU_ITEM(gtkwin->menu.send_as));

	for (child = gtk_container_get_children(GTK_CONTAINER(menu));
		 child != NULL;
		 child = child->next) {

		GtkWidget *item = child->data;
		GaimAccount *item_account = g_object_get_data(G_OBJECT(item),
				"gaim_account");

		if (account == item_account) {
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
			break;
		}
	}

	return FALSE;
}

static void
generate_send_as_items(GaimConvWindow *win, GaimConversation *deleted_conv)
{
	GaimGtkWindow *gtkwin;
	GtkWidget *menu;
	GtkWidget *menuitem;
	GList *gcs;
	GList *convs;
	GSList *group = NULL;
	gboolean first_offline = TRUE;
	gboolean found_online = FALSE;
	GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

	gtkwin = GAIM_GTK_WINDOW(win);

	if (gtkwin->menu.send_as != NULL)
		gtk_widget_destroy(gtkwin->menu.send_as);

	/* See if we have > 1 connection active. */
	if (g_list_length(gaim_connections_get_all()) < 2) {
		/* Now make sure we don't have any Offline entries. */
		gboolean found_offline = FALSE;

		for (convs = gaim_get_conversations();
			 convs != NULL;
			 convs = convs->next) {

			GaimConversation *conv;
			GaimAccount *account;

			conv = (GaimConversation *)convs->data;
			account = gaim_conversation_get_account(conv);

			if (account != NULL && account->gc == NULL) {
				found_offline = TRUE;
				break;
			}
		}

		if (!found_offline) {
			gtkwin->menu.send_as = NULL;
			return;
		}
	}

	/* Build the Send As menu */
	gtkwin->menu.send_as = gtk_menu_item_new_with_mnemonic(_("_Send As"));
	gtk_widget_show(gtkwin->menu.send_as);

	menu = gtk_menu_new();
	gtk_menu_shell_insert(GTK_MENU_SHELL(gtkwin->menu.menubar),
						  gtkwin->menu.send_as, 2);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(gtkwin->menu.send_as), menu);

	gtk_widget_show(menu);

	/* Fill it with entries. */
	for (gcs = gaim_connections_get_all(); gcs != NULL; gcs = gcs->next) {

		GaimConnection *gc;
		GaimAccount *account;
		GtkWidget *box;
		GtkWidget *label;
		GtkWidget *image;
		GdkPixbuf *pixbuf, *scale;

		found_online = TRUE;

		gc = (GaimConnection *)gcs->data;

		/* Create a pixmap for the protocol icon. */
		pixbuf = gaim_gtk_create_prpl_icon(gc->account);
		scale = gdk_pixbuf_scale_simple(pixbuf, 16, 16, GDK_INTERP_BILINEAR);

		/* Now convert it to GtkImage */
		if (pixbuf == NULL)
			image = gtk_image_new();
		else
			image = gtk_image_new_from_pixbuf(scale);

		gtk_size_group_add_widget(sg, image);

		g_object_unref(G_OBJECT(scale));
		g_object_unref(G_OBJECT(pixbuf));

		account = gaim_connection_get_account(gc);

		/* Make our menu item */
		menuitem = gtk_radio_menu_item_new_with_label(group,
				gaim_account_get_username(account));
		group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));

		/* Do some evil, see some evil, speak some evil. */
		box = gtk_hbox_new(FALSE, 0);

		label = gtk_bin_get_child(GTK_BIN(menuitem));
		g_object_ref(label);
		gtk_container_remove(GTK_CONTAINER(menuitem), label);

		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 4);

		g_object_unref(label);

		gtk_container_add(GTK_CONTAINER(menuitem), box);

		gtk_widget_show(label);
		gtk_widget_show(image);
		gtk_widget_show(box);

		/* Set our data and callbacks. */
		g_object_set_data(G_OBJECT(menuitem), "user_data", win);
		g_object_set_data(G_OBJECT(menuitem), "gaim_account", gc->account);

		g_signal_connect(G_OBJECT(menuitem), "activate",
						 G_CALLBACK(menu_conv_sel_send_cb), NULL);

		gtk_widget_show(menuitem);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
	}

	/*
	 * Fill it with any accounts that still has an open (yet disabled) window
	 * (signed off accounts with a window open).
	 */
	for (convs = gaim_get_conversations();
		 convs != NULL;
		 convs = convs->next) {

		GaimConversation *conv;
		GaimAccount *account;
		GtkWidget *box;
		GtkWidget *label;
		GtkWidget *image;
		GdkPixbuf *pixbuf, *scale;

		conv = (GaimConversation *)convs->data;

		if (conv == deleted_conv)
			continue;

		account = gaim_conversation_get_account(conv);

		if (account != NULL && account->gc == NULL) {
			if (first_offline && found_online) {
				menuitem = gtk_separator_menu_item_new();
				gtk_widget_show(menuitem);
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);

				first_offline = FALSE;
			}

			/* Create a pixmap for the protocol icon. */
			pixbuf = gaim_gtk_create_prpl_icon(account);
			scale = gdk_pixbuf_scale_simple(pixbuf, 16, 16,
											GDK_INTERP_BILINEAR);

			/* Now convert it to GtkImage */
			if (pixbuf == NULL)
				image = gtk_image_new();
			else
				image = gtk_image_new_from_pixbuf(scale);

			gtk_size_group_add_widget(sg, image);

			if (scale  != NULL) g_object_unref(scale);
			if (pixbuf != NULL) g_object_unref(pixbuf);

			/* Make our menu item */
			menuitem = gtk_radio_menu_item_new_with_label(group,
														  account->username);
			group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(menuitem));

			/* Do some evil, see some evil, speak some evil. */
			box = gtk_hbox_new(FALSE, 0);

			label = gtk_bin_get_child(GTK_BIN(menuitem));
			g_object_ref(label);
			gtk_container_remove(GTK_CONTAINER(menuitem), label);

			gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
			gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 4);

			g_object_unref(label);

			gtk_container_add(GTK_CONTAINER(menuitem), box);

			gtk_widget_show(label);
			gtk_widget_show(image);
			gtk_widget_show(box);

			gtk_widget_set_sensitive(menuitem, FALSE);
			g_object_set_data(G_OBJECT(menuitem), "user_data", win);
			g_object_set_data(G_OBJECT(menuitem), "gaim_account", account);

			g_signal_connect(G_OBJECT(menuitem), "activate",
							 G_CALLBACK(menu_conv_sel_send_cb), NULL);

			gtk_widget_show(menuitem);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
		}
	}

	g_object_unref(sg);

	gtk_widget_show(gtkwin->menu.send_as);
	update_send_as_selection(win);
}

static GList *
generate_invite_user_names(GaimConnection *gc)
{
	GaimBlistNode *gnode,*cnode,*bnode;
	static GList *tmp = NULL;

	if (tmp)
		g_list_free(tmp);

	tmp = g_list_append(NULL, "");

	if (gc != NULL) {
		for(gnode = gaim_get_blist()->root; gnode; gnode = gnode->next) {
			if(!GAIM_BLIST_NODE_IS_GROUP(gnode))
				continue;
			for(cnode = gnode->child; cnode; cnode = cnode->next) {
				if(!GAIM_BLIST_NODE_IS_CONTACT(cnode))
					continue;
				for(bnode = cnode->child; bnode; bnode = bnode->next) {
					GaimBuddy *buddy;

					if(!GAIM_BLIST_NODE_IS_BUDDY(bnode))
						continue;

					buddy = (GaimBuddy *)bnode;

					if (buddy->account == gc->account &&
							GAIM_BUDDY_IS_ONLINE(buddy))
						tmp = g_list_insert_sorted(tmp, buddy->name,
												   (GCompareFunc)g_utf8_collate);
				}
			}
		}
	}

	return tmp;
}

static GdkPixbuf *
get_chat_buddy_status_icon(GaimConvChat *chat, const char *name, GaimConvChatBuddyFlags flags)
{
	GdkPixbuf *pixbuf, *scale, *scale2;
	char *filename;
	const char *image = NULL;

	if (flags & GAIM_CBFLAGS_FOUNDER) {
		image = "founder.png";
	} else if (flags & GAIM_CBFLAGS_OP) {
		image = "op.png";
	} else if (flags & GAIM_CBFLAGS_HALFOP) {
		image = "halfop.png";
	} else if (flags & GAIM_CBFLAGS_VOICE) {
		image = "voice.png";
	} else if ((!flags) && gaim_conv_chat_is_user_ignored(chat, name)) {
		image = "ignored.png";
	} else {
		return NULL;
	}

	filename = g_build_filename(DATADIR, "pixmaps", "gaim", "status", "default", image, NULL);
	pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
	g_free(filename);

	if (!pixbuf)
		return NULL;

	scale = gdk_pixbuf_scale_simple(pixbuf, 15, 15, GDK_INTERP_BILINEAR);
	g_object_unref(pixbuf);

	if (flags && gaim_conv_chat_is_user_ignored(chat, name)) {
		filename = g_build_filename(DATADIR, "pixmaps", "gaim", "status", "default", "ignored.png", NULL);
		pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
		g_free(filename);
		scale2 = gdk_pixbuf_scale_simple(pixbuf, 15, 15, GDK_INTERP_BILINEAR);
		g_object_unref(pixbuf);
		gdk_pixbuf_composite(scale2, scale, 0, 0, 15, 15, 0, 0, 1, 1, GDK_INTERP_BILINEAR, 192);
		g_object_unref(scale2);
	}

	return scale;
}

static void
add_chat_buddy_common(GaimConversation *conv, const char *name, const char *alias)
{
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GaimConvChat *chat;
	GaimConnection *gc;
	GaimPluginProtocolInfo *prpl_info;
	GaimConvChatBuddyFlags flags;
	GtkListStore *ls;
	GdkPixbuf *pixbuf;
	GtkTreeIter iter;
	gboolean is_me = FALSE;
	gboolean is_buddy;
	GdkColor send_color;
 
	gdk_color_parse(SEND_COLOR, &send_color);

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;
	gc      = gaim_conversation_get_gc(conv);

	if (!gc || !(prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)))
		return;

	ls = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list)));

	flags = gaim_conv_chat_user_get_flags(chat, name);
	pixbuf = get_chat_buddy_status_icon(chat, name, flags);

	if (!(prpl_info->options & OPT_PROTO_UNIQUE_CHATNAME)) {
		char *tmp;
		
		if (prpl_info->options & OPT_PROTO_USE_DISPLAY_NAME_FOR_ME_IN_CHATS)
			tmp = g_strdup(gaim_normalize(conv->account, gc->display_name));
		else
			tmp = g_strdup(gaim_normalize(conv->account, conv->account->username));
		
		if (!strcmp(tmp, gaim_normalize(conv->account, name)))
			is_me = TRUE;
		g_free(tmp);
	}

	is_buddy = (gaim_find_buddy(conv->account, name) != NULL);

	gtk_list_store_append(ls, &iter);
	gtk_list_store_set(ls, &iter,
						CHAT_USERS_ICON_COLUMN,  pixbuf,
						CHAT_USERS_ALIAS_COLUMN, alias,
						CHAT_USERS_NAME_COLUMN,  name,
						CHAT_USERS_FLAGS_COLUMN, flags,
						CHAT_USERS_COLOR_COLUMN, is_me ? &send_color : get_nick_color(gtkconv, alias),
						CHAT_USERS_BUDDY_COLUMN, is_buddy,
						-1);

	if (pixbuf)
		g_object_unref(pixbuf);
}

static void
tab_complete_process_item(int *most_matched, char *entered, char **partial, char *nick_partial,
				  GList **matches, gboolean command, char *name)
{
	strncpy(nick_partial, name, strlen(entered));
	nick_partial[strlen(entered)] = '\0';
	if (gaim_utf8_strcasecmp(nick_partial, entered))
		return;

	/* if we're here, it's a possible completion */

	if (*most_matched == -1) {
		/*
		 * this will only get called once, since from now
		 * on *most_matched is >= 0
		 */
		*most_matched = strlen(name);
		*partial = g_strdup(name);
	}
	else if (*most_matched) {
		char *tmp = g_strdup(name);

		while (gaim_utf8_strcasecmp(tmp, *partial)) {
			(*partial)[*most_matched] = '\0';
			if (*most_matched < strlen(tmp))
				tmp[*most_matched] = '\0';
			(*most_matched)--;
		}
		(*most_matched)++;

		g_free(tmp);
	}

	*matches = g_list_insert_sorted(*matches, g_strdup(name),
								   (GCompareFunc)gaim_utf8_strcasecmp);
}

static gboolean
tab_complete(GaimConversation *conv)
{
	GaimGtkConversation *gtkconv;
	GtkTextIter cursor, word_start, start_buffer;
	int start;
	int most_matched = -1;
	char *entered, *partial = NULL;
	char *text;
	char *nick_partial;
	const char *prefix;
	GList *matches = NULL;
	gboolean command = FALSE;

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtk_text_buffer_get_start_iter(gtkconv->entry_buffer, &start_buffer);
	gtk_text_buffer_get_iter_at_mark(gtkconv->entry_buffer, &cursor,
			gtk_text_buffer_get_insert(gtkconv->entry_buffer));

	word_start = cursor;

	/* if there's nothing there just return */
	if (!gtk_text_iter_compare(&cursor, &start_buffer))
		return (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) ? TRUE : FALSE;

	text = gtk_text_buffer_get_text(gtkconv->entry_buffer, &start_buffer,
									&cursor, FALSE);

	/* if we're at the end of ": " we need to move back 2 spaces */
	start = strlen(text) - 1;

	if (strlen(text) >= 2 && !strncmp(&text[start-1], ": ", 2)) {
		gtk_text_iter_backward_chars(&word_start, 2);
		start-=2;
	}

	/* find the start of the word that we're tabbing */
	while (start >= 0 && text[start] != ' ') {
		gtk_text_iter_backward_char(&word_start);
		start--;
	}

	prefix = gaim_gtk_get_cmd_prefix();
	if (start == -1 && (strlen(text) >= strlen(prefix)) && !strncmp(text, prefix, strlen(prefix))) {
		command = TRUE;
		gtk_text_iter_forward_chars(&word_start, strlen(prefix));
	}

	g_free(text);

	entered = gtk_text_buffer_get_text(gtkconv->entry_buffer, &word_start,
									   &cursor, FALSE);

	if (!g_utf8_strlen(entered, -1)) {
		g_free(entered);
		return (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) ? TRUE : FALSE;
	}

	nick_partial = g_malloc(strlen(entered)+1);

	if (command) {
		GList *list = gaim_cmd_list(conv);
		GList *l;

		/* Commands */
		for (l = list; l != NULL; l = l->next) {
			tab_complete_process_item(&most_matched, entered, &partial, nick_partial,
									  &matches, TRUE, l->data);
		}
		g_list_free(list);
	} else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		GaimConvChat *chat = GAIM_CONV_CHAT(conv);
		GList *l = gaim_conv_chat_get_users(chat);
		GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(GAIM_GTK_CONVERSATION(conv)->u.chat->list));
		GtkTreeIter iter;
		int f;

		/* Users */
		for (; l != NULL; l = l->next) {
			tab_complete_process_item(&most_matched, entered, &partial, nick_partial,
									  &matches, TRUE, ((GaimConvChatBuddy *)l->data)->name);
		}


		/* Aliases */
		if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		{
			do {
				char *name;
				char *alias;

				gtk_tree_model_get(model, &iter,
						   CHAT_USERS_NAME_COLUMN, &name,
						   CHAT_USERS_ALIAS_COLUMN, &alias,
						   -1);

				if (strcmp(name, alias))
					tab_complete_process_item(&most_matched, entered, &partial, nick_partial,
										  &matches, FALSE, alias);
				g_free(name);
				g_free(alias);

				f = gtk_tree_model_iter_next(model, &iter);
			} while (f != 0);
		}
	} else {
		g_free(nick_partial);
		g_free(entered);
		return FALSE;
	}

	g_free(nick_partial);

	/* we're only here if we're doing new style */

	/* if there weren't any matches, return */
	if (!matches) {
		/* if matches isn't set partials won't be either */
		g_free(entered);
		return (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) ? TRUE : FALSE;
	}

	gtk_text_buffer_delete(gtkconv->entry_buffer, &word_start, &cursor);

	if (!matches->next) {
		/* there was only one match. fill it in. */
		gtk_text_buffer_get_start_iter(gtkconv->entry_buffer, &start_buffer);
		gtk_text_buffer_get_iter_at_mark(gtkconv->entry_buffer, &cursor,
				gtk_text_buffer_get_insert(gtkconv->entry_buffer));

		if (!gtk_text_iter_compare(&cursor, &start_buffer)) {
			char *tmp = g_strdup_printf("%s: ", (char *)matches->data);
			gtk_text_buffer_insert_at_cursor(gtkconv->entry_buffer, tmp, -1);
			g_free(tmp);
		}
		else
			gtk_text_buffer_insert_at_cursor(gtkconv->entry_buffer,
											 matches->data, -1);

		g_free(matches->data);
		matches = g_list_remove(matches, matches->data);
	}
	else {
		/*
		 * there were lots of matches, fill in as much as possible
		 * and display all of them
		 */
		char *addthis = g_malloc0(1);

		while (matches) {
			char *tmp = addthis;
			addthis = g_strconcat(tmp, matches->data, " ", NULL);
			g_free(tmp);
			g_free(matches->data);
			matches = g_list_remove(matches, matches->data);
		}

		gaim_conversation_write(conv, NULL, addthis, GAIM_MESSAGE_NO_LOG,
								time(NULL));
		gtk_text_buffer_insert_at_cursor(gtkconv->entry_buffer, partial, -1);
		g_free(addthis);
	}

	g_free(entered);
	g_free(partial);

	return TRUE;
}

static GtkItemFactoryEntry menu_items[] =
{
	/* Conversation menu */
	{ N_("/_Conversation"), NULL, NULL, 0, "<Branch>" },

	{ N_("/Conversation/New Instant _Message..."), "<CTL>M", menu_new_conv_cb,
	  0, "<StockItem>", GAIM_STOCK_IM },

	{ "/Conversation/sep0", NULL, NULL, 0, "<Separator>" },

	{ N_("/Conversation/_Find..."), NULL, menu_find_cb, 0,
	  "<StockItem>", GTK_STOCK_FIND },
	{ N_("/Conversation/View _Log"), NULL, menu_view_log_cb, 0, NULL },
	{ N_("/Conversation/_Save As..."), NULL, menu_save_as_cb, 0,
	  "<StockItem>", GTK_STOCK_SAVE_AS },
	{ N_("/Conversation/Clear"), "<CTL>L", menu_clear_cb, 0, "<StockItem>", GTK_STOCK_CLEAR },

	{ "/Conversation/sep1", NULL, NULL, 0, "<Separator>" },

	{ N_("/Conversation/Se_nd File..."), NULL, menu_send_file_cb, 0, "<StockItem>", GAIM_STOCK_FILE_TRANSFER },
	{ N_("/Conversation/Add Buddy _Pounce..."), NULL, menu_add_pounce_cb,
		0, NULL },
	{ N_("/Conversation/_Get Info"), "<CTL>O", menu_get_info_cb, 0,
	  "<StockItem>", GAIM_STOCK_INFO },
	{ N_("/Conversation/In_vite..."), NULL, menu_invite_cb, 0,
	  "<StockItem>", GAIM_STOCK_INVITE },

	{ "/Conversation/sep2", NULL, NULL, 0, "<Separator>" },

	{ N_("/Conversation/Al_ias..."), NULL, menu_alias_cb, 0,
	  "<StockItem>", GAIM_STOCK_EDIT },
	{ N_("/Conversation/_Block..."), NULL, menu_block_cb, 0,
	  "<StockItem>", GAIM_STOCK_BLOCK },
	{ N_("/Conversation/_Add..."), NULL, menu_add_remove_cb, 0,
	  "<StockItem>", GTK_STOCK_ADD },
	{ N_("/Conversation/_Remove..."), NULL, menu_add_remove_cb, 0,
	  "<StockItem>", GTK_STOCK_REMOVE },

	{ "/Conversation/sep3", NULL, NULL, 0, "<Separator>" },

	{ N_("/Conversation/Insert Lin_k..."), NULL, menu_insert_link_cb, 0,
	  "<StockItem>", GAIM_STOCK_LINK },
	{ N_("/Conversation/Insert Imag_e..."), NULL, menu_insert_image_cb, 0,
	  "<StockItem>", GAIM_STOCK_IMAGE },

	{ "/Conversation/sep4", NULL, NULL, 0, "<Separator>" },

	{ N_("/Conversation/_Close"), NULL, menu_close_conv_cb, 0,
	  "<StockItem>", GTK_STOCK_CLOSE },

	/* Options */
	{ N_("/_Options"), NULL, NULL, 0, "<Branch>" },
	{ N_("/Options/Enable _Logging"), NULL, menu_logging_cb, 0, "<CheckItem>" },
	{ N_("/Options/Enable _Sounds"), NULL, menu_sounds_cb, 0, "<CheckItem>" },
	{ N_("/Options/Show Formatting _Toolbars"), NULL, menu_toolbar_cb, 0, "<CheckItem>" },
	{ N_("/Options/Show Ti_mestamps"), "F2", menu_timestamps_cb, 0, "<CheckItem>" },
	{ N_("/Options/Show Buddy _Icon"), NULL, menu_buddyicon_cb, 0, "<CheckItem>" },
};

static const int menu_item_count =
	sizeof(menu_items) / sizeof(*menu_items);

static char *
item_factory_translate_func (const char *path, gpointer func_data)
{
	return _((char *)path);
}

static GtkWidget *
setup_menubar(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	GtkAccelGroup *accel_group;
	GtkWidget *box_item;

	gtkwin = GAIM_GTK_WINDOW(win);

	accel_group = gtk_accel_group_new ();
	gtk_window_add_accel_group (GTK_WINDOW (gtkwin->window), accel_group);
	g_object_unref (accel_group);

	gtkwin->menu.item_factory =
		gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", accel_group);

	gtk_item_factory_set_translate_func(gtkwin->menu.item_factory,
										item_factory_translate_func,
										NULL, NULL);

	gtk_item_factory_create_items(gtkwin->menu.item_factory, menu_item_count,
								  menu_items, win);
	g_signal_connect(G_OBJECT(accel_group), "accel-changed",
									 G_CALLBACK(gaim_gtk_save_accels_cb), NULL);


	gtkwin->menu.menubar =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory, "<main>");


	gtkwin->menu.view_log =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/View Log"));

	/* --- */

	gtkwin->menu.send_file =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Send File..."));

	gtkwin->menu.add_pounce =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Add Buddy Pounce..."));

	/* --- */

	gtkwin->menu.get_info =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Get Info"));

	gtkwin->menu.invite =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Invite..."));

	/* --- */

	gtkwin->menu.alias =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Alias..."));

	gtkwin->menu.block =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Block..."));

	gtkwin->menu.add =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Add..."));

	gtkwin->menu.remove =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Remove..."));

	/* --- */

	gtkwin->menu.insert_link =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Insert Link..."));

	gtkwin->menu.insert_image =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Conversation/Insert Image..."));

	/* --- */

	gtkwin->menu.logging =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Options/Enable Logging"));
	gtkwin->menu.sounds =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Options/Enable Sounds"));
	gtkwin->menu.show_formatting_toolbar =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Options/Show Formatting Toolbars"));
	gtkwin->menu.show_timestamps =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Options/Show Timestamps"));
	gtkwin->menu.show_icon =
		gtk_item_factory_get_widget(gtkwin->menu.item_factory,
									N_("/Options/Show Buddy Icon"));

	generate_send_as_items(win, NULL);

	box_item = gtk_menu_item_new();
	gtk_menu_item_set_right_justified(GTK_MENU_ITEM(box_item), TRUE);
	gtk_menu_shell_append(GTK_MENU_SHELL(gtkwin->menu.menubar), box_item);
	gtk_widget_show(box_item);
	gtk_widget_set_size_request(box_item, -1, 16);

	gtkwin->menu.menubox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(box_item), gtkwin->menu.menubox);
	gtk_widget_show(gtkwin->menu.menubox);

	gtk_widget_show(gtkwin->menu.menubar);

	return gtkwin->menu.menubar;
}

static void topic_callback(GtkWidget *w, GaimGtkConversation *gtkconv)
{
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimConnection *gc;
	GaimConversation *conv = gtkconv->active_conv;
	GaimGtkChatPane *gtkchat;
	char *new_topic;
	const char *current_topic;

	gc      = gaim_conversation_get_gc(conv);

	if(!gc || !(prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl)))
		return;

	if(prpl_info->set_chat_topic == NULL)
		return;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;
	new_topic = g_strdup(gtk_entry_get_text(GTK_ENTRY(gtkchat->topic_text)));
	current_topic = gaim_conv_chat_get_topic(GAIM_CONV_CHAT(conv));

	if(current_topic && !g_utf8_collate(new_topic, current_topic)){
		g_free(new_topic);
		return;
	}

	gtk_entry_set_text(GTK_ENTRY(gtkchat->topic_text), current_topic);

	prpl_info->set_chat_topic(gc, gaim_conv_chat_get_id(GAIM_CONV_CHAT(conv)),
			new_topic);

	g_free(new_topic);
}

static gint
sort_chat_users(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer userdata)
{
	GaimConvChatBuddyFlags f1 = 0, f2 = 0;
	char *user1 = NULL, *user2 = NULL;
	gboolean buddy1 = FALSE, buddy2 = FALSE;
	gint ret = 0;

	gtk_tree_model_get(model, a,
						CHAT_USERS_ALIAS_COLUMN, &user1,
						CHAT_USERS_FLAGS_COLUMN, &f1,
						CHAT_USERS_BUDDY_COLUMN, &buddy1,
						-1);
	gtk_tree_model_get(model, b,
						CHAT_USERS_ALIAS_COLUMN, &user2,
						CHAT_USERS_FLAGS_COLUMN, &f2,
						CHAT_USERS_BUDDY_COLUMN, &buddy2,
						-1);

	if (user1 == NULL || user2 == NULL) {
		if (!(user1 == NULL && user2 == NULL))
			ret = (user1 == NULL) ? -1: 1;
	} else if (f1 != f2) {
		/* sort more important users first */
		ret = (f1 > f2) ? -1 : 1;
	} else if (buddy1 != buddy2) {
		ret = buddy1 ? -1 : 1;
	} else {
		ret = gaim_utf8_strcasecmp(user1, user2);
	}

	g_free(user1);
	g_free(user2);

	return ret;
}

static void
update_chat_alias(GaimBuddy *buddy, GaimConversation *conv, GaimConnection *gc, GaimPluginProtocolInfo *prpl_info)
{
	GaimGtkConversation *gtkconv = GAIM_GTK_CONVERSATION(conv);
	GtkTreeModel *model;
	char *normalized_name;
	GtkTreeIter iter;
	int f;

	g_return_if_fail(buddy != NULL);
	g_return_if_fail(conv != NULL);

	/* This is safe because this callback is only used in chats, not IMs. */
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkconv->u.chat->list));

	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		return;

	normalized_name = g_strdup(gaim_normalize(conv->account, buddy->name));

	do {
		char *name;

		gtk_tree_model_get(model, &iter, CHAT_USERS_NAME_COLUMN, &name, -1);

		if (!strcmp(normalized_name, gaim_normalize(conv->account, name))) {
			const char *alias = name;
			char *tmp;
			GaimBuddy *buddy2;

			if (prpl_info->options & OPT_PROTO_USE_DISPLAY_NAME_FOR_ME_IN_CHATS)
				tmp = g_strdup(gaim_normalize(conv->account, gc->display_name));
			else
				tmp = g_strdup(gaim_normalize(conv->account, conv->account->username));

			if (strcmp(tmp, gaim_normalize(conv->account, name))) {
				/* This user is not me, so look into updating the alias. */

				if ((buddy2 = gaim_find_buddy(conv->account, name)) != NULL)
					alias = gaim_buddy_get_contact_alias(buddy2);

				gtk_list_store_set(GTK_LIST_STORE(model), &iter,
								   CHAT_USERS_ALIAS_COLUMN, alias,
								   CHAT_USERS_COLOR_COLUMN, get_nick_color(gtkconv, alias),
								   -1);
			}
			g_free(tmp);
			g_free(name);
			break;
		}

		f = gtk_tree_model_iter_next(model, &iter);

		g_free(name);
	} while (f != 0);

	g_free(normalized_name);

}

static void
blist_node_aliased_cb(GaimBlistNode *node, const char *old_alias, GaimConversation *conv)
{
	GaimConnection *gc;
	GaimPluginProtocolInfo *prpl_info;

	g_return_if_fail(node != NULL);
	g_return_if_fail(conv != NULL);

	gc = gaim_conversation_get_gc(conv);
	g_return_if_fail(gc != NULL);
	g_return_if_fail(gc->prpl != NULL);
	prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	if (prpl_info->options & OPT_PROTO_UNIQUE_CHATNAME)
		return;

	if (GAIM_BLIST_NODE_IS_CONTACT(node))
	{
		GaimBlistNode *bnode;

		for(bnode = node->child; bnode; bnode = bnode->next) {

			if(!GAIM_BLIST_NODE_IS_BUDDY(bnode))
				continue;

			update_chat_alias((GaimBuddy *)bnode, conv, gc, prpl_info);
		}
	}
	else if (GAIM_BLIST_NODE_IS_BUDDY(node))
		update_chat_alias((GaimBuddy *)node, conv, gc, prpl_info);
}

static void
buddy_cb_common(GaimBuddy *buddy, GaimConversation *conv, gboolean is_buddy)
{
	GtkTreeModel *model;
	char *normalized_name;
	GtkTreeIter iter;
	int f;

	g_return_if_fail(buddy != NULL);
	g_return_if_fail(conv != NULL);

	/* This is safe because this callback is only used in chats, not IMs. */
	model = gtk_tree_view_get_model(GTK_TREE_VIEW(GAIM_GTK_CONVERSATION(conv)->u.chat->list));

	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		return;

	normalized_name = g_strdup(gaim_normalize(conv->account, buddy->name));

	do {
		char *name;

		gtk_tree_model_get(model, &iter, CHAT_USERS_NAME_COLUMN, &name, -1);

		if (!strcmp(normalized_name, gaim_normalize(conv->account, name))) {
			gtk_list_store_set(GTK_LIST_STORE(model), &iter, CHAT_USERS_BUDDY_COLUMN, is_buddy, -1);
			g_free(name);
			break;
		}

		f = gtk_tree_model_iter_next(model, &iter);

		g_free(name);
	} while (f != 0);

	g_free(normalized_name);

	blist_node_aliased_cb((GaimBlistNode *)buddy, NULL, conv);
}

static void
buddy_added_cb(GaimBuddy *buddy, GaimConversation *conv)
{
	buddy_cb_common(buddy, conv, TRUE);
}

static void
buddy_removed_cb(GaimBuddy *buddy, GaimConversation *conv)
{
	/* If there's another buddy for the same "dude" on the list, do nothing. */
	if (gaim_find_buddy(buddy->account, buddy->name) != NULL)
		return;

	buddy_cb_common(buddy, conv, FALSE);
}

static GtkWidget *
setup_chat_pane(GaimGtkConversation *gtkconv)
{
	GaimPluginProtocolInfo *prpl_info = NULL;
	GaimConversation *conv = gtkconv->active_conv;
	GaimGtkChatPane *gtkchat;
	GaimConnection *gc;
	GtkWidget *vpaned, *hpaned;
	GtkWidget *vbox, *hbox, *frame;
	GtkWidget *lbox, *bbox;
	GtkWidget *label;
	GtkWidget *list;
	GtkWidget *button;
	GtkWidget *sw;
	GtkListStore *ls;
	GtkCellRenderer *rend;
	GtkTreeViewColumn *col;
	void *blist_handle = gaim_blist_get_handle();
	GList *focus_chain = NULL;

	gtkchat = gtkconv->u.chat;
	gc      = gaim_conversation_get_gc(conv);

	/* Setup the outer pane. */
	vpaned = gtk_vpaned_new();
	gtk_widget_show(vpaned);

	/* Setup the top part of the pane. */
	vbox = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_paned_pack1(GTK_PANED(vpaned), vbox, TRUE, TRUE);
	gtk_widget_show(vbox);

	if (gc != NULL)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(gc->prpl);

	if (prpl_info->options & OPT_PROTO_CHAT_TOPIC)
	{
		hbox = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
		gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
		gtk_widget_show(hbox);

		label = gtk_label_new(_("Topic:"));
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);
		gtk_widget_show(label);

		gtkchat->topic_text = gtk_entry_new();

		if(prpl_info->set_chat_topic == NULL) {
			gtk_editable_set_editable(GTK_EDITABLE(gtkchat->topic_text), FALSE);
		} else {
			g_signal_connect(GTK_OBJECT(gtkchat->topic_text), "activate",
					G_CALLBACK(topic_callback), gtkconv);
		}

		gtk_box_pack_start(GTK_BOX(hbox), gtkchat->topic_text, TRUE, TRUE, 0);
		gtk_widget_show(gtkchat->topic_text);
	}

	/* Setup the horizontal pane. */
	hpaned = gtk_hpaned_new();
	gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);
	gtk_widget_show(hpaned);

	/* Setup gtkihmtml. */
	frame = gaim_gtk_create_imhtml(FALSE, &gtkconv->imhtml, NULL);
	gtk_widget_set_name(gtkconv->imhtml, "gaim_gtkconv_imhtml");
	gtk_imhtml_show_comments(GTK_IMHTML(gtkconv->imhtml), TRUE);
	gtk_paned_pack1(GTK_PANED(hpaned), frame, TRUE, TRUE);
	gtk_widget_show(frame);

	gtk_widget_set_size_request(gtkconv->imhtml,
			gaim_prefs_get_int("/gaim/gtk/conversations/chat/default_width"),
			gaim_prefs_get_int("/gaim/gtk/conversations/chat/default_height"));
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "size-allocate",
					 G_CALLBACK(size_allocate_cb), gtkconv);

	g_signal_connect_after(G_OBJECT(gtkconv->imhtml), "button_press_event",
						   G_CALLBACK(entry_stop_rclick_cb), NULL);
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "key_press_event",
						   G_CALLBACK(refocus_entry_cb), gtkconv);
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "key_release_event",
						   G_CALLBACK(refocus_entry_cb), gtkconv);

	/* Build the right pane. */
	lbox = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_paned_pack2(GTK_PANED(hpaned), lbox, FALSE, TRUE);
	gtk_widget_show(lbox);

	/* Setup the label telling how many people are in the room. */
	gtkchat->count = gtk_label_new(_("0 people in room"));
	gtk_box_pack_start(GTK_BOX(lbox), gtkchat->count, FALSE, FALSE, 0);
	gtk_widget_show(gtkchat->count);

	/* Setup the list of users. */
	sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
								   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(lbox), sw, TRUE, TRUE, 0);
	gtk_widget_show(sw);

	ls = gtk_list_store_new(CHAT_USERS_COLUMNS, GDK_TYPE_PIXBUF, G_TYPE_STRING,
							G_TYPE_STRING, G_TYPE_INT, GDK_TYPE_COLOR, G_TYPE_BOOLEAN);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(ls), CHAT_USERS_ALIAS_COLUMN,
									sort_chat_users, NULL, NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(ls), CHAT_USERS_ALIAS_COLUMN,
										 GTK_SORT_ASCENDING);

	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));

	rend = gtk_cell_renderer_pixbuf_new();

	col = gtk_tree_view_column_new_with_attributes(NULL, rend,
												   "pixbuf", CHAT_USERS_ICON_COLUMN, NULL);
	gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);

	g_signal_connect(G_OBJECT(list), "button_press_event",
					 G_CALLBACK(right_click_chat_cb), gtkconv);
	g_signal_connect(G_OBJECT(list), "popup-menu",
			 G_CALLBACK(gtkconv_chat_popup_menu_cb), gtkconv);

	rend = gtk_cell_renderer_text_new();

	g_object_set(rend,
				 "foreground-set", TRUE,
				 "weight", PANGO_WEIGHT_BOLD,
				 NULL);
	col = gtk_tree_view_column_new_with_attributes(NULL, rend,
												   "text", CHAT_USERS_ALIAS_COLUMN,
												   "foreground-gdk", CHAT_USERS_COLOR_COLUMN,
												   "weight-set", CHAT_USERS_BUDDY_COLUMN,
												   NULL);

	gaim_signal_connect(blist_handle, "buddy-added",
						gtkchat, GAIM_CALLBACK(buddy_added_cb), conv);
	gaim_signal_connect(blist_handle, "buddy-removed",
						gtkchat, GAIM_CALLBACK(buddy_removed_cb), conv);
	gaim_signal_connect(blist_handle, "blist-node-aliased",
						gtkchat, GAIM_CALLBACK(blist_node_aliased_cb), conv);

#if GTK_CHECK_VERSION(2,6,0)
	gtk_tree_view_column_set_expand(col, TRUE);
	g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
#endif

	gtk_tree_view_append_column(GTK_TREE_VIEW(list), col);

	gtk_widget_set_size_request(list, 150, -1);

	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);
	gtk_widget_show(list);

	gtkchat->list = list;

	gtk_container_add(GTK_CONTAINER(sw), list);

	/* Setup the user list toolbar. */
	bbox = gtk_hbox_new(TRUE, GAIM_HIG_BOX_SPACE);
	gtk_box_pack_start(GTK_BOX(lbox), bbox, FALSE, FALSE, 0);
	gtk_widget_show(bbox);

	/* IM */
	button = gaim_pixbuf_button_from_stock(NULL, GAIM_STOCK_IM,
										   GAIM_BUTTON_VERTICAL);
	gtkchat->userlist_im = button;
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_tooltips_set_tip(gtkconv->tooltips, button, _("IM the user"), NULL);
	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(chat_im_button_cb), gtkconv);

	gtk_widget_show(button);

	/* Ignore */
	button = gaim_pixbuf_button_from_stock(NULL, GAIM_STOCK_IGNORE,
										   GAIM_BUTTON_VERTICAL);
	gtkchat->userlist_ignore = button;
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_tooltips_set_tip(gtkconv->tooltips, button,
						 _("Ignore the user"), NULL);
	g_signal_connect(G_OBJECT(button), "clicked",
					 G_CALLBACK(ignore_cb), gtkconv);
	gtk_widget_show(button);

	/* Info */
	button = gaim_pixbuf_button_from_stock(NULL, GAIM_STOCK_INFO,
										   GAIM_BUTTON_VERTICAL);
	gtkchat->userlist_info = button;
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(bbox), button, FALSE, FALSE, 0);
	gtk_tooltips_set_tip(gtkconv->tooltips, button,
						 _("Get the user's information"), NULL);
	g_signal_connect(G_OBJECT(button), "clicked",
	                 G_CALLBACK(info_cb), gtkconv);

	gtk_widget_show(button);

	/* Setup the bottom half of the conversation window */
	vbox = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_paned_pack2(GTK_PANED(vpaned), vbox, FALSE, TRUE);
	gtk_widget_show(vbox);

	gtkconv->lower_hbox = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_box_pack_start(GTK_BOX(vbox), gtkconv->lower_hbox, TRUE, TRUE, 0);
	gtk_widget_show(gtkconv->lower_hbox);

	vbox = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_box_pack_end(GTK_BOX(gtkconv->lower_hbox), vbox, TRUE, TRUE, 0);
	gtk_widget_show(vbox);

	/* Setup the toolbar, entry widget and all signals */
	frame = gaim_gtk_create_imhtml(TRUE, &gtkconv->entry, &gtkconv->toolbar);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	gtk_widget_set_name(gtkconv->entry, "gaim_gtkconv_entry");
	gtk_imhtml_set_protocol_name(GTK_IMHTML(gtkconv->entry),
								 gaim_account_get_protocol_name(conv->account));
	gtk_widget_set_size_request(gtkconv->entry, -1,
			gaim_prefs_get_int("/gaim/gtk/conversations/chat/entry_height"));
	gtkconv->entry_buffer =
		gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtkconv->entry));
	g_object_set_data(G_OBJECT(gtkconv->entry_buffer), "user_data", gtkconv);

	g_signal_connect(G_OBJECT(gtkconv->entry), "key_press_event",
	                 G_CALLBACK(entry_key_press_cb), gtkconv);
	g_signal_connect(G_OBJECT(gtkconv->entry), "message_send",
	                 G_CALLBACK(send_cb), gtkconv);
	g_signal_connect_after(G_OBJECT(gtkconv->entry), "button_press_event",
	                       G_CALLBACK(entry_stop_rclick_cb), NULL);
	g_signal_connect(G_OBJECT(gtkconv->entry), "size-allocate",
	                 G_CALLBACK(size_allocate_cb), gtkconv);

	default_formatize(gtkconv);

	/*
	 * Focus for chat windows should be as follows:
	 * Tab title -> chat topic -> conversation scrollback -> user list ->
	 *   user list buttons -> entry -> buttons at bottom
	 */
	focus_chain = g_list_prepend(focus_chain, gtkconv->entry);
	gtk_container_set_focus_chain(GTK_CONTAINER(vbox), focus_chain);

	return vpaned;
}

static GtkWidget *
setup_im_pane(GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GtkWidget *frame;
	GtkWidget *paned;
	GtkWidget *vbox;
	GtkWidget *vbox2;
	GList *focus_chain = NULL;

	/* Setup the outer pane */
	paned = gtk_vpaned_new();
	gtk_widget_show(paned);

	/* Setup the top part of the pane */
	vbox = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_paned_pack1(GTK_PANED(paned), vbox, TRUE, TRUE);
	gtk_widget_show(vbox);

	/* Setup the gtkimhtml widget */
	frame = gaim_gtk_create_imhtml(FALSE, &gtkconv->imhtml, NULL);
	gtk_widget_set_name(gtkconv->imhtml, "gaim_gtkconv_imhtml");
	gtk_imhtml_show_comments(GTK_IMHTML(gtkconv->imhtml),TRUE);
	gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	gtk_widget_set_size_request(gtkconv->imhtml,
			gaim_prefs_get_int("/gaim/gtk/conversations/im/default_width"),
			gaim_prefs_get_int("/gaim/gtk/conversations/im/default_height"));
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "size-allocate",
	                 G_CALLBACK(size_allocate_cb), gtkconv);

	g_signal_connect_after(G_OBJECT(gtkconv->imhtml), "button_press_event",
	                       G_CALLBACK(entry_stop_rclick_cb), NULL);
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "key_press_event",
	                 G_CALLBACK(refocus_entry_cb), gtkconv);
	g_signal_connect(G_OBJECT(gtkconv->imhtml), "key_release_event",
	                 G_CALLBACK(refocus_entry_cb), gtkconv);

	/* Setup the bottom half of the conversation window */
	vbox2 = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_paned_pack2(GTK_PANED(paned), vbox2, FALSE, TRUE);
	gtk_widget_show(vbox2);

	gtkconv->lower_hbox = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_box_pack_start(GTK_BOX(vbox2), gtkconv->lower_hbox, TRUE, TRUE, 0);
	gtk_widget_show(gtkconv->lower_hbox);

	vbox2 = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtk_box_pack_end(GTK_BOX(gtkconv->lower_hbox), vbox2, TRUE, TRUE, 0);
	gtk_widget_show(vbox2);

	/* Setup the toolbar, entry widget and all signals */
	frame = gaim_gtk_create_imhtml(TRUE, &gtkconv->entry, &gtkconv->toolbar);
	gtk_box_pack_start(GTK_BOX(vbox2), frame, TRUE, TRUE, 0);
	gtk_widget_show(frame);

	gtk_widget_set_name(gtkconv->entry, "gaim_gtkconv_entry");
	gtk_imhtml_set_protocol_name(GTK_IMHTML(gtkconv->entry),
								 gaim_account_get_protocol_name(conv->account));
	gtk_widget_set_size_request(gtkconv->entry, -1,
			gaim_prefs_get_int("/gaim/gtk/conversations/im/entry_height"));
	gtkconv->entry_buffer =
		gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtkconv->entry));
	g_object_set_data(G_OBJECT(gtkconv->entry_buffer), "user_data", gtkconv);

	g_signal_connect(G_OBJECT(gtkconv->entry), "key_press_event",
	                 G_CALLBACK(entry_key_press_cb), gtkconv);
	g_signal_connect(G_OBJECT(gtkconv->entry), "message_send", G_CALLBACK(send_cb), gtkconv);
	g_signal_connect_after(G_OBJECT(gtkconv->entry), "button_press_event",
	                       G_CALLBACK(entry_stop_rclick_cb), NULL);
	g_signal_connect(G_OBJECT(gtkconv->entry), "size-allocate",
	                 G_CALLBACK(size_allocate_cb), gtkconv);

	g_signal_connect(G_OBJECT(gtkconv->entry_buffer), "insert_text",
	                 G_CALLBACK(insert_text_cb), gtkconv);
	g_signal_connect(G_OBJECT(gtkconv->entry_buffer), "delete_range",
	                 G_CALLBACK(delete_text_cb), gtkconv);

	/* had to move this after the imtoolbar is attached so that the
	 * signals get fired to toggle the buttons on the toolbar as well.
	 */
	default_formatize(gtkconv);

	g_signal_connect_after(G_OBJECT(gtkconv->entry), "format_function_clear",
						   G_CALLBACK(clear_formatting_cb), gtkconv);

	gtkconv->u.im->animate = gaim_prefs_get_bool("/gaim/gtk/conversations/im/animate_buddy_icons");
	gtkconv->u.im->show_icon = TRUE;

	/*
	 * Focus for IM windows should be as follows:
	 * Tab title -> conversation scrollback -> entry
	 */
	focus_chain = g_list_prepend(focus_chain, gtkconv->entry);
	gtk_container_set_focus_chain(GTK_CONTAINER(vbox2), focus_chain);

	return paned;
}

static void
conv_dnd_recv(GtkWidget *widget, GdkDragContext *dc, guint x, guint y,
              GtkSelectionData *sd, guint info, guint t,
              GaimGtkConversation *gtkconv)
{
	GaimConversation *conv = gtkconv->active_conv;
	GaimConvWindow *win = conv->window;
	GaimConversation *c;
	if (sd->target == gdk_atom_intern("GAIM_BLIST_NODE", FALSE))
	{
		GaimBlistNode *n = NULL;
		GaimBuddy *b;

		memcpy(&n, sd->data, sizeof(n));

		if (GAIM_BLIST_NODE_IS_CONTACT(n))
			b = gaim_contact_get_priority_buddy((GaimContact*)n);
		else if (GAIM_BLIST_NODE_IS_BUDDY(n))
			b = (GaimBuddy*)n;
		else
			return;

		/*
		 * If we already have an open conversation with this buddy, then
		 * just move the conv to this window.  Otherwise, create a new
		 * conv and add it to this window.
		 */
		c = gaim_find_conversation_with_account(GAIM_CONV_TYPE_IM, b->name, b->account);
		if (c != NULL) {
			GaimConvWindow *oldwin;
			oldwin = gaim_conversation_get_window(c);
			if (oldwin != win) {
				gaim_conv_window_remove_conversation(oldwin, c);
				gaim_conv_window_add_conversation(win, c);
			}
		} else {
			c = gaim_conversation_new(GAIM_CONV_TYPE_IM, b->account, b->name);
			gaim_conv_window_add_conversation(win, c);
		}

		/* Make this conversation the active conversation */
		gaim_conv_window_switch_conversation(win, c);

		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
	else if (sd->target == gdk_atom_intern("application/x-im-contact", FALSE))
	{
		char *protocol = NULL;
		char *username = NULL;
		GaimAccount *account;

		if (gaim_gtk_parse_x_im_contact((const char *)sd->data, FALSE, &account,
						&protocol, &username, NULL))
		{
			if (account == NULL)
			{
				gaim_notify_error(NULL, NULL,
					_("You are not currently signed on with an account that "
					  "can add that buddy."), NULL);
			}
			else
			{
				c = gaim_conversation_new(GAIM_CONV_TYPE_IM, account, username);
				gaim_conv_window_add_conversation(win, c);
			}
		}

		if (username != NULL) g_free(username);
		if (protocol != NULL) g_free(protocol);

		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
	else if (sd->target == gdk_atom_intern("text/uri-list", FALSE)) {
		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
			gaim_dnd_file_manage(sd, gaim_conversation_get_account(conv), gaim_conversation_get_name(conv));
		gtk_drag_finish(dc, TRUE, (dc->action == GDK_ACTION_MOVE), t);
	}
	else
		gtk_drag_finish(dc, FALSE, FALSE, t);
}

/**************************************************************************
 * GTK+ window ops
 **************************************************************************/
static void
gaim_gtk_new_window(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	GtkPositionType pos;
	GtkWidget *testidea;
	GtkWidget *menubar;

	gtkwin = g_malloc0(sizeof(GaimGtkWindow));

	win->ui_data = gtkwin;

	/* Create the window. */
	gtkwin->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_role(GTK_WINDOW(gtkwin->window), "conversation");
	gtk_window_set_resizable(GTK_WINDOW(gtkwin->window), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(gtkwin->window), 0);
	GTK_WINDOW(gtkwin->window)->allow_shrink = TRUE;

	g_signal_connect(G_OBJECT(gtkwin->window), "delete_event",
					 G_CALLBACK(close_win_cb), win);

	g_signal_connect(G_OBJECT(gtkwin->window), "focus_in_event",
					 G_CALLBACK(focus_win_cb), win);

	/* Create the notebook. */
	gtkwin->notebook = gtk_notebook_new();

	pos = gaim_prefs_get_int("/gaim/gtk/conversations/tab_side");

#if 0
	gtk_notebook_set_tab_hborder(GTK_NOTEBOOK(gtkwin->notebook), 0);
	gtk_notebook_set_tab_vborder(GTK_NOTEBOOK(gtkwin->notebook), 0);
#endif
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(gtkwin->notebook), pos);
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(gtkwin->notebook), TRUE);
	gtk_notebook_popup_enable(GTK_NOTEBOOK(gtkwin->notebook));
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gtkwin->notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(gtkwin->notebook), FALSE);

	gtk_widget_show(gtkwin->notebook);

	g_signal_connect(G_OBJECT(gtkwin->notebook), "switch_page",
					 G_CALLBACK(before_switch_conv_cb), win);
	g_signal_connect_after(G_OBJECT(gtkwin->notebook), "switch_page",
						   G_CALLBACK(switch_conv_cb), win);

	/* Setup the tab drag and drop signals. */
	gtk_widget_add_events(gtkwin->notebook,
				GDK_BUTTON1_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK);
	g_signal_connect(G_OBJECT(gtkwin->notebook), "button_press_event",
					 G_CALLBACK(notebook_press_cb), win);
	g_signal_connect(G_OBJECT(gtkwin->notebook), "button_release_event",
					 G_CALLBACK(notebook_release_cb), win);

	testidea = gtk_vbox_new(FALSE, 0);

	/* Setup the menubar. */
	menubar = setup_menubar(win);
	gtk_box_pack_start(GTK_BOX(testidea), menubar, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(testidea), gtkwin->notebook, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(gtkwin->window), testidea);

	gtk_widget_show(testidea);
}

static void
gaim_gtk_destroy_window(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(win);

	gtk_widget_destroy(gtkwin->window);

	g_object_unref(G_OBJECT(gtkwin->menu.item_factory));

	g_free(gtkwin);
	win->ui_data = NULL;
}

static void
gaim_gtk_show(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(win);

	gtk_widget_show(gtkwin->window);
}

static void
gaim_gtk_hide(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(win);

	gtk_widget_hide(gtkwin->window);
}

static void
gaim_gtk_raise(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(win);

	gtk_widget_show(gtkwin->window);
	gtk_window_deiconify(GTK_WINDOW(gtkwin->window));
	gdk_window_raise(gtkwin->window->window);
}

static void
gaim_gtk_switch_conversation(GaimConvWindow *win, GaimConversation *conv)
{
	GaimGtkWindow *gtkwin;
	GaimGtkConversation *gtkconv = conv->ui_data;

	gtkwin = GAIM_GTK_WINDOW(win);

	gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), gtk_notebook_page_num(GTK_NOTEBOOK(gtkwin->notebook), gtkconv->tab_cont));
}

static const GtkTargetEntry te[] =
{
	GTK_IMHTML_DND_TARGETS,
	{"GAIM_BLIST_NODE", GTK_TARGET_SAME_APP, GTK_IMHTML_DRAG_NUM},
	{"application/x-im-contact", 0, GTK_IMHTML_DRAG_NUM + 1}
};

static GaimGtkConversation *
gaim_gtk_conv_find_gtkconv(GaimConversation * conv)
{
	GaimBuddy *bud = gaim_find_buddy(conv->account, conv->name), *b;
	GaimContact *c;
	GaimBlistNode *cn;

	if (!bud)
		return NULL;

	if (!(c = gaim_buddy_get_contact(bud)))
		return NULL;

	cn = (GaimBlistNode *)c;
	for (b = (GaimBuddy *)cn->child; b; b = (GaimBuddy *) ((GaimBlistNode *)b)->next) {
		GaimConversation *conv;
		if ((conv = gaim_find_conversation_with_account(GAIM_CONV_TYPE_IM, b->name, b->account))) {
			if (conv->ui_data)
				return conv->ui_data;
		}
	}

	return NULL;
}

static void
gaim_gtk_add_conversation(GaimConvWindow *win, GaimConversation *conv)
{
	GaimGtkWindow *gtkwin;
	GaimGtkConversation *gtkconv, *focus_gtkconv;
	GaimConversation *focus_conv;
	GtkWidget *pane = NULL;
	GtkWidget *tab_cont;
	GtkWidget *tabby, *menu_tabby;
	GtkWidget *close_image;
	gboolean new_ui;
	GaimConversationType conv_type;
	const char *name;
	gint close_button_width, close_button_height, focus_width, focus_pad;

	name      = gaim_conversation_get_name(conv);
	conv_type = gaim_conversation_get_type(conv);
	gtkwin    = GAIM_GTK_WINDOW(win);

	if (conv->ui_data != NULL) {
		gtkconv = (GaimGtkConversation *)conv->ui_data;

		tab_cont = gtkconv->tab_cont;

		new_ui = FALSE;
	} else if (conv_type == GAIM_CONV_TYPE_IM && (gtkconv = gaim_gtk_conv_find_gtkconv(conv))) {
		conv->ui_data = gtkconv;
		gtkconv->active_conv = conv;
		if (!g_list_find(gtkconv->convs, conv))
			gtkconv->convs = g_list_prepend(gtkconv->convs, conv);
		return;
	} else {
		gtkconv = g_malloc0(sizeof(GaimGtkConversation));
		conv->ui_data = gtkconv;
		gtkconv->active_conv = conv;
		gtkconv->convs = g_list_prepend(gtkconv->convs, conv);

		/* Setup some initial variables. */
		gtkconv->sg       = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
		gtkconv->tooltips = gtk_tooltips_new();

		if (conv_type == GAIM_CONV_TYPE_IM) {
			gtkconv->u.im = g_malloc0(sizeof(GaimGtkImPane));
			gtkconv->u.im->a_virgin = TRUE;

			pane = setup_im_pane(gtkconv);
		}
		else if (conv_type == GAIM_CONV_TYPE_CHAT) {
			gtkconv->u.chat = g_malloc0(sizeof(GaimGtkChatPane));

			pane = setup_chat_pane(gtkconv);
		}

		if (pane == NULL) {
			if      (conv_type == GAIM_CONV_TYPE_CHAT) g_free(gtkconv->u.chat);
			else if (conv_type == GAIM_CONV_TYPE_IM)   g_free(gtkconv->u.im);

			g_free(gtkconv);
			conv->ui_data = NULL;

			return;
		}

		/* Setup drag-and-drop */
		gtk_drag_dest_set(pane,
				  GTK_DEST_DEFAULT_MOTION |
				  GTK_DEST_DEFAULT_DROP,
				  te, sizeof(te) / sizeof(GtkTargetEntry),
				  GDK_ACTION_COPY);
		gtk_drag_dest_set(pane,
				  GTK_DEST_DEFAULT_MOTION |
				  GTK_DEST_DEFAULT_DROP,
				  te, sizeof(te) / sizeof(GtkTargetEntry),
				  GDK_ACTION_COPY);
		gtk_drag_dest_set(gtkconv->imhtml, 0,
				  te, sizeof(te) / sizeof(GtkTargetEntry),
				  GDK_ACTION_COPY);

		gtk_drag_dest_set(gtkconv->entry, 0,
				  te, sizeof(te) / sizeof(GtkTargetEntry),
				  GDK_ACTION_COPY);

		g_signal_connect(G_OBJECT(pane), "drag_data_received",
				 G_CALLBACK(conv_dnd_recv), gtkconv);
		g_signal_connect(G_OBJECT(gtkconv->imhtml), "drag_data_received",
				 G_CALLBACK(conv_dnd_recv), gtkconv);
		g_signal_connect(G_OBJECT(gtkconv->entry), "drag_data_received",
				 G_CALLBACK(conv_dnd_recv), gtkconv);

		/* Setup the container for the tab. */
		gtkconv->tab_cont = tab_cont = gtk_vbox_new(FALSE, GAIM_HIG_BOX_SPACE);
		g_object_set_data(G_OBJECT(tab_cont), "GaimGtkConversation", gtkconv);
		gtk_container_set_border_width(GTK_CONTAINER(tab_cont), GAIM_HIG_BOX_SPACE);
		gtk_container_add(GTK_CONTAINER(tab_cont), pane);
		gtk_widget_show(pane);

		new_ui = TRUE;

		gtkconv->make_sound = TRUE;

		if (gaim_prefs_get_bool("/gaim/gtk/conversations/show_formatting_toolbar"))
			gtk_widget_show(gtkconv->toolbar);
		else
			gtk_widget_hide(gtkconv->toolbar);

		gtkconv->show_timestamps = TRUE;
		gtk_imhtml_show_comments(GTK_IMHTML(gtkconv->imhtml), TRUE);

		g_signal_connect_swapped(G_OBJECT(pane), "focus",
		                         G_CALLBACK(gtk_widget_grab_focus),
		                         gtkconv->entry);
	}

	gtkconv->tabby = tabby = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);
	gtkconv->menu_tabby = menu_tabby = gtk_hbox_new(FALSE, GAIM_HIG_BOX_SPACE);

	/* Close button. */
	gtkconv->close = gtk_button_new();
	gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &close_button_width, &close_button_height);
	if (gtk_check_version(2, 4, 2) == NULL)
	{
		/* Need to account for extra padding around the gtkbutton */
		gtk_widget_style_get(GTK_WIDGET(gtkconv->close),
							 "focus-line-width", &focus_width,
							 "focus-padding", &focus_pad,
							 NULL);
		close_button_width += (focus_width + focus_pad) * 2;
		close_button_height += (focus_width + focus_pad) * 2;
	}
	gtk_widget_set_size_request(GTK_WIDGET(gtkconv->close),
								close_button_width, close_button_height);

	gtk_button_set_relief(GTK_BUTTON(gtkconv->close), GTK_RELIEF_NONE);
	close_image = gtk_image_new_from_stock(GTK_STOCK_CLOSE, GTK_ICON_SIZE_MENU);
	gtk_widget_show(close_image);
	gtk_container_add(GTK_CONTAINER(gtkconv->close), close_image);
	gtk_tooltips_set_tip(gtkconv->tooltips, gtkconv->close,
						 _("Close conversation"), NULL);

	g_signal_connect(G_OBJECT(gtkconv->close), "clicked",
	                 G_CALLBACK(close_conv_cb), gtkconv);

	/*
	 * I love Galeon. They have a fix for that stupid annoying visible
	 * border bug. I love you guys! -- ChipX86
	 */
	g_signal_connect(G_OBJECT(gtkconv->close), "state_changed",
	                 G_CALLBACK(tab_close_button_state_changed_cb), NULL);

	/* Status icon. */
	gtkconv->icon = gtk_image_new();
	gtkconv->menu_icon = gtk_image_new();
	update_tab_icon(conv);

	/* Tab label. */
	gtkconv->tab_label = gtk_label_new(gaim_conversation_get_title(conv));
#if GTK_CHECK_VERSION(2,6,0)
	g_object_set(G_OBJECT(gtkconv->tab_label), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
#endif
	gtkconv->menu_label = gtk_label_new(gaim_conversation_get_title(conv));
#if 0
	gtk_misc_set_alignment(GTK_MISC(gtkconv->tab_label), 0.00, 0.5);
	gtk_misc_set_padding(GTK_MISC(gtkconv->tab_label), 4, 0);
#endif

	/* Pack it all together. */
	gtk_box_pack_start(GTK_BOX(tabby), gtkconv->icon, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(menu_tabby), gtkconv->menu_icon,
					   FALSE, FALSE, 0);

	gtk_widget_show_all(gtkconv->icon);
	gtk_widget_show_all(gtkconv->menu_icon);

	gtk_box_pack_start(GTK_BOX(tabby), gtkconv->tab_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(menu_tabby), gtkconv->menu_label, TRUE, TRUE, 0);
	gtk_widget_show(gtkconv->tab_label);
	gtk_widget_show(gtkconv->menu_label);
	gtk_misc_set_alignment(GTK_MISC(gtkconv->menu_label), 0, 0);

	gtk_box_pack_start(GTK_BOX(tabby), gtkconv->close, FALSE, FALSE, 0);
	if (gaim_prefs_get_bool("/gaim/gtk/conversations/close_on_tabs"))
		gtk_widget_show(gtkconv->close);

	gtk_widget_show(tabby);
	gtk_widget_show(menu_tabby);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
		gaim_gtkconv_update_buddy_icon(conv);

	/* Add this pane to the conversation's notebook. */
	gtk_notebook_append_page_menu(GTK_NOTEBOOK(gtkwin->notebook), tab_cont, tabby, menu_tabby);
	gtk_notebook_set_tab_label_packing(GTK_NOTEBOOK(gtkwin->notebook), tab_cont, TRUE, TRUE, GTK_PACK_START);


	gtk_widget_show(tab_cont);

	if (gaim_conv_window_get_conversation_count(win) == 1) {
		/* Er, bug in notebooks? Switch to the page manually. */
		gtk_notebook_set_current_page(GTK_NOTEBOOK(gtkwin->notebook), 0);

		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gtkwin->notebook),
				gaim_prefs_get_bool("/gaim/gtk/conversations/tabs"));
	}
	else
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gtkwin->notebook), TRUE);

	focus_conv = g_list_nth_data(gaim_conv_window_get_conversations(win),
			gtk_notebook_get_current_page(GTK_NOTEBOOK(gtkwin->notebook)));
	focus_gtkconv = GAIM_GTK_CONVERSATION(focus_conv);
	gtk_widget_grab_focus(focus_gtkconv->entry);

	if (!new_ui)
		g_object_unref(gtkconv->tab_cont);

	if (gaim_conv_window_get_conversation_count(win) == 1)
		g_timeout_add(0, (GSourceFunc)update_send_as_selection, win);
}

static void
gaim_gtk_remove_conversation(GaimConvWindow *win, GaimConversation *conv)
{
	GaimGtkWindow *gtkwin;
	GaimGtkConversation *gtkconv;
	unsigned int index;
	GaimConversationType conv_type;

	conv_type = gaim_conversation_get_type(conv);
	gtkwin  = GAIM_GTK_WINDOW(win);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	index = gtk_notebook_page_num(GTK_NOTEBOOK(gtkwin->notebook), gtkconv->tab_cont);

	g_object_ref(gtkconv->tab_cont);
	gtk_object_sink(GTK_OBJECT(gtkconv->tab_cont));

	gtk_notebook_remove_page(GTK_NOTEBOOK(gtkwin->notebook), index);

	/* go back to tabless if need be */
	if (gaim_conv_window_get_conversation_count(win) <= 2) {
		gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gtkwin->notebook),
				gaim_prefs_get_bool("/gaim/gtk/conversations/tabs"));
	}


	/* If this window is setup with an inactive gc, regenerate the menu. */
	if (conv_type == GAIM_CONV_TYPE_IM &&
		gaim_conversation_get_gc(conv) == NULL) {

		generate_send_as_items(win, conv);
	}
}

GaimGtkConversation *
gaim_gtk_get_gtkconv_at_index(const GaimConvWindow *win, int index)
{
	GaimGtkWindow *gtkwin;
	GtkWidget *tab_cont;

	gtkwin = GAIM_GTK_WINDOW(win);

	if (index == -1)
		index = 0;
	tab_cont = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gtkwin->notebook), index);
	return g_object_get_data(G_OBJECT(tab_cont), "GaimGtkConversation");
}

static GaimConversation *
gaim_gtk_get_active_conversation(const GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	GaimGtkConversation *gtkconv;
	int index;
	GtkWidget *tab_cont;

	gtkwin = GAIM_GTK_WINDOW(win);

	index = gtk_notebook_get_current_page(GTK_NOTEBOOK(gtkwin->notebook));
	if (index == -1)
		index = 0;
	tab_cont = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gtkwin->notebook), index);
	if (!tab_cont)
		return NULL;
	gtkconv = g_object_get_data(G_OBJECT(tab_cont), "GaimGtkConversation");
	return gtkconv->active_conv;
}

static gboolean
gaim_gtk_has_focus(GaimConvWindow *win)
{
	GaimGtkWindow *gtkwin;
	gboolean has_focus = FALSE;

	gtkwin = GAIM_GTK_WINDOW(win);
	g_object_get(G_OBJECT(gtkwin->window), "has-toplevel-focus", &has_focus, NULL);

	return has_focus;
}

static GaimConvWindowUiOps window_ui_ops =
{
	gaim_gtk_conversations_get_conv_ui_ops,
	gaim_gtk_new_window,
	gaim_gtk_destroy_window,
	gaim_gtk_show,
	gaim_gtk_hide,
	gaim_gtk_raise,
	gaim_gtk_switch_conversation,
	gaim_gtk_add_conversation,
	gaim_gtk_remove_conversation,
	gaim_gtk_get_active_conversation,
	gaim_gtk_has_focus
};

GaimConvWindowUiOps *
gaim_gtk_conversations_get_win_ui_ops(void)
{
	return &window_ui_ops;
}

/**************************************************************************
 * Conversation UI operations
 **************************************************************************/
static void
gaim_gtkconv_destroy(GaimConversation *conv)
{
	GaimGtkConversation *gtkconv = GAIM_GTK_CONVERSATION(conv);

	gtkconv->convs = g_list_remove(gtkconv->convs, conv);
	/* Don't destroy ourselves until all our convos are gone */
	if (gtkconv->convs)
		return;
	
	/* If the "Save Conversation" or "Save Icon" dialogs are open then close them */
	gaim_request_close_with_handle(conv);

	gtk_widget_destroy(gtkconv->tab_cont);
	g_object_unref(gtkconv->tab_cont);

	if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM) {
		if (gtkconv->u.im->icon_timer != 0)
			g_source_remove(gtkconv->u.im->icon_timer);

		if (gtkconv->u.im->anim != NULL)
			g_object_unref(G_OBJECT(gtkconv->u.im->anim));

		g_free(gtkconv->u.im);
	}
	else if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT) {
		gaim_signals_disconnect_by_handle(gtkconv->u.chat);
		g_free(gtkconv->u.chat);
	}

	gtk_object_sink(GTK_OBJECT(gtkconv->tooltips));

	g_free(gtkconv);
}

static void
gaim_gtkconv_write_im(GaimConversation *conv, const char *who,
					  const char *message, GaimMessageFlags flags,
					  time_t mtime)
{
	GaimGtkConversation *gtkconv;
	GaimConvWindow *gaimwin;
	GaimGtkWindow *gtkwin;
	gboolean has_focus;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkconv->active_conv = conv;
	gaimwin = gaim_conversation_get_window(conv);
	gtkwin = GAIM_GTK_WINDOW(gaimwin);

	g_object_get(G_OBJECT(gtkwin->window), "has-toplevel-focus", &has_focus, NULL);

	/* Play a sound, if specified in prefs. */
	if (gtkconv->make_sound && !((gaim_conv_window_get_active_conversation(gaimwin) == conv) &&
		!gaim_prefs_get_bool("/gaim/gtk/sound/conv_focus") && has_focus)) {
		if (flags & GAIM_MESSAGE_RECV) {
			if (gtkconv->u.im->a_virgin &&
				gaim_prefs_get_bool("/gaim/gtk/sound/enabled/first_im_recv")) {

				gaim_sound_play_event(GAIM_SOUND_FIRST_RECEIVE);
			}
			else
				gaim_sound_play_event(GAIM_SOUND_RECEIVE);
		}
		else {
			gaim_sound_play_event(GAIM_SOUND_SEND);
		}
	}

	gtkconv->u.im->a_virgin = FALSE;

	gaim_conversation_write(conv, who, message, flags, mtime);
}

static void
gaim_gtkconv_write_chat(GaimConversation *conv, const char *who,
						const char *message, GaimMessageFlags flags, time_t mtime)
{
	GaimGtkConversation *gtkconv;
	GaimConvWindow *gaimwin;
	GaimGtkWindow *gtkwin;
	gboolean has_focus;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkconv->active_conv = conv;
	gaimwin = gaim_conversation_get_window(conv);
	gtkwin = GAIM_GTK_WINDOW(gaimwin);

	g_object_get(G_OBJECT(gtkwin->window), "has-toplevel-focus", &has_focus, NULL);

	/* Play a sound, if specified in prefs. */
	if (gtkconv->make_sound && !((gaim_conv_window_get_active_conversation(gaimwin) == conv) &&
		!gaim_prefs_get_bool("/gaim/gtk/sound/conv_focus") && has_focus) &&
		!(flags & GAIM_MESSAGE_DELAYED) &&
		!(flags & GAIM_MESSAGE_SYSTEM)) {

		if (!(flags & GAIM_MESSAGE_WHISPER) && (flags & GAIM_MESSAGE_SEND))
			gaim_sound_play_event(GAIM_SOUND_CHAT_YOU_SAY);
		else if (flags & GAIM_MESSAGE_RECV) {
			if ((flags & GAIM_MESSAGE_NICK) &&
				gaim_prefs_get_bool("/gaim/gtk/sound/enabled/nick_said")) {

				gaim_sound_play_event(GAIM_SOUND_CHAT_NICK);
			}
			else
				gaim_sound_play_event(GAIM_SOUND_CHAT_SAY);
		}
	}

	flags |= GAIM_MESSAGE_COLORIZE;

	gaim_conversation_write(conv, who, message, flags, mtime);
}

/* The callback for an event on a link tag. */
static gboolean buddytag_event(GtkTextTag *tag, GObject *imhtml,
		GdkEvent *event, GtkTextIter *arg2, gpointer data) {
	if (event->type == GDK_BUTTON_PRESS
			|| event->type == GDK_2BUTTON_PRESS) {
		GdkEventButton *btn_event = (GdkEventButton*) event;
		GaimConversation *conv = data;
		char *buddyname;

		/* strlen("BUDDY ") == 6 */
		g_return_val_if_fail((tag->name != NULL)
				&& (strlen(tag->name) > 6), FALSE);

		buddyname = (tag->name) + 6;

		if (btn_event->button == 2
				&& event->type == GDK_2BUTTON_PRESS) {
			chat_do_info(GAIM_GTK_CONVERSATION(conv), buddyname);

			return TRUE;
		} else if (btn_event->button == 3
				&& event->type == GDK_BUTTON_PRESS) {
			GtkTextIter start, end;

			/* we shouldn't display the popup
			 * if the user has selected something: */
			if (!gtk_text_buffer_get_selection_bounds(
						gtk_text_iter_get_buffer(arg2),
						&start, &end)) {
				GaimPluginProtocolInfo *prpl_info = NULL;
				GtkWidget *menu = NULL;
				GaimConnection *gc =
					gaim_conversation_get_gc(conv);


				if (gc != NULL)
					prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(
							gc->prpl);

				menu = create_chat_menu(conv, buddyname,
						prpl_info, gc);
				gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
						NULL, GTK_WIDGET(imhtml),
						btn_event->button,
						btn_event->time);

				/* Don't propagate the event any further */
				return TRUE;
			}
		}
	}

	return FALSE;
}

static GtkTextTag *get_buddy_tag(GaimConversation *conv, const char *who) {
	GaimGtkConversation *gtkconv = GAIM_GTK_CONVERSATION(conv);
	GtkTextTag *buddytag;
	/* strlen("BUDDY ") == 6 */
	gchar str[strlen(who) + 7];

	g_snprintf(str, sizeof(str), "BUDDY %s", who);
	str[sizeof(str)] = '\0';

	buddytag = gtk_text_tag_table_lookup(
			gtk_text_buffer_get_tag_table(
				GTK_IMHTML(gtkconv->imhtml)->text_buffer), str);

	if (buddytag == NULL) {
		buddytag = gtk_text_buffer_create_tag(
				GTK_IMHTML(gtkconv->imhtml)->text_buffer, str, NULL);

		g_signal_connect(G_OBJECT(buddytag), "event",
				G_CALLBACK(buddytag_event), conv);
	}

	return buddytag;
}

static void
gaim_gtkconv_write_conv(GaimConversation *conv, const char *name, const char *alias,
						const char *message, GaimMessageFlags flags,
						time_t mtime)
{
	GaimGtkConversation *gtkconv;
	GaimConvWindow *win;
	GaimConnection *gc;
	int gtk_font_options = 0;
	int max_scrollback_lines = gaim_prefs_get_int(
		"/gaim/gtk/conversations/scrollback_lines");
	int line_count;
	char buf2[BUF_LONG];
	char mdate[64];
	char color[10];
	char *str;
	char *with_font_tag;
	char *sml_attrib = NULL;
	size_t length = strlen(message) + 1;

	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkconv->active_conv = conv;
	gc = gaim_conversation_get_gc(conv);

	win = gaim_conversation_get_window(conv);

	line_count = gtk_text_buffer_get_line_count(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(
				gtkconv->imhtml)));

	/* If we're sitting at more than 100 lines more than the
	   max scrollback, trim down to max scrollback */
	if (max_scrollback_lines > 0
			&& line_count > (max_scrollback_lines + 100)) {
		GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(
			GTK_TEXT_VIEW(gtkconv->imhtml));
		GtkTextIter start, end;

		gtk_text_buffer_get_start_iter(text_buffer, &start);
		gtk_text_buffer_get_iter_at_line(text_buffer, &end,
			(line_count - max_scrollback_lines));
		gtk_imhtml_delete(GTK_IMHTML(gtkconv->imhtml), &start, &end);
	}

	if (gtk_text_buffer_get_char_count(gtk_text_view_get_buffer(GTK_TEXT_VIEW(gtkconv->imhtml))))
		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), "<BR>", 0);

	if(time(NULL) > mtime + 20*60) /* show date if older than 20 minutes */
		strftime(mdate, sizeof(mdate), "%Y-%m-%d %H:%M:%S", localtime(&mtime));
	else
		strftime(mdate, sizeof(mdate), "%H:%M:%S", localtime(&mtime));

	if(gc)
		sml_attrib = g_strdup_printf("sml=\"%s\"",
									 gaim_account_get_protocol_name(conv->account));

	gtk_font_options |= GTK_IMHTML_NO_COMMENTS;

	if (!gaim_prefs_get_bool("/gaim/gtk/conversations/show_incoming_formatting"))
		gtk_font_options |= GTK_IMHTML_NO_COLOURS | GTK_IMHTML_NO_FONTS | GTK_IMHTML_NO_SIZES;

	/* this is gonna crash one day, I can feel it. */
	if (GAIM_PLUGIN_PROTOCOL_INFO(gaim_find_prpl(gaim_account_get_protocol_id(conv->account)))->options &
	    OPT_PROTO_USE_POINTSIZE) {
		gtk_font_options |= GTK_IMHTML_USE_POINTSIZE;
	}

	/* TODO: These colors should not be hardcoded so log.c can use them */
	if (flags & GAIM_MESSAGE_SYSTEM) {
		g_snprintf(buf2, sizeof(buf2),
			   "<FONT %s><FONT SIZE=\"2\"><!--(%s) --></FONT><B>%s</B></FONT>",
			   sml_attrib ? sml_attrib : "", mdate, message);

		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), buf2, 0);

	} else if (flags & GAIM_MESSAGE_ERROR) {
		g_snprintf(buf2, sizeof(buf2),
			   "<FONT COLOR=\"#ff0000\"><FONT %s><FONT SIZE=\"2\"><!--(%s) --></FONT><B>%s</B></FONT></FONT>",
			   sml_attrib ? sml_attrib : "", mdate, message);

		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), buf2, 0);

	} else if (flags & GAIM_MESSAGE_NO_LOG) {
		g_snprintf(buf2, BUF_LONG,
			   "<B><FONT %s COLOR=\"#777777\">%s</FONT></B>",
			   sml_attrib ? sml_attrib : "", message);

		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), buf2, 0);
	} else if (flags & GAIM_MESSAGE_RAW) {
		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), message, 0);
	} else {
		char *new_message = g_memdup(message, length);
		char *alias_escaped = (alias ? g_markup_escape_text(alias, strlen(alias)) : g_strdup(""));
		/* The initial offset is to deal with
		 * escaped entities making the string longer */
		int tag_start_offset = alias ? (strlen(alias_escaped) - strlen(alias)) : 0;
		int tag_end_offset = 0;

		if (flags & GAIM_MESSAGE_WHISPER) {
			str = g_malloc(1024);

			/* If we're whispering, it's not an autoresponse. */
			if (gaim_message_meify(new_message, -1 )) {
				g_snprintf(str, 1024, "***%s", alias_escaped);
				strcpy(color, "#6C2585");
				tag_start_offset += 3;
			}
			else {
				g_snprintf(str, 1024, "*%s*:", alias_escaped);
				tag_start_offset += 1;
				tag_end_offset = 2;
				strcpy(color, "#00FF00");
			}
		}
		else {
			if (gaim_message_meify(new_message, -1)) {
				str = g_malloc(1024);

				if (flags & GAIM_MESSAGE_AUTO_RESP) {
					g_snprintf(str, 1024, "%s ***%s", AUTO_RESPONSE, alias_escaped);
					tag_start_offset += 4
						+ strlen(AUTO_RESPONSE);
				} else {
					g_snprintf(str, 1024, "***%s", alias_escaped);
					tag_start_offset += 3;
				}

				if (flags & GAIM_MESSAGE_NICK)
					strcpy(color, "#AF7F00");
				else
					strcpy(color, "#062585");
			}
			else {
				str = g_malloc(1024);
				if (flags & GAIM_MESSAGE_AUTO_RESP) {
					g_snprintf(str, 1024, "%s %s", alias_escaped, AUTO_RESPONSE);
					tag_start_offset += 1
						+ strlen(AUTO_RESPONSE);
				} else {
					g_snprintf(str, 1024, "%s:", alias_escaped);
					tag_end_offset = 1;
				}
				if (flags & GAIM_MESSAGE_NICK)
					strcpy(color, "#AF7F00");
				else if (flags & GAIM_MESSAGE_RECV) {
					if (flags & GAIM_MESSAGE_COLORIZE) {
						GdkColor *col = get_nick_color(gtkconv, alias);

						g_snprintf(color, sizeof(color), "#%02X%02X%02X",
							   col->red >> 8, col->green >> 8, col->blue >> 8);
					} else
						strcpy(color, RECV_COLOR);
				}
				else if (flags & GAIM_MESSAGE_SEND)
					strcpy(color, SEND_COLOR);
				else {
					gaim_debug_error("gtkconv", "message missing flags\n");
					strcpy(color, "#000000");
				}
			}
		}

		if(alias_escaped)
			g_free(alias_escaped);
		g_snprintf(buf2, BUF_LONG,
			   "<FONT COLOR=\"%s\" %s><FONT SIZE=\"2\"><!--(%s) --></FONT>"
			   "<B>%s</B></FONT> ",
			   color, sml_attrib ? sml_attrib : "", mdate, str);

		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml), buf2, 0);

		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT &&
		    !(flags & GAIM_MESSAGE_SEND)) {

			GtkTextIter start, end;
			GtkTextTag *buddytag = get_buddy_tag(conv, name);

			gtk_text_buffer_get_end_iter(
					GTK_IMHTML(gtkconv->imhtml)->text_buffer,
					&end);
			gtk_text_iter_backward_chars(&end,
					tag_end_offset + 1);

			gtk_text_buffer_get_end_iter(
					GTK_IMHTML(gtkconv->imhtml)->text_buffer,
					&start);
			gtk_text_iter_backward_chars(&start,
					strlen(str) + 1 - tag_start_offset);

			gtk_text_buffer_apply_tag(
					GTK_IMHTML(gtkconv->imhtml)->text_buffer,
					buddytag, &start, &end);
		}

		g_free(str);

		if(gc){
			char *pre = g_strdup_printf("<font %s>", sml_attrib ? sml_attrib : "");
			char *post = "</font>";
			int pre_len = strlen(pre);
			int post_len = strlen(post);

			with_font_tag = g_malloc(length + pre_len + post_len + 1);

			strcpy(with_font_tag, pre);
			memcpy(with_font_tag + pre_len, new_message, length);
			strcpy(with_font_tag + pre_len + length, post);

			length += pre_len + post_len;
			g_free(pre);
		}
		else
			with_font_tag = g_memdup(new_message, length);

		gtk_imhtml_append_text(GTK_IMHTML(gtkconv->imhtml),
							 with_font_tag, gtk_font_options);

		g_free(with_font_tag);
		g_free(new_message);
	}

	if(sml_attrib)
		g_free(sml_attrib);
}

static void
gaim_gtkconv_chat_add_users(GaimConversation *conv, GList *users, GList *aliases)
{
	GaimConvChat *chat;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GList *l;
	GList *ll;
	char tmp[BUF_LONG];
	int num_users;

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;

	num_users = g_list_length(gaim_conv_chat_get_users(chat));

	g_snprintf(tmp, sizeof(tmp),
			   ngettext("%d person in room", "%d people in room",
						num_users),
			   num_users);

	gtk_label_set_text(GTK_LABEL(gtkchat->count), tmp);

	l = users;
	ll = aliases;
	while (l != NULL && ll != NULL) {
		add_chat_buddy_common(conv, (const char *)l->data, (const char *)ll->data);
		l = l->next;
		ll = ll->next;
	}
}

static void
gaim_gtkconv_chat_rename_user(GaimConversation *conv, const char *old_name,
							  const char *new_name)
{
	GaimConvChat *chat;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	int f = 1;
	char *alias = NULL;

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		return;

	while (f != 0) {
		char *val;

		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &val, -1);

		if (!gaim_utf8_strcasecmp(old_name, val)) {
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_ALIAS_COLUMN, &alias, -1);
			gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
			g_free(val);
			break;
		}

		f = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);

		g_free(val);
	}

	if (!gaim_conv_chat_find_user(chat, old_name))
		return;

	g_return_if_fail(alias != NULL);

	add_chat_buddy_common(conv, new_name, alias);
	g_free(alias);
}

static void
gaim_gtkconv_chat_remove_user(GaimConversation *conv, const char *user)
{
	GaimConvChat *chat;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	char tmp[BUF_LONG];
	int num_users;
	int f = 1;

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;

	num_users = g_list_length(gaim_conv_chat_get_users(chat)) - 1;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		return;

	while (f != 0) {
		char *val;

		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &val, -1);

		if (!gaim_utf8_strcasecmp(user, val)) {
			gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
			g_free(val);
			break;
		}

		f = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);

		g_free(val);
	}

	if (!gaim_conv_chat_find_user(chat, user))
		return;

	g_snprintf(tmp, sizeof(tmp),
			ngettext("%d person in room", "%d people in room",
				num_users), num_users);

	gtk_label_set_text(GTK_LABEL(gtkchat->count), tmp);

	if (gtkconv->make_sound)
		gaim_sound_play_event(GAIM_SOUND_CHAT_LEAVE);
}

static void
gaim_gtkconv_chat_remove_users(GaimConversation *conv, GList *users)
{
	GaimConvChat *chat;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	GList *l;
	char tmp[BUF_LONG];
	int num_users;
	gboolean f;

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;

	num_users = g_list_length(gaim_conv_chat_get_users(chat)) -
	            g_list_length(users);

	for (l = users; l != NULL; l = l->next) {
		model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

		if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
			continue;

		do {
			char *val;

			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
							   CHAT_USERS_NAME_COLUMN, &val, -1);

			if (!gaim_utf8_strcasecmp((char *)l->data, val)) {
#if GTK_CHECK_VERSION(2,2,0)
				f = gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
#else
				gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
				f = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);
#endif
			}
			else
				f = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);

			g_free(val);
		} while (f);
	}

	g_snprintf(tmp, sizeof(tmp),
			   ngettext("%d person in room", "%d people in room",
						num_users), num_users);

	gtk_label_set_text(GTK_LABEL(gtkchat->count), tmp);
}

static void
gaim_gtkconv_chat_update_user(GaimConversation *conv, const char *user)
{
	GaimConvChat *chat;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GtkTreeIter iter;
	GtkTreeModel *model;
	int f = 1;
	char *alias = NULL;

	chat    = GAIM_CONV_CHAT(conv);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	gtkchat = gtkconv->u.chat;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(gtkchat->list));

	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		return;

	while (f != 0) {
		char *val;

		gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &val, -1);

		if (!gaim_utf8_strcasecmp(user, val)) {
			gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, CHAT_USERS_NAME_COLUMN, &alias, -1);
			gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
			g_free(val);
			break;
		}

		f = gtk_tree_model_iter_next(GTK_TREE_MODEL(model), &iter);

		g_free(val);
	}

	if (!gaim_conv_chat_find_user(chat, user))
		return;

	g_return_if_fail(alias != NULL);

	add_chat_buddy_common(conv, user, alias);
	g_free(alias);
}

static gboolean
gaim_gtkconv_has_focus(GaimConversation *conv)
{
	GaimConvWindow *win;
	GaimGtkWindow *gtkwin;
	gboolean has_focus;

	win = gaim_conversation_get_window(conv);
	gtkwin = GAIM_GTK_WINDOW(win);

	g_object_get(G_OBJECT(gtkwin->window), "has-toplevel-focus", &has_focus, NULL);

	return has_focus;
}

static gboolean
gaim_gtkconv_custom_smiley_add(GaimConversation *conv, const char *smile)
{
	GaimGtkConversation *gtkconv;
	GtkIMHtmlSmiley *smiley;
	GdkPixbufLoader *loader;
	const char *sml;

	if (conv == NULL || smile == NULL) {
		return FALSE;
	}

	sml = gaim_account_get_protocol_name(conv->account); /* XXX this sucks */
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	smiley = gtk_imhtml_smiley_get(GTK_IMHTML(gtkconv->imhtml), sml, smile);

	/* TODO: implement changing a custom smiley in the middle of a conversation */

	if (smiley) {
		return FALSE;
	}


	loader = gdk_pixbuf_loader_new();

	/* this is wrong, this file ought not call g_new on GtkIMHtmlSmiley */
	/* Let gtk_imhtml have a gtk_imhtml_smiley_new function, and let
	   GtkIMHtmlSmiley by opaque */
	smiley = g_new0(GtkIMHtmlSmiley, 1);
	smiley->file   = NULL;
	smiley->smile  = g_strdup(smile);
	smiley->loader = loader;

	smiley->icon = gdk_pixbuf_loader_get_animation(loader);
	if (smiley->icon)
		g_object_ref(G_OBJECT(smiley->icon));

	gtk_imhtml_associate_smiley(GTK_IMHTML(gtkconv->imhtml), sml, smiley);

	return TRUE;
}

static void
gaim_gtkconv_custom_smiley_write(GaimConversation *conv, const char *smile,
                                      const guchar *data, gsize size)
{
	GaimGtkConversation *gtkconv;
	GtkIMHtmlSmiley *smiley;
	GdkPixbufLoader *loader;
	const char *sml;

	sml = gaim_account_get_protocol_name(conv->account);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	smiley = gtk_imhtml_smiley_get(GTK_IMHTML(gtkconv->imhtml), sml, smile);

	if (!smiley)
		return;

	loader = smiley->loader;
	if (!loader)
		return;

	gdk_pixbuf_loader_write(loader, data, size, NULL);
}

static void
gaim_gtkconv_custom_smiley_close(GaimConversation *conv, const char *smile)
{
	GaimGtkConversation *gtkconv;
	GtkIMHtmlSmiley *smiley;
	GdkPixbufLoader *loader;
	const char *sml;

	g_return_if_fail(conv  != NULL);
	g_return_if_fail(smile != NULL);

	sml = gaim_account_get_protocol_name(conv->account);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	smiley = gtk_imhtml_smiley_get(GTK_IMHTML(gtkconv->imhtml), sml, smile);

	if (!smiley)
		return;

	loader = smiley->loader;

	if (!loader)
		return;

	gaim_debug_info("gtkconv", "About to close the smiley pixbuf\n");

	gdk_pixbuf_loader_close(loader, NULL);
	g_object_unref(G_OBJECT(loader));
	smiley->loader = NULL;
}


static void
gaim_gtkconv_updated(GaimConversation *conv, GaimConvUpdateType type)
{
	GaimConvWindow *win;
	GaimGtkWindow *gtkwin;
	GaimGtkConversation *gtkconv;
	GaimGtkChatPane *gtkchat;
	GaimConvChat *chat;

	g_return_if_fail(conv != NULL);

	win     = gaim_conversation_get_window(conv);
	gtkwin  = GAIM_GTK_WINDOW(win);
	gtkconv = GAIM_GTK_CONVERSATION(conv);
	conv = gtkconv->active_conv; /* Gross hack */

	if (type == GAIM_CONV_UPDATE_ACCOUNT)
	{
		gaim_conversation_autoset_title(conv);

		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
			gaim_gtkconv_update_buddy_icon(conv);

		gaim_gtkconv_update_buttons_by_protocol(conv);

		g_timeout_add(0, (GSourceFunc)update_send_as_selection, win);

		smiley_themeize(gtkconv->imhtml);

		update_tab_icon(conv);
	}
	else if (type == GAIM_CONV_UPDATE_TYPING ||
	         type == GAIM_CONV_UPDATE_UNSEEN ||
	         type == GAIM_CONV_UPDATE_TITLE)
	{
		char *title;
		GaimConvIm *im = NULL;
		GaimConnection *gc = gaim_conversation_get_gc(conv);
		char color[8];

		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
			im = GAIM_CONV_IM(conv);

		if (!gc || ((gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_CHAT)
		                && gaim_conv_chat_has_left(GAIM_CONV_CHAT(conv))))
			title = g_strdup_printf("(%s)", gaim_conversation_get_title(conv));

		else
			title = g_strdup(gaim_conversation_get_title(conv));

		*color = '\0';

		if (!GTK_WIDGET_REALIZED(gtkconv->tab_label))
			gtk_widget_realize(gtkconv->tab_label);

		if (im != NULL && gaim_conv_im_get_typing_state(im) == GAIM_TYPING)
		{
			strcpy(color, "#47A046");
		}
		else if (im != NULL && gaim_conv_im_get_typing_state(im) == GAIM_TYPED)
		{
			strcpy(color, "#D1940C");
		}
		else if (gaim_conversation_get_unseen(conv) == GAIM_UNSEEN_NICK)
		{
			strcpy(color, "#0D4E91");
		}
		else if (gaim_conversation_get_unseen(conv) == GAIM_UNSEEN_TEXT)
		{
			strcpy(color, "#DF421E");
		}
		else if (gaim_conversation_get_unseen(conv) == GAIM_UNSEEN_EVENT)
		{
			strcpy(color, "#868272");
		}

		if (*color != '\0')
		{
			char *html_title,*label;

			html_title = g_markup_escape_text(title, -1);

			label = g_strdup_printf("<span color=\"%s\">%s</span>",
			                        color, html_title);
			g_free(html_title);
			gtk_label_set_markup(GTK_LABEL(gtkconv->tab_label), label);
			g_free(label);
		}
		else
			gtk_label_set_text(GTK_LABEL(gtkconv->tab_label), title);

		if (conv == gaim_conv_window_get_active_conversation(win))
			update_typing_icon(gtkconv);

		if (type == GAIM_CONV_UPDATE_TITLE) {
			gtk_label_set_text(GTK_LABEL(gtkconv->menu_label), title);
			if (conv == gaim_conv_window_get_active_conversation(win))
				gtk_window_set_title(GTK_WINDOW(gtkwin->window), title);
		}

		g_free(title);
	}
	else if (type == GAIM_CONV_UPDATE_TOPIC)
	{
		const char *topic;
		chat = GAIM_CONV_CHAT(conv);
		gtkchat = gtkconv->u.chat;

		topic = gaim_conv_chat_get_topic(chat);

		gtk_entry_set_text(GTK_ENTRY(gtkchat->topic_text), topic ? topic : "");
		gtk_tooltips_set_tip(gtkconv->tooltips, gtkchat->topic_text,
		                     topic ? topic : "", NULL);
	}
	else if (type == GAIM_CONV_ACCOUNT_ONLINE ||
			 type == GAIM_CONV_ACCOUNT_OFFLINE)
	{
		gray_stuff_out(GAIM_GTK_CONVERSATION(gaim_conv_window_get_active_conversation(win)));
		generate_send_as_items(win, NULL);
		update_tab_icon(conv);
		gaim_conversation_autoset_title(conv);
	}
	else if (type == GAIM_CONV_UPDATE_AWAY)
	{
		update_tab_icon(conv);
	}
	else if (type == GAIM_CONV_UPDATE_ADD || type == GAIM_CONV_UPDATE_REMOVE ||
	         type == GAIM_CONV_UPDATE_CHATLEFT)
	{
		gaim_conversation_autoset_title(conv);
		gray_stuff_out(GAIM_GTK_CONVERSATION(conv));
	}
	else if (type == GAIM_CONV_UPDATE_ICON)
	{
		gaim_gtkconv_update_buddy_icon(conv);
	}
	else if (type == GAIM_CONV_UPDATE_FEATURES)
	{
		gray_stuff_out(GAIM_GTK_CONVERSATION(conv));
	}
}

static GaimConversationUiOps conversation_ui_ops =
{
	gaim_gtkconv_destroy,            /* destroy_conversation */
	gaim_gtkconv_write_chat,         /* write_chat           */
	gaim_gtkconv_write_im,           /* write_im             */
	gaim_gtkconv_write_conv,         /* write_conv           */
	gaim_gtkconv_chat_add_users,     /* chat_add_users       */
	gaim_gtkconv_chat_rename_user,   /* chat_rename_user     */
	gaim_gtkconv_chat_remove_user,   /* chat_remove_user     */
	gaim_gtkconv_chat_remove_users,  /* chat_remove_users    */
	gaim_gtkconv_chat_update_user,   /* chat_update_user     */
	NULL,                            /* update_progress      */
	gaim_gtkconv_has_focus,          /* has_focus            */
	gaim_gtkconv_custom_smiley_add,  /* custom_smiley_add */
	gaim_gtkconv_custom_smiley_write, /* custom_smiley_write */
	gaim_gtkconv_custom_smiley_close, /* custom_smiley_close */
	gaim_gtkconv_updated             /* updated              */
};

GaimConversationUiOps *
gaim_gtk_conversations_get_conv_ui_ops(void)
{
	return &conversation_ui_ops;
}

/**************************************************************************
 * Public conversation utility functions
 **************************************************************************/
void
gaim_gtkconv_update_buddy_icon(GaimConversation *conv)
{
	GaimGtkConversation *gtkconv;
	GaimGtkWindow *gtkwin = GAIM_GTK_WINDOW(gaim_conversation_get_window(conv));

	GdkPixbufLoader *loader;
	GdkPixbufAnimation *anim;
	GError *err = NULL;

	const void *data;
	size_t len;

	GdkPixbuf *buf;

	GtkWidget *event;
	GtkWidget *frame;
	GdkPixbuf *scale;
	GdkPixmap *pm;
	GdkBitmap *bm;
	int scale_width, scale_height;

	GaimAccount *account;
	GaimPluginProtocolInfo *prpl_info = NULL;

	GaimBuddyIcon *icon;

	g_return_if_fail(conv != NULL);
	g_return_if_fail(GAIM_IS_GTK_CONVERSATION(conv));
	g_return_if_fail(gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM);

	gtkconv = GAIM_GTK_CONVERSATION(conv);

	if (!gtkconv->u.im->show_icon)
		return;

	account = gaim_conversation_get_account(conv);
	if(account && account->gc)
		prpl_info = GAIM_PLUGIN_PROTOCOL_INFO(account->gc->prpl);

	/* Remove the current icon stuff */
	if (gtkconv->u.im->icon_container != NULL)
		gtk_widget_destroy(gtkconv->u.im->icon_container);
	gtkconv->u.im->icon_container = NULL;

	if (gtkconv->u.im->anim != NULL)
		g_object_unref(G_OBJECT(gtkconv->u.im->anim));

	gtkconv->u.im->anim = NULL;

	if (gtkconv->u.im->icon_timer != 0)
		g_source_remove(gtkconv->u.im->icon_timer);

	gtkconv->u.im->icon_timer = 0;

	if (gtkconv->u.im->iter != NULL)
		g_object_unref(G_OBJECT(gtkconv->u.im->iter));

	gtkconv->u.im->iter = NULL;

	if (!gaim_prefs_get_bool("/gaim/gtk/conversations/im/show_buddy_icons"))
		return;

	if (gaim_conversation_get_gc(conv) == NULL)
		return;

	icon = gaim_conv_im_get_icon(GAIM_CONV_IM(conv));

	if (icon == NULL)
		return;

	data = gaim_buddy_icon_get_data(icon, &len);

	loader = gdk_pixbuf_loader_new();
	gdk_pixbuf_loader_write(loader, data, len, NULL);
	anim = gdk_pixbuf_loader_get_animation(loader);
	if (anim)
		g_object_ref(G_OBJECT(anim));
	gdk_pixbuf_loader_close(loader, &err);
	g_object_unref(loader);

	if (!anim)
		return;
	gtkconv->u.im->anim = anim;

	if (err) {
		gaim_debug(GAIM_DEBUG_ERROR, "gtkconv",
				   "Buddy icon error: %s\n", err->message);
		g_error_free(err);
	}

	if (!gtkconv->u.im->anim)
		return;

	if (gdk_pixbuf_animation_is_static_image(gtkconv->u.im->anim)) {
		gtkconv->u.im->iter = NULL;
		buf = gdk_pixbuf_animation_get_static_image(gtkconv->u.im->anim);
	} else {
		gtkconv->u.im->iter =
			gdk_pixbuf_animation_get_iter(gtkconv->u.im->anim, NULL); /* LEAK */
		buf = gdk_pixbuf_animation_iter_get_pixbuf(gtkconv->u.im->iter);
		if (gtkconv->u.im->animate)
			start_anim(NULL, gtkconv);
	}

	gaim_gtk_buddy_icon_get_scale_size(buf, prpl_info ? &prpl_info->icon_spec :
			NULL, &scale_width, &scale_height);
	scale = gdk_pixbuf_scale_simple(buf,
				MAX(gdk_pixbuf_get_width(buf) * scale_width /
				    gdk_pixbuf_animation_get_width(gtkconv->u.im->anim), 1),
				MAX(gdk_pixbuf_get_height(buf) * scale_height /
				    gdk_pixbuf_animation_get_height(gtkconv->u.im->anim), 1),
				GDK_INTERP_BILINEAR);

	gdk_pixbuf_render_pixmap_and_mask(scale, &pm, &bm, 100);
	g_object_unref(G_OBJECT(scale));


	gtkconv->u.im->icon_container = gtk_vbox_new(FALSE, 0);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame),
							  (bm ? GTK_SHADOW_NONE : GTK_SHADOW_IN));
	gtk_box_pack_start(GTK_BOX(gtkconv->u.im->icon_container), frame,
					   FALSE, FALSE, 0);

	event = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(frame), event);
	g_signal_connect(G_OBJECT(event), "button-press-event",
					 G_CALLBACK(icon_menu), gtkconv);
	gtk_widget_show(event);

	gtkconv->u.im->icon = gtk_image_new_from_pixmap(pm, bm);
	gtk_widget_set_size_request(gtkconv->u.im->icon, scale_width, scale_height);
	gtk_container_add(GTK_CONTAINER(event), gtkconv->u.im->icon);
	gtk_widget_show(gtkconv->u.im->icon);

	g_object_unref(G_OBJECT(pm));

	if (bm)
		g_object_unref(G_OBJECT(bm));

	gtk_box_pack_start(GTK_BOX(gtkconv->lower_hbox),
			   gtkconv->u.im->icon_container, FALSE, FALSE, 0);

	gtk_widget_show(gtkconv->u.im->icon_container);
	gtk_widget_show(frame);

	/* The buddy icon code needs badly to be fixed. */
	buf = gdk_pixbuf_animation_get_static_image(gtkconv->u.im->anim);
	if(conv == gaim_conv_window_get_active_conversation(gaim_conversation_get_window(conv)))
		gtk_window_set_icon(GTK_WINDOW(gtkwin->window), buf);
}

void
gaim_gtkconv_update_buttons_by_protocol(GaimConversation *conv)
{
	GaimConvWindow *win;

	if (!GAIM_IS_GTK_CONVERSATION(conv))
		return;

	win = gaim_conversation_get_window(conv);

	if (win != NULL && gaim_conv_window_get_active_conversation(win) == conv)
		gray_stuff_out(GAIM_GTK_CONVERSATION(conv));
}

GaimConvWindow *
gaim_gtkwin_get_at_xy(int x, int y)
{
	GaimConvWindow *win = NULL;
	GaimGtkWindow *gtkwin;
	GdkWindow *gdkwin;
	GList *l;

	gdkwin = gdk_window_at_pointer(&x, &y);

	if (gdkwin)
		gdkwin = gdk_window_get_toplevel(gdkwin);

	for (l = gaim_get_windows(); l != NULL; l = l->next) {
		win = (GaimConvWindow *)l->data;

		if (!GAIM_IS_GTK_WINDOW(win))
			continue;

		gtkwin = GAIM_GTK_WINDOW(win);

		if (gdkwin == gtkwin->window->window)
			return win;
	}

	return NULL;
}

int
gaim_gtkconv_get_tab_at_xy(GaimConvWindow *win, int x, int y)
{
	GaimGtkWindow *gtkwin;
	gint nb_x, nb_y, x_rel, y_rel;
	GtkNotebook *notebook;
	GtkWidget *page, *tab;
	gint i, page_num = -1;
	gint count;
	gboolean horiz;

	if (!GAIM_IS_GTK_WINDOW(win))
		return -1;

	gtkwin = GAIM_GTK_WINDOW(win);
	notebook = GTK_NOTEBOOK(gtkwin->notebook);

	gdk_window_get_origin(gtkwin->notebook->window, &nb_x, &nb_y);
	x_rel = x - nb_x;
	y_rel = y - nb_y;

	horiz = (gtk_notebook_get_tab_pos(notebook) == GTK_POS_TOP ||
			gtk_notebook_get_tab_pos(notebook) == GTK_POS_BOTTOM);

	count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(notebook));

	for (i = 0; i < count; i++) {

		page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), i);
		tab = gtk_notebook_get_tab_label(GTK_NOTEBOOK(notebook), page);

		if (horiz) {
			if (x_rel >= tab->allocation.x - GAIM_HIG_BOX_SPACE &&
					x_rel <= tab->allocation.x + tab->allocation.width + GAIM_HIG_BOX_SPACE) {
				page_num = i;
				break;
			}
		} else {
			if (y_rel >= tab->allocation.y - GAIM_HIG_BOX_SPACE &&
					y_rel <= tab->allocation.y + tab->allocation.height + GAIM_HIG_BOX_SPACE) {
				page_num = i;
				break;
			}
		}
	}

	return page_num;
}

static void
close_on_tabs_pref_cb(const char *name, GaimPrefType type, gpointer value,
						gpointer data)
{
	GList *l;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;

	for (l = gaim_get_conversations(); l != NULL; l = l->next) {
		conv = (GaimConversation *)l->data;

		if (!GAIM_IS_GTK_CONVERSATION(conv))
			continue;

		gtkconv = GAIM_GTK_CONVERSATION(conv);

		if (value)
			gtk_widget_show(gtkconv->close);
		else
			gtk_widget_hide(gtkconv->close);
	}
}

static void
spellcheck_pref_cb(const char *name, GaimPrefType type, gpointer value,
				   gpointer data)
{
#ifdef USE_GTKSPELL
	GList *cl;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GtkSpell *spell;

	for (cl = gaim_get_conversations(); cl != NULL; cl = cl->next) {

		conv = (GaimConversation *)cl->data;

		if (!GAIM_IS_GTK_CONVERSATION(conv))
			continue;

		gtkconv = GAIM_GTK_CONVERSATION(conv);

		if (value)
			gaim_gtk_setup_gtkspell(GTK_TEXT_VIEW(gtkconv->entry));
		else {
			spell = gtkspell_get_from_text_view(GTK_TEXT_VIEW(gtkconv->entry));
			gtkspell_detach(spell);
		}
	}
#endif
}

static void
tab_side_pref_cb(const char *name, GaimPrefType type, gpointer value,
				 gpointer data)
{
	GList *l;
	GtkPositionType pos;
	GaimConvWindow *win;
	GaimGtkWindow *gtkwin;

	pos = GPOINTER_TO_INT(value);

	for (l = gaim_get_windows(); l != NULL; l = l->next) {
		win = (GaimConvWindow *)l->data;

		if (!GAIM_IS_GTK_WINDOW(win))
			continue;

		gtkwin = GAIM_GTK_WINDOW(win);

		gtk_notebook_set_tab_pos(GTK_NOTEBOOK(gtkwin->notebook), pos);
	}
}

static void
show_formatting_toolbar_pref_cb(const char *name, GaimPrefType type,
								gpointer value, gpointer data)
{
	GList *l;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GaimConvWindow *win;
	GaimGtkWindow *gtkwin;

	for (l = gaim_get_conversations(); l != NULL; l = l->next)
	{
		conv = (GaimConversation *)l->data;

		if (!GAIM_IS_GTK_CONVERSATION(conv))
			continue;

		gtkconv = GAIM_GTK_CONVERSATION(conv);
		win     = gaim_conversation_get_window(conv);
		gtkwin  = GAIM_GTK_WINDOW(win);

		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(gtkwin->menu.show_formatting_toolbar),
				(gboolean)GPOINTER_TO_INT(value));

		if ((gboolean)GPOINTER_TO_INT(value))
			gtk_widget_show(gtkconv->toolbar);
		else
			gtk_widget_hide(gtkconv->toolbar);
	}
}

static void
animate_buddy_icons_pref_cb(const char *name, GaimPrefType type,
							gpointer value, gpointer data)
{
	GList *l;
	GaimConversation *conv;
	GaimGtkConversation *gtkconv;
	GaimConvWindow *win;

	if (!gaim_prefs_get_bool("/gaim/gtk/conversations/im/show_buddy_icons"))
		return;

	/* Set the "animate" flag for each icon based on the new preference */
	for (l = gaim_get_ims(); l != NULL; l = l->next) {
		conv = (GaimConversation *)l->data;
		gtkconv = GAIM_GTK_CONVERSATION(conv);
		gtkconv->u.im->animate = GPOINTER_TO_INT(value);
	}

	/* Now either stop or start animation for the active conversation in each window */
	for (l = gaim_get_windows(); l != NULL; l = l->next) {
		win = (GaimConvWindow *)l->data;
		conv = gaim_conv_window_get_active_conversation(win);
		gaim_gtkconv_update_buddy_icon(conv);
	}
}

static void
show_buddy_icons_pref_cb(const char *name, GaimPrefType type, gpointer value,
						 gpointer data)
{
	GList *l;

	for (l = gaim_get_conversations(); l != NULL; l = l->next) {
		GaimConversation *conv = l->data;

		if (gaim_conversation_get_type(conv) == GAIM_CONV_TYPE_IM)
			gaim_conversation_foreach(gaim_gtkconv_update_buddy_icon);
	}
}

static void
conv_placement_pref_cb(const char *name, GaimPrefType type,
					   gpointer value, gpointer data)
{
	GaimConvPlacementFunc func;

	if (strcmp(name, "/gaim/gtk/conversations/placement"))
		return;

	func = gaim_conv_placement_get_fnc(value);

	if (func == NULL)
		return;

	gaim_conv_placement_set_current_func(func);
}

void *
gaim_gtk_conversations_get_handle(void)
{
	static int handle;

	return &handle;
}

void
gaim_gtk_conversations_init(void)
{
	void *handle = gaim_gtk_conversations_get_handle();

	/* Conversations */
	gaim_prefs_add_none("/gaim/gtk/conversations");
	gaim_prefs_add_bool("/gaim/gtk/conversations/close_on_tabs", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/send_formatting", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/send_bold", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/send_italic", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/send_underline", FALSE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/spellcheck", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/show_incoming_formatting", TRUE);

	gaim_prefs_add_bool("/gaim/gtk/conversations/show_formatting_toolbar", TRUE);
	gaim_prefs_add_bool("/gaim/gtk/conversations/passthrough_unknown_commands", FALSE);

	gaim_prefs_add_string("/gaim/gtk/conversations/placement", "last");
	gaim_prefs_add_int("/gaim/gtk/conversations/placement_number", 1);
	gaim_prefs_add_string("/gaim/gtk/conversations/bgcolor", "");
	gaim_prefs_add_string("/gaim/gtk/conversations/fgcolor", "");
	gaim_prefs_add_string("/gaim/gtk/conversations/font_face", "");
	gaim_prefs_add_int("/gaim/gtk/conversations/font_size", 3);
	gaim_prefs_add_bool("/gaim/gtk/conversations/tabs", TRUE);
	gaim_prefs_add_int("/gaim/gtk/conversations/tab_side", GTK_POS_TOP);
	gaim_prefs_add_int("/gaim/gtk/conversations/scrollback_lines", 4000);

	/* Conversations -> Chat */
	gaim_prefs_add_none("/gaim/gtk/conversations/chat");
	gaim_prefs_add_int("/gaim/gtk/conversations/chat/default_width", 410);
	gaim_prefs_add_int("/gaim/gtk/conversations/chat/default_height", 160);
	gaim_prefs_add_int("/gaim/gtk/conversations/chat/entry_height", 50);

	/* Conversations -> IM */
	gaim_prefs_add_none("/gaim/gtk/conversations/im");

	gaim_prefs_add_bool("/gaim/gtk/conversations/im/animate_buddy_icons", TRUE);

	gaim_prefs_add_int("/gaim/gtk/conversations/im/default_width", 410);
	gaim_prefs_add_int("/gaim/gtk/conversations/im/default_height", 160);
	gaim_prefs_add_int("/gaim/gtk/conversations/im/entry_height", 50);
	gaim_prefs_add_bool("/gaim/gtk/conversations/im/show_buddy_icons", TRUE);

	/* Connect callbacks. */
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/close_on_tabs",
								close_on_tabs_pref_cb, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/show_formatting_toolbar",
								show_formatting_toolbar_pref_cb, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/spellcheck",
								spellcheck_pref_cb, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/tab_side",
								tab_side_pref_cb, NULL);

	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/placement",
			conv_placement_pref_cb, NULL);
	gaim_prefs_trigger_callback("/gaim/gtk/conversations/placement");

	/* IM callbacks */
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/im/animate_buddy_icons",
								animate_buddy_icons_pref_cb, NULL);
	gaim_prefs_connect_callback(handle, "/gaim/gtk/conversations/im/show_buddy_icons",
								show_buddy_icons_pref_cb, NULL);


	/**********************************************************************
	 * Register signals
	 **********************************************************************/
	gaim_signal_register(handle, "conversation-dragging",
	                     gaim_marshal_VOID__POINTER_POINTER, NULL, 2,
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_CONV_WINDOW),
	                     gaim_value_new(GAIM_TYPE_SUBTYPE,
	                                    GAIM_SUBTYPE_CONV_WINDOW));

	/**********************************************************************
	 * Register commands
	 **********************************************************************/
	gaim_cmd_register("say", "S", GAIM_CMD_P_DEFAULT,
	                  GAIM_CMD_FLAG_CHAT | GAIM_CMD_FLAG_IM, NULL,
	                  say_command_cb, _("say &lt;message&gt;:  Send a message normally as if you weren't using a command."), NULL);
	gaim_cmd_register("me", "S", GAIM_CMD_P_DEFAULT,
	                  GAIM_CMD_FLAG_CHAT | GAIM_CMD_FLAG_IM, NULL,
	                  me_command_cb, _("me &lt;action&gt;:  Send an IRC style action to a buddy or chat."), NULL);
	gaim_cmd_register("debug", "w", GAIM_CMD_P_DEFAULT,
	                  GAIM_CMD_FLAG_CHAT | GAIM_CMD_FLAG_IM, NULL,
	                  debug_command_cb, _("debug &lt;option&gt;:  Send various debug information to the current conversation."), NULL);
	gaim_cmd_register("clear", "", GAIM_CMD_P_DEFAULT,
	                  GAIM_CMD_FLAG_CHAT | GAIM_CMD_FLAG_IM, NULL,
	                  clear_command_cb, _("clear: Clears the conversation scrollback."), NULL);
	gaim_cmd_register("help", "w", GAIM_CMD_P_DEFAULT,
	                  GAIM_CMD_FLAG_CHAT | GAIM_CMD_FLAG_IM | GAIM_CMD_FLAG_ALLOW_WRONG_ARGS, NULL,
	                  help_command_cb, _("help &lt;command&gt;:  Help on a specific command."), NULL);
}

void
gaim_gtk_conversations_uninit(void)
{
	gaim_prefs_disconnect_by_handle(gaim_gtk_conversations_get_handle());
	gaim_signals_unregister_by_instance(gaim_gtk_conversations_get_handle());
}
