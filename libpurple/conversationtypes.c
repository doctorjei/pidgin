/*
 * purple
 *
 * Purple is the legal property of its developers, whose names are too numerous
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include <glib/gi18n-lib.h>

#include "internal.h"
#include "conversationtypes.h"
#include "debug.h"
#include "enums.h"
#include "purpleprivate.h"

/**************************************************************************/
/* PurpleChatConversation                                                 */
/**************************************************************************/

/*
 * Data specific to Chats.
 */
typedef struct
{
	GList *ignored;     /* Ignored users.                            */
	char  *who;         /* The person who set the topic.             */
	char  *topic;       /* The topic.                                */
	int    id;          /* The chat ID.                              */
	char *nick;         /* Your nick in this chat.                   */
	gboolean left;      /* We left the chat and kept the window open */
	GHashTable *users;  /* Hash table of the users in the room.      */
} PurpleChatConversationPrivate;

/* Chat Property enums */
enum {
	CHAT_PROP_0,
	CHAT_PROP_TOPIC_WHO,
	CHAT_PROP_TOPIC,
	CHAT_PROP_ID,
	CHAT_PROP_NICK,
	CHAT_PROP_LEFT,
	CHAT_PROP_LAST
};

static GParamSpec *chat_properties[CHAT_PROP_LAST];

G_DEFINE_TYPE_WITH_PRIVATE(PurpleChatConversation, purple_chat_conversation,
		PURPLE_TYPE_CONVERSATION);

/**************************************************************************
 * Chat Conversation API
 **************************************************************************/
static guint
_purple_conversation_user_hash(gconstpointer data)
{
	const gchar *name = data;
	gchar *collated;
	guint hash;

	collated = g_utf8_collate_key(name, -1);
	hash     = g_str_hash(collated);
	g_free(collated);
	return hash;
}

static gboolean
_purple_conversation_user_equal(gconstpointer a, gconstpointer b)
{
	return !g_utf8_collate(a, b);
}

GList *
purple_chat_conversation_get_users(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return g_hash_table_get_values(priv->users);
}

guint
purple_chat_conversation_get_users_count(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), 0);

	priv = purple_chat_conversation_get_instance_private(chat);
	return g_hash_table_size(priv->users);
}

void
purple_chat_conversation_ignore(PurpleChatConversation *chat, const char *name)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));
	g_return_if_fail(name != NULL);

	priv = purple_chat_conversation_get_instance_private(chat);

	/* Make sure the user isn't already ignored. */
	if (purple_chat_conversation_is_ignored_user(chat, name))
		return;

	purple_chat_conversation_set_ignored(chat,
		g_list_append(priv->ignored, g_strdup(name)));
}

void
purple_chat_conversation_unignore(PurpleChatConversation *chat, const char *name)
{
	PurpleChatConversationPrivate *priv = NULL;
	GList *item;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));
	g_return_if_fail(name != NULL);

	priv = purple_chat_conversation_get_instance_private(chat);

	/* Make sure the user is actually ignored. */
	if (!purple_chat_conversation_is_ignored_user(chat, name))
		return;

	item = g_list_find(purple_chat_conversation_get_ignored(chat),
					   purple_chat_conversation_get_ignored_user(chat, name));
	g_free(item->data);

	purple_chat_conversation_set_ignored(chat,
		g_list_delete_link(priv->ignored, item));
}

GList *
purple_chat_conversation_set_ignored(PurpleChatConversation *chat, GList *ignored)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	priv->ignored = ignored;
	return ignored;
}

GList *
purple_chat_conversation_get_ignored(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->ignored;
}

const char *
purple_chat_conversation_get_ignored_user(PurpleChatConversation *chat, const char *user)
{
	GList *ignored;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);
	g_return_val_if_fail(user != NULL, NULL);

	for (ignored = purple_chat_conversation_get_ignored(chat);
		 ignored != NULL;
		 ignored = ignored->next) {

		const char *ign = (const char *)ignored->data;

		if (!purple_utf8_strcasecmp(user, ign) ||
			((*ign == '+' || *ign == '%') && !purple_utf8_strcasecmp(user, ign + 1)))
			return ign;

		if (*ign == '@') {
			ign++;

			if ((*ign == '+' && !purple_utf8_strcasecmp(user, ign + 1)) ||
				(*ign != '+' && !purple_utf8_strcasecmp(user, ign)))
				return ign;
		}
	}

	return NULL;
}

