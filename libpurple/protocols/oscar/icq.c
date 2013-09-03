/* purple
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
 *
 */

#include "icq.h"

#include "core.h"
#include "plugins.h"
#include "signals.h"

#include "oscarcommon.h"

static GHashTable *
icq_get_account_text_table(PurpleAccount *account)
{
	GHashTable *table;
	table = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(table, "login_label", (gpointer)_("ICQ UIN..."));
	return table;
}

static gssize
icq_get_max_message_size(PurpleConversation *conv)
{
	/* XXX: got from pidgin-otr - verify and document it */
	return 2346;
}

static void
icq_protocol_base_init(ICQProtocolClass *klass)
{
	PurpleProtocolClass *proto_class = PURPLE_PROTOCOL_CLASS(klass);
	PurpleAccountOption *option;

	proto_class->id        = "icq";
	proto_class->name      = "ICQ";

	oscar_init_protocol_options(proto_class);

	option = purple_account_option_string_new(_("Server"), "server", oscar_get_login_server(TRUE, TRUE));
	proto_class->protocol_options = g_list_append(proto_class->protocol_options, option);

	option = purple_account_option_string_new(_("Encoding"), "encoding", OSCAR_DEFAULT_CUSTOM_ENCODING);
	proto_class->protocol_options = g_list_append(proto_class->protocol_options, option);
}

static void
icq_protocol_interface_init(PurpleProtocolInterface *iface)
{
	iface->list_icon              = oscar_list_icon_icq;
	iface->get_account_text_table = icq_get_account_text_table;
	iface->get_moods              = oscar_get_purple_moods;
	iface->get_max_message_size   = icq_get_max_message_size;
}

static void icq_protocol_base_finalize(ICQProtocolClass *klass) { }

extern PurplePlugin *_oscar_plugin;
PURPLE_PROTOCOL_DEFINE_EXTENDED(_oscar_plugin, ICQProtocol, icq_protocol,
                                OSCAR_TYPE_PROTOCOL, 0);