gboolean
purple_chat_conversation_is_ignored_user(PurpleChatConversation *chat, const char *user)
{
	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), FALSE);
	g_return_val_if_fail(user != NULL, FALSE);

	return (purple_chat_conversation_get_ignored_user(chat, user) != NULL);
}

void
purple_chat_conversation_set_topic(PurpleChatConversation *chat, const char *who, const char *topic)
{
	PurpleChatConversationPrivate *priv = NULL;
	GObject *obj;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	priv = purple_chat_conversation_get_instance_private(chat);

	g_free(priv->who);
	g_free(priv->topic);

	priv->who   = g_strdup(who);
	priv->topic = g_strdup(topic);

	obj = G_OBJECT(chat);
	g_object_freeze_notify(obj);
	g_object_notify_by_pspec(obj, chat_properties[CHAT_PROP_TOPIC_WHO]);
	g_object_notify_by_pspec(obj, chat_properties[CHAT_PROP_TOPIC]);
	g_object_thaw_notify(obj);

	purple_conversation_update(PURPLE_CONVERSATION(chat),
							 PURPLE_CONVERSATION_UPDATE_TOPIC);

	purple_signal_emit(purple_conversations_get_handle(), "chat-topic-changed",
					 chat, priv->who, priv->topic);
}

const char *
purple_chat_conversation_get_topic(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->topic;
}

const char *
purple_chat_conversation_get_topic_who(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->who;
}

void
purple_chat_conversation_set_id(PurpleChatConversation *chat, int id)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	priv = purple_chat_conversation_get_instance_private(chat);
	priv->id = id;

	g_object_notify_by_pspec(G_OBJECT(chat), chat_properties[CHAT_PROP_ID]);
}

int
purple_chat_conversation_get_id(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), -1);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->id;
}

static void
chat_conversation_write_message(PurpleConversation *conv, PurpleMessage *msg)
{
	PurpleChatConversationPrivate *priv = NULL;
	PurpleMessageFlags flags;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(conv));
	g_return_if_fail(msg != NULL);

	priv = purple_chat_conversation_get_instance_private(PURPLE_CHAT_CONVERSATION(conv));

	/* Don't display this if the person who wrote it is ignored. */
	if (purple_message_get_author(msg) && purple_chat_conversation_is_ignored_user(
		PURPLE_CHAT_CONVERSATION(conv), purple_message_get_author(msg)))
	{
		return;
	}

	flags = purple_message_get_flags(msg);
	if (flags & PURPLE_MESSAGE_RECV) {
		if (purple_utf8_has_word(purple_message_get_contents(msg), priv->nick)) {
			flags |= PURPLE_MESSAGE_NICK;
			purple_message_set_flags(msg, flags);
		}
	}

	_purple_conversation_write_common(conv, msg);
}

void
purple_chat_conversation_add_user(PurpleChatConversation *chat, const char *user,
						const char *extra_msg, PurpleChatUserFlags flags,
						gboolean new_arrival)
{
	GList *users = g_list_append(NULL, (char *)user);
	GList *extra_msgs = g_list_append(NULL, (char *)extra_msg);
	GList *flags2 = g_list_append(NULL, GINT_TO_POINTER(flags));

	purple_chat_conversation_add_users(chat, users, extra_msgs, flags2, new_arrival);

	g_list_free(users);
	g_list_free(extra_msgs);
	g_list_free(flags2);
}

void
purple_chat_conversation_add_users(PurpleChatConversation *chat, GList *users, GList *extra_msgs,
						 GList *flags, gboolean new_arrivals)
{
	PurpleConversation *conv;
	PurpleConversationUiOps *ops;
	PurpleChatUser *chatuser;
	PurpleChatConversationPrivate *priv;
	PurpleAccount *account;
	PurpleConnection *gc;
	PurpleProtocol *protocol;
	GList *ul, *fl;
	GList *cbuddies = NULL;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));
	g_return_if_fail(users != NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	conv = PURPLE_CONVERSATION(chat);
	ops  = purple_conversation_get_ui_ops(conv);

	account = purple_conversation_get_account(conv);
	gc = purple_conversation_get_connection(conv);
	g_return_if_fail(PURPLE_IS_CONNECTION(gc));
	protocol = purple_connection_get_protocol(gc);
	g_return_if_fail(PURPLE_IS_PROTOCOL(protocol));

	ul = users;
	fl = flags;
	while ((ul != NULL) && (fl != NULL)) {
		const char *user = (const char *)ul->data;
		const char *alias = user;
		gboolean quiet;
		PurpleChatUserFlags flag = GPOINTER_TO_INT(fl->data);
		const char *extra_msg = (extra_msgs ? extra_msgs->data : NULL);

		if(!(purple_protocol_get_options(protocol) & OPT_PROTO_UNIQUE_CHATNAME)) {
			if (purple_strequal(priv->nick, purple_normalize(account, user))) {
				const char *alias2 = purple_account_get_private_alias(account);
				if (alias2 != NULL)
					alias = alias2;
				else
				{
					const char *display_name = purple_connection_get_display_name(gc);
					if (display_name != NULL)
						alias = display_name;
				}
			} else {
				PurpleBuddy *buddy;
				if ((buddy = purple_blist_find_buddy(purple_connection_get_account(gc), user)) != NULL)
					alias = purple_buddy_get_contact_alias(buddy);
			}
		}

		quiet = GPOINTER_TO_INT(purple_signal_emit_return_1(purple_conversations_get_handle(),
						 "chat-user-joining", chat, user, flag)) ||
				purple_chat_conversation_is_ignored_user(chat, user);

		chatuser = purple_chat_user_new(chat, user, alias, flag);

		g_hash_table_replace(priv->users,
			g_strdup(purple_chat_user_get_name(chatuser)),
			chatuser);

		cbuddies = g_list_prepend(cbuddies, chatuser);

		if (!quiet && new_arrivals) {
			char *alias_esc = g_markup_escape_text(alias, -1);
			char *tmp;

			if (extra_msg == NULL)
				tmp = g_strdup_printf(_("%s entered the room."), alias_esc);
			else {
				char *extra_msg_esc = g_markup_escape_text(extra_msg, -1);
				tmp = g_strdup_printf(_("%s [<I>%s</I>] entered the room."),
				                      alias_esc, extra_msg_esc);
				g_free(extra_msg_esc);
			}
			g_free(alias_esc);

			purple_conversation_write_system_message(
				conv, tmp, PURPLE_MESSAGE_NO_LINKIFY);
			g_free(tmp);
		}

		purple_signal_emit(purple_conversations_get_handle(),
						 "chat-user-joined", chat, user, flag, new_arrivals);
		ul = ul->next;
		fl = fl->next;
		if (extra_msgs != NULL)
			extra_msgs = extra_msgs->next;
	}

	cbuddies = g_list_sort(cbuddies, (GCompareFunc)purple_chat_user_compare);

	if (ops != NULL && ops->chat_add_users != NULL)
		ops->chat_add_users(chat, cbuddies, new_arrivals);

	g_list_free(cbuddies);
}

void
purple_chat_conversation_rename_user(PurpleChatConversation *chat, const char *old_user,
						   const char *new_user)
{
	PurpleConversation *conv;
	PurpleConversationUiOps *ops;
	PurpleAccount *account;
	PurpleConnection *gc;
	PurpleProtocol *protocol;
	PurpleChatUser *cb;
	PurpleChatUserFlags flags;
	PurpleChatConversationPrivate *priv;
	const char *new_alias = new_user;
	char tmp[BUF_LONG];
	gboolean is_me = FALSE;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));
	g_return_if_fail(old_user != NULL);
	g_return_if_fail(new_user != NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	conv    = PURPLE_CONVERSATION(chat);
	ops     = purple_conversation_get_ui_ops(conv);
	account = purple_conversation_get_account(conv);

	gc = purple_conversation_get_connection(conv);
	g_return_if_fail(PURPLE_IS_CONNECTION(gc));
	protocol = purple_connection_get_protocol(gc);
	g_return_if_fail(PURPLE_IS_PROTOCOL(protocol));

	if (purple_strequal(priv->nick, purple_normalize(account, old_user))) {
		const char *alias;

		/* Note this for later. */
		is_me = TRUE;

		if(!(purple_protocol_get_options(protocol) & OPT_PROTO_UNIQUE_CHATNAME)) {
			alias = purple_account_get_private_alias(account);
			if (alias != NULL)
				new_alias = alias;
			else
			{
				const char *display_name = purple_connection_get_display_name(gc);
				if (display_name != NULL)
					new_alias = display_name;
			}
		}
	} else if (!(purple_protocol_get_options(protocol) & OPT_PROTO_UNIQUE_CHATNAME)) {
		PurpleBuddy *buddy;
		if ((buddy = purple_blist_find_buddy(purple_connection_get_account(gc), new_user)) != NULL)
			new_alias = purple_buddy_get_contact_alias(buddy);
	}

	flags = purple_chat_user_get_flags(purple_chat_conversation_find_user(chat, old_user));
	cb = purple_chat_user_new(chat, new_user, new_alias, flags);

	g_hash_table_replace(priv->users,
		g_strdup(purple_chat_user_get_name(cb)), cb);

	if (ops != NULL && ops->chat_rename_user != NULL)
		ops->chat_rename_user(chat, old_user, new_user, new_alias);

	cb = purple_chat_conversation_find_user(chat, old_user);

	if (cb)
		g_hash_table_remove(priv->users, purple_chat_user_get_name(cb));

	if (purple_chat_conversation_is_ignored_user(chat, old_user)) {
		purple_chat_conversation_unignore(chat, old_user);
		purple_chat_conversation_ignore(chat, new_user);
	}
	else if (purple_chat_conversation_is_ignored_user(chat, new_user))
		purple_chat_conversation_unignore(chat, new_user);

	if (is_me)
		purple_chat_conversation_set_nick(chat, new_user);

	if (purple_prefs_get_bool("/purple/conversations/chat/show_nick_change") &&
	    !purple_chat_conversation_is_ignored_user(chat, new_user)) {

		if (is_me) {
			char *escaped = g_markup_escape_text(new_user, -1);
			g_snprintf(tmp, sizeof(tmp),
					_("You are now known as %s"), escaped);
			g_free(escaped);
		} else {
			const char *old_alias = old_user;
			const char *new_alias = new_user;
			char *escaped;
			char *escaped2;

			if (!(purple_protocol_get_options(protocol) & OPT_PROTO_UNIQUE_CHATNAME)) {
				PurpleBuddy *buddy;

				if ((buddy = purple_blist_find_buddy(purple_connection_get_account(gc), old_user)) != NULL)
					old_alias = purple_buddy_get_contact_alias(buddy);
				if ((buddy = purple_blist_find_buddy(purple_connection_get_account(gc), new_user)) != NULL)
					new_alias = purple_buddy_get_contact_alias(buddy);
			}

			escaped = g_markup_escape_text(old_alias, -1);
			escaped2 = g_markup_escape_text(new_alias, -1);
			g_snprintf(tmp, sizeof(tmp),
					_("%s is now known as %s"), escaped, escaped2);
			g_free(escaped);
			g_free(escaped2);
		}

		purple_conversation_write_system_message(conv,
			tmp, PURPLE_MESSAGE_NO_LINKIFY);
	}
}

void
purple_chat_conversation_remove_user(PurpleChatConversation *chat, const char *user, const char *reason)
{
	GList *users = g_list_append(NULL, (char *)user);

	purple_chat_conversation_remove_users(chat, users, reason);

	g_list_free(users);
}

void
purple_chat_conversation_remove_users(PurpleChatConversation *chat, GList *users, const char *reason)
{
	PurpleConversation *conv;
	PurpleConnection *gc;
	PurpleProtocol *protocol;
	PurpleConversationUiOps *ops;
	PurpleChatUser *cb;
	PurpleChatConversationPrivate *priv;
	GList *l;
	gboolean quiet;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));
	g_return_if_fail(users != NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	conv = PURPLE_CONVERSATION(chat);

	gc = purple_conversation_get_connection(conv);
	g_return_if_fail(PURPLE_IS_CONNECTION(gc));
	protocol = purple_connection_get_protocol(gc);
	g_return_if_fail(PURPLE_IS_PROTOCOL(protocol));

	ops  = purple_conversation_get_ui_ops(conv);

	for (l = users; l != NULL; l = l->next) {
		const char *user = (const char *)l->data;
		quiet = GPOINTER_TO_INT(purple_signal_emit_return_1(purple_conversations_get_handle(),
					"chat-user-leaving", chat, user, reason)) |
				purple_chat_conversation_is_ignored_user(chat, user);

		cb = purple_chat_conversation_find_user(chat, user);

		if (cb) {
			g_hash_table_remove(priv->users,
				purple_chat_user_get_name(cb));
		}

		/* NOTE: Don't remove them from ignored in case they re-enter. */

		if (!quiet) {
			const char *alias = user;
			char *alias_esc;
			char *tmp;

			if (!(purple_protocol_get_options(protocol) & OPT_PROTO_UNIQUE_CHATNAME)) {
				PurpleBuddy *buddy;

				if ((buddy = purple_blist_find_buddy(purple_connection_get_account(gc), user)) != NULL)
					alias = purple_buddy_get_contact_alias(buddy);
			}

			alias_esc = g_markup_escape_text(alias, -1);

			if (reason == NULL || !*reason)
				tmp = g_strdup_printf(_("%s left the room."), alias_esc);
			else {
				char *reason_esc = g_markup_escape_text(reason, -1);
				tmp = g_strdup_printf(_("%s left the room (%s)."),
				                      alias_esc, reason_esc);
				g_free(reason_esc);
			}
			g_free(alias_esc);

			purple_conversation_write_system_message(conv,
				tmp, PURPLE_MESSAGE_NO_LINKIFY);
			g_free(tmp);
		}

		purple_signal_emit(purple_conversations_get_handle(), "chat-user-left",
						 conv, user, reason);
	}

	if (ops != NULL && ops->chat_remove_users != NULL)
		ops->chat_remove_users(chat, users);
}

void
purple_chat_conversation_clear_users(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;
	PurpleConversationUiOps *ops;
	GHashTableIter it;
	gchar *name;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	priv = purple_chat_conversation_get_instance_private(chat);
	ops = purple_conversation_get_ui_ops(PURPLE_CONVERSATION(chat));

	if (ops != NULL && ops->chat_remove_users != NULL) {
		GList *names = NULL;

		g_hash_table_iter_init(&it, priv->users);
		while (g_hash_table_iter_next(&it, (gpointer*)&name, NULL))
			names = g_list_prepend(names, name);

		ops->chat_remove_users(chat, names);
		g_list_free(names);
	}

	g_hash_table_iter_init(&it, priv->users);
	while (g_hash_table_iter_next(&it, (gpointer*)&name, NULL)) {
		purple_signal_emit(purple_conversations_get_handle(),
						 "chat-user-leaving", chat, name, NULL);
		purple_signal_emit(purple_conversations_get_handle(),
						 "chat-user-left", chat, name, NULL);
	}

	g_hash_table_remove_all(priv->users);
}

void purple_chat_conversation_set_nick(PurpleChatConversation *chat, const char *nick) {
	PurpleChatConversationPrivate *priv = NULL;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	priv = purple_chat_conversation_get_instance_private(chat);
	g_free(priv->nick);
	priv->nick = g_strdup(purple_normalize(
			purple_conversation_get_account(PURPLE_CONVERSATION(chat)), nick));

	g_object_notify_by_pspec(G_OBJECT(chat), chat_properties[CHAT_PROP_NICK]);
}

const char *purple_chat_conversation_get_nick(PurpleChatConversation *chat) {
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->nick;
}

static void
invite_user_to_chat(gpointer data, PurpleRequestFields *fields)
{
	PurpleConversation *conv;
	PurpleChatConversationPrivate *priv;
	const char *user, *message;

	conv = data;
	priv = purple_chat_conversation_get_instance_private(
			PURPLE_CHAT_CONVERSATION(conv));
	user = purple_request_fields_get_string(fields, "screenname");
	message = purple_request_fields_get_string(fields, "message");

	purple_serv_chat_invite(purple_conversation_get_connection(conv), priv->id, message, user);
}

void purple_chat_conversation_invite_user(PurpleChatConversation *chat, const char *user,
		const char *message, gboolean confirm)
{
	PurpleAccount *account;
	PurpleRequestFields *fields;
	PurpleRequestFieldGroup *group;
	PurpleRequestField *field;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	if (!user || !*user || !message || !*message)
		confirm = TRUE;

	account = purple_conversation_get_account(PURPLE_CONVERSATION(chat));

	if (!confirm) {
		purple_serv_chat_invite(purple_account_get_connection(account),
				purple_chat_conversation_get_id(chat), message, user);
		return;
	}

	fields = purple_request_fields_new();
	group = purple_request_field_group_new(_("Invite to chat"));
	purple_request_fields_add_group(fields, group);

	field = purple_request_field_string_new("screenname", _("Buddy"), user, FALSE);
	purple_request_field_group_add_field(group, field);
	purple_request_field_set_required(field, TRUE);
	purple_request_field_set_type_hint(field, "screenname");

	field = purple_request_field_string_new("message", _("Message"), message, FALSE);
	purple_request_field_group_add_field(group, field);

	purple_request_fields(chat, _("Invite to chat"), NULL,
			_("Please enter the name of the user you wish to invite, "
				"along with an optional invite message."),
			fields,
			_("Invite"), G_CALLBACK(invite_user_to_chat),
			_("Cancel"), NULL,
			purple_request_cpar_from_conversation(PURPLE_CONVERSATION(chat)),
			chat);
}

gboolean
purple_chat_conversation_has_user(PurpleChatConversation *chat, const char *user)
{
	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), FALSE);
	g_return_val_if_fail(user != NULL, FALSE);

	return (purple_chat_conversation_find_user(chat, user) != NULL);
}

void
purple_chat_conversation_leave(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat));

	priv = purple_chat_conversation_get_instance_private(chat);
	priv->left = TRUE;

	if (!g_object_get_data(G_OBJECT(chat), "is-finalizing"))
		g_object_notify_by_pspec(G_OBJECT(chat), chat_properties[CHAT_PROP_LEFT]);

	purple_conversation_update(PURPLE_CONVERSATION(chat), PURPLE_CONVERSATION_UPDATE_CHATLEFT);
}

gboolean
purple_chat_conversation_has_left(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), TRUE);

	priv = purple_chat_conversation_get_instance_private(chat);
	return priv->left;
}

static void
chat_conversation_cleanup_for_rejoin(PurpleChatConversation *chat)
{
	const char *disp;
	PurpleAccount *account;
	PurpleConnection *gc;
	PurpleConversation *conv = PURPLE_CONVERSATION(chat);
	PurpleChatConversationPrivate *priv =
			purple_chat_conversation_get_instance_private(chat);

	account = purple_conversation_get_account(conv);

	purple_conversation_close_logs(conv);
	purple_conversation_set_logging(conv, TRUE);

	gc = purple_account_get_connection(account);

	if ((disp = purple_connection_get_display_name(gc)) != NULL)
		purple_chat_conversation_set_nick(chat, disp);
	else
	{
		purple_chat_conversation_set_nick(chat,
								purple_account_get_username(account));
	}

	purple_chat_conversation_clear_users(chat);
	purple_chat_conversation_set_topic(chat, NULL, NULL);
	priv->left = FALSE;

	g_object_notify_by_pspec(G_OBJECT(chat), chat_properties[CHAT_PROP_LEFT]);

	purple_conversation_update(conv, PURPLE_CONVERSATION_UPDATE_CHATLEFT);
}

PurpleChatUser *
purple_chat_conversation_find_user(PurpleChatConversation *chat, const char *name)
{
	PurpleChatConversationPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CHAT_CONVERSATION(chat), NULL);
	g_return_val_if_fail(name != NULL, NULL);

	priv = purple_chat_conversation_get_instance_private(chat);
	return g_hash_table_lookup(priv->users, name);
}

/**************************************************************************
 * GObject code for chats
 **************************************************************************/

/* Set method for GObject properties */
static void
purple_chat_conversation_set_property(GObject *obj, guint param_id, const GValue *value,
		GParamSpec *pspec)
{
	PurpleChatConversation *chat = PURPLE_CHAT_CONVERSATION(obj);

	switch (param_id) {
		case CHAT_PROP_ID:
			purple_chat_conversation_set_id(chat, g_value_get_int(value));
			break;
		case CHAT_PROP_NICK:
			purple_chat_conversation_set_nick(chat, g_value_get_string(value));
			break;
		case CHAT_PROP_LEFT:
			{
				gboolean left = g_value_get_boolean(value);
				if (left)
					purple_chat_conversation_leave(chat);
			}
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
			break;
	}
}

/* Get method for GObject properties */
static void
purple_chat_conversation_get_property(GObject *obj, guint param_id, GValue *value,
		GParamSpec *pspec)
{
	PurpleChatConversation *chat = PURPLE_CHAT_CONVERSATION(obj);

	switch (param_id) {
		case CHAT_PROP_TOPIC_WHO:
			g_value_set_string(value, purple_chat_conversation_get_topic_who(chat));
			break;
		case CHAT_PROP_TOPIC:
			g_value_set_string(value, purple_chat_conversation_get_topic(chat));
			break;
		case CHAT_PROP_ID:
			g_value_set_int(value, purple_chat_conversation_get_id(chat));
			break;
		case CHAT_PROP_NICK:
			g_value_set_string(value, purple_chat_conversation_get_nick(chat));
			break;
		case CHAT_PROP_LEFT:
			g_value_set_boolean(value, purple_chat_conversation_has_left(chat));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, param_id, pspec);
			break;
	}
}

/* GObject initialization function */
static void purple_chat_conversation_init(PurpleChatConversation *chat)
{
	PurpleChatConversationPrivate *priv =
			purple_chat_conversation_get_instance_private(chat);

	priv->users = g_hash_table_new_full(_purple_conversation_user_hash,
		_purple_conversation_user_equal, g_free, g_object_unref);
}

/* Called when done constructing */
static void
purple_chat_conversation_constructed(GObject *object)
{
	PurpleChatConversation *chat = PURPLE_CHAT_CONVERSATION(object);
	PurpleAccount *account;
	const char *disp;

	G_OBJECT_CLASS(purple_chat_conversation_parent_class)->
			constructed(object);

	g_object_get(object, "account", &account, NULL);

	if ((disp = purple_connection_get_display_name(purple_account_get_connection(account))))
		purple_chat_conversation_set_nick(chat, disp);
	else
		purple_chat_conversation_set_nick(chat,
								purple_account_get_username(account));

	if (purple_prefs_get_bool("/purple/logging/log_chats"))
		purple_conversation_set_logging(PURPLE_CONVERSATION(chat), TRUE);

	g_object_unref(account);
}

/* GObject dispose function */
static void
purple_chat_conversation_dispose(GObject *object)
{
	PurpleChatConversationPrivate *priv =
			purple_chat_conversation_get_instance_private
					(PURPLE_CHAT_CONVERSATION(object));

	g_hash_table_remove_all(priv->users);

	G_OBJECT_CLASS(purple_chat_conversation_parent_class)->dispose(object);
}

/* GObject finalize function */
static void
purple_chat_conversation_finalize(GObject *object)
{
	PurpleChatConversation *chat = PURPLE_CHAT_CONVERSATION(object);
	PurpleConnection *gc = purple_conversation_get_connection(PURPLE_CONVERSATION(chat));
	PurpleChatConversationPrivate *priv =
			purple_chat_conversation_get_instance_private(chat);

	if (gc != NULL)
	{
		/* Still connected */
		int chat_id = purple_chat_conversation_get_id(chat);

		/*
		 * Close the window when the user tells us to, and let the protocol
		 * deal with the internals on it's own time. Don't do this if the
		 * protocol already knows it left the chat.
		 */
		if (!purple_chat_conversation_has_left(chat))
			purple_serv_chat_leave(gc, chat_id);

		/*
		 * If they didn't call purple_serv_got_chat_left by now, it's too late.
		 * So we better do it for them before we destroy the thing.
		 */
		if (!purple_chat_conversation_has_left(chat))
			purple_serv_got_chat_left(gc, chat_id);
	}

	g_hash_table_destroy(priv->users);
	priv->users = NULL;

	g_list_free_full(priv->ignored, g_free);
	priv->ignored = NULL;

	g_free(priv->who);
	g_free(priv->topic);
	g_free(priv->nick);

	priv->who = NULL;
	priv->topic = NULL;
	priv->nick = NULL;

	G_OBJECT_CLASS(purple_chat_conversation_parent_class)->finalize(object);
}

/* Class initializer function */
static void purple_chat_conversation_class_init(PurpleChatConversationClass *klass)
{
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);
	PurpleConversationClass *conv_class = PURPLE_CONVERSATION_CLASS(klass);

	obj_class->dispose = purple_chat_conversation_dispose;
	obj_class->finalize = purple_chat_conversation_finalize;
	obj_class->constructed = purple_chat_conversation_constructed;

	/* Setup properties */
	obj_class->get_property = purple_chat_conversation_get_property;
	obj_class->set_property = purple_chat_conversation_set_property;

	conv_class->write_message = chat_conversation_write_message;

	chat_properties[CHAT_PROP_TOPIC_WHO] = g_param_spec_string("topic-who",
				"Who set topic",
				"Who set the chat topic.", NULL,
				G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	chat_properties[CHAT_PROP_TOPIC] = g_param_spec_string("topic",
				"Topic",
				"Topic of the chat.", NULL,
				G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	chat_properties[CHAT_PROP_ID] = g_param_spec_int("chat-id",
				"Chat ID",
				"The ID of the chat.", G_MININT, G_MAXINT, 0,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	chat_properties[CHAT_PROP_NICK] = g_param_spec_string("nick",
				"Nickname",
				"The nickname of the user in a chat.", NULL,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	chat_properties[CHAT_PROP_LEFT] = g_param_spec_boolean("left",
				"Left the chat",
				"Whether the user has left the chat.", FALSE,
				G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties(obj_class, CHAT_PROP_LAST,
				chat_properties);
}

PurpleChatConversation *
purple_chat_conversation_new(PurpleAccount *account, const char *name)
{
	PurpleChatConversation *chat;
	PurpleConnection *gc;

	g_return_val_if_fail(PURPLE_IS_ACCOUNT(account), NULL);
	g_return_val_if_fail(name != NULL, NULL);

	/* Check if this conversation already exists. */
	if ((chat = purple_conversations_find_chat_with_account(name, account)) != NULL)
	{
		if (!purple_chat_conversation_has_left(chat)) {
			purple_debug_warning("conversationtypes", "Trying to create "
					"multiple chats (%s) with the same name is deprecated and "
					"will be removed in libpurple 3.0.0", name);
		} else {
			/*
			 * This hack is necessary because some protocols (MSN) have unnamed chats
			 * that all use the same name.  A PurpleConversation for one of those
			 * is only ever re-used if the user has left, so calls to
			 * purple_conversation_new need to fall-through to creating a new
			 * chat.
			 * TODO 3.0.0: Remove this workaround and mandate unique names.
			 */

			chat_conversation_cleanup_for_rejoin(chat);
			return chat;
		}
	}

	gc = purple_account_get_connection(account);
	g_return_val_if_fail(PURPLE_IS_CONNECTION(gc), NULL);

	chat = g_object_new(PURPLE_TYPE_CHAT_CONVERSATION,
			"account", account,
			"name",    name,
			"title",   name,
			NULL);

	return chat;
}
