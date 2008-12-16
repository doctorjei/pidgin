/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */
 
#include "internal.h"
#include "ibb.h"
#include "debug.h"
#include "xmlnode.h"

#define JABBER_IBB_SESSION_DEFAULT_BLOCK_SIZE 4096

static GHashTable *jabber_ibb_sessions = NULL;
static GList *open_handlers = NULL;

JabberIBBSession *
jabber_ibb_session_create(JabberStream *js, const gchar *sid, const gchar *who, 
	gpointer user_data)
{
	JabberIBBSession *sess = g_new0(JabberIBBSession, 1);
	sess->js = js;
	if (sid) {
		sess->sid = g_strdup(sid);
	} else {
		sess->sid = jabber_get_next_id(js);
	}
	sess->who = g_strdup(who);
	sess->block_size = JABBER_IBB_SESSION_DEFAULT_BLOCK_SIZE;
	sess->state = JABBER_IBB_SESSION_NOT_OPENED;
	sess->user_data = user_data;
	
	g_hash_table_insert(jabber_ibb_sessions, sess->sid, sess);
	
	return sess;
}

JabberIBBSession *
jabber_ibb_session_create_from_xmlnode(JabberStream *js, xmlnode *packet,
	gpointer user_data)
{
	JabberIBBSession *sess = NULL;
	xmlnode *open = xmlnode_get_child_with_namespace(packet, "open",
		XEP_0047_NAMESPACE);
	const gchar *sid = xmlnode_get_attrib(open, "sid");
	const gchar *block_size = xmlnode_get_attrib(open, "block-size");
	
	if (!open) {
		return NULL;
	}

	if (!sid || !block_size) {
		purple_debug_error("jabber", 
			"IBB session open tag requires sid and block-size attributes\n");
		g_free(sess);
		return NULL;
	}

	sess = jabber_ibb_session_create(js, sid,
			xmlnode_get_attrib(packet, "from"), user_data);
	sess->id = g_strdup(xmlnode_get_attrib(packet, "id"));
	sess->block_size = atoi(block_size);
	/* if we create a session from an incoming <open/> request, it means the
	  session is immediatly open... */
	sess->state = JABBER_IBB_SESSION_OPENED;
	
	return sess;
}

void
jabber_ibb_session_destroy(JabberIBBSession *sess)
{
	purple_debug_info("jabber", "IBB: destroying session %p %s\n", sess, 
		sess->sid);
	
	if (jabber_ibb_session_get_state(sess) == JABBER_IBB_SESSION_OPENED) {
		jabber_ibb_session_close(sess);
	}
	
	if (sess->last_iq_id) {
		purple_debug_info("jabber", "IBB: removing callback for <iq/> %s\n",
			sess->last_iq_id);
		jabber_iq_remove_callback_by_id(jabber_ibb_session_get_js(sess), 
			sess->last_iq_id);
		g_free(sess->last_iq_id);
		sess->last_iq_id = NULL;
	}
	
	g_hash_table_remove(jabber_ibb_sessions, sess->sid);
	g_free(sess->id);
	g_free(sess->sid);
	g_free(sess->who);
	g_free(sess);
}

const gchar *
jabber_ibb_session_get_sid(const JabberIBBSession *sess)
{
	return sess->sid;
}

JabberStream *
jabber_ibb_session_get_js(JabberIBBSession *sess)
{
	return sess->js;
}

const gchar *
jabber_ibb_session_get_who(const JabberIBBSession *sess)
{
	return sess->who;
}

guint16
jabber_ibb_session_get_send_seq(const JabberIBBSession *sess)
{
	return sess->send_seq;
}

guint16
jabber_ibb_session_get_recv_seq(const JabberIBBSession *sess)
{
	return sess->recv_seq;
}

JabberIBBSessionState 
jabber_ibb_session_get_state(const JabberIBBSession *sess)
{
	return sess->state;
}

gsize
jabber_ibb_session_get_block_size(const JabberIBBSession *sess)
{
	return sess->block_size;
}

void
jabber_ibb_session_set_block_size(JabberIBBSession *sess, gsize size)
{
	if (jabber_ibb_session_get_state(sess) == JABBER_IBB_SESSION_NOT_OPENED) {
		sess->block_size = size;
	} else {
		purple_debug_error("jabber", 
			"Can't set block size on an open IBB session\n");
	}
}

gpointer
jabber_ibb_session_get_user_data(JabberIBBSession *sess)
{
	return sess->user_data;
}

void
jabber_ibb_session_set_opened_callback(JabberIBBSession *sess,
	JabberIBBOpenedCallback *cb)
{
	sess->opened_cb = cb;
}
	
void 
jabber_ibb_session_set_data_sent_callback(JabberIBBSession *sess,
	JabberIBBSentCallback *cb)
{
	sess->data_sent_cb = cb;
}
	
void 
jabber_ibb_session_set_closed_callback(JabberIBBSession *sess,
	JabberIBBClosedCallback *cb)
{
	sess->closed_cb = cb;
}
	
void 
jabber_ibb_session_set_data_received_callback(JabberIBBSession *sess,
	JabberIBBDataCallback *cb)
{
	sess->data_received_cb = cb;
}

void 
jabber_ibb_session_set_error_callback(JabberIBBSession *sess, 
	JabberIBBErrorCallback *cb)
{
	sess->error_cb = cb;
}

static void
jabber_ibb_session_opened_cb(JabberStream *js, xmlnode *packet, gpointer data)
{
	JabberIBBSession *sess = (JabberIBBSession *) data;

	if (strcmp(xmlnode_get_attrib(packet, "type"), "error") == 0) {
		sess->state = JABBER_IBB_SESSION_ERROR;
	} else {
		sess->state = JABBER_IBB_SESSION_OPENED;
	}
		
	if (sess->opened_cb) {
		sess->opened_cb(sess);
	}
}

void
jabber_ibb_session_open(JabberIBBSession *sess)
{
	if (jabber_ibb_session_get_state(sess) != JABBER_IBB_SESSION_NOT_OPENED) {
		purple_debug_error("jabber", 
			"jabber_ibb_session called on an already open stream\n");
	} else {
		JabberIq *set = jabber_iq_new(sess->js, JABBER_IQ_SET);
		xmlnode *open = xmlnode_new("open");
		gchar block_size[10];
		
		xmlnode_set_attrib(set->node, "to", jabber_ibb_session_get_who(sess));
		xmlnode_set_namespace(open, XEP_0047_NAMESPACE);
		xmlnode_set_attrib(open, "sid", jabber_ibb_session_get_sid(sess));
		g_snprintf(block_size, sizeof(block_size), "%" G_GSIZE_FORMAT, 
			jabber_ibb_session_get_block_size(sess));
		xmlnode_set_attrib(open, "block-size", block_size);
		xmlnode_insert_child(set->node, open);
		
		jabber_iq_set_callback(set, jabber_ibb_session_opened_cb, sess);
		
		jabber_iq_send(set);
	}
}

void
jabber_ibb_session_close(JabberIBBSession *sess)
{
	JabberIBBSessionState state = jabber_ibb_session_get_state(sess);
	
	if (state != JABBER_IBB_SESSION_OPENED && state != JABBER_IBB_SESSION_ERROR) {
		purple_debug_error("jabber",
			"jabber_ibb_session_close called on a session that has not been"
			"opened\n");
	} else {
		JabberIq *set = jabber_iq_new(jabber_ibb_session_get_js(sess),
			JABBER_IQ_SET);
		xmlnode *close = xmlnode_new("close");
		
		xmlnode_set_attrib(set->node, "to", jabber_ibb_session_get_who(sess));
		xmlnode_set_namespace(close, XEP_0047_NAMESPACE);
		xmlnode_set_attrib(close, "sid", jabber_ibb_session_get_sid(sess));
		xmlnode_insert_child(set->node, close);
		jabber_iq_send(set);
		sess->state = JABBER_IBB_SESSION_CLOSED;
	}
}

void
jabber_ibb_session_accept(JabberIBBSession *sess)
{
	JabberIq *result = jabber_iq_new(jabber_ibb_session_get_js(sess),
		JABBER_IQ_RESULT);
	
	xmlnode_set_attrib(result->node, "to", jabber_ibb_session_get_who(sess));
	jabber_iq_set_id(result, sess->id);
	jabber_iq_send(result);
	sess->state = JABBER_IBB_SESSION_OPENED;
}

static void
jabber_ibb_session_send_acknowledge_cb(JabberStream *js, xmlnode *packet, gpointer data)
{
	JabberIBBSession *sess = (JabberIBBSession *) data;
	xmlnode *error = xmlnode_get_child(packet, "error");
	
	if (sess) {
		/* reset callback */
		if (sess->last_iq_id) {
			g_free(sess->last_iq_id);
			sess->last_iq_id = NULL;
		}
		
		if (error) {
			jabber_ibb_session_close(sess);
			sess->state = JABBER_IBB_SESSION_ERROR;
		
			if (sess->error_cb) {
				sess->error_cb(sess);
			}
		} else {
			if (sess->data_sent_cb) {
				sess->data_sent_cb(sess);
			}
		}
	} else {
		/* the session has gone away, it was probably cancelled */
		purple_debug_info("jabber", 
			"got response from send data, but IBB session is no longer active\n");
	}
}

void
jabber_ibb_session_send_data(JabberIBBSession *sess, gconstpointer data,
                             gsize size)
{
	JabberIBBSessionState state = jabber_ibb_session_get_state(sess);
	
	purple_debug_info("jabber", "sending data block of %" G_GSIZE_FORMAT " bytes on IBB stream\n",
		size);
	
	if (state != JABBER_IBB_SESSION_OPENED) {
		purple_debug_error("jabber", 
			"trying to send data on a non-open IBB session\n");
	} else if (size > jabber_ibb_session_get_block_size(sess)) {
		purple_debug_error("jabber", 
			"trying to send a too large packet in the IBB session\n");
	} else {
		JabberIq *set = jabber_iq_new(jabber_ibb_session_get_js(sess), 
			JABBER_IQ_SET);
		xmlnode *data_element = xmlnode_new("data");
		char *base64 = purple_base64_encode(data, size);
		char seq[10];
		g_snprintf(seq, sizeof(seq), "%u", jabber_ibb_session_get_send_seq(sess));
		
		xmlnode_set_attrib(set->node, "to", jabber_ibb_session_get_who(sess));
		xmlnode_set_namespace(data_element, XEP_0047_NAMESPACE);
		xmlnode_set_attrib(data_element, "sid", jabber_ibb_session_get_sid(sess));
		xmlnode_set_attrib(data_element, "seq", seq);
		xmlnode_insert_data(data_element, base64, -1);
		
		xmlnode_insert_child(set->node, data_element);
	
		purple_debug_info("jabber", 
			"IBB: setting send <iq/> callback for session %p %s\n", sess,
			sess->sid);
		jabber_iq_set_callback(set, jabber_ibb_session_send_acknowledge_cb, sess);
		sess->last_iq_id = g_strdup(xmlnode_get_attrib(set->node, "id"));
		purple_debug_info("jabber", "IBB: set sess->last_iq_id: %s\n",
			sess->last_iq_id);
		jabber_iq_send(set);
		
		g_free(base64);
		(sess->send_seq)++;
	}
}

static void
jabber_ibb_send_error_response(JabberStream *js, xmlnode *packet)
{
	JabberIq *result = jabber_iq_new(js, JABBER_IQ_ERROR);
	xmlnode *error = xmlnode_new("error");
	xmlnode *item_not_found = xmlnode_new("item-not-found");
	
	xmlnode_set_namespace(item_not_found, 
		"urn:ietf:params:xml:ns:xmpp-stanzas");
	xmlnode_set_attrib(error, "code", "440");
	xmlnode_set_attrib(error, "type", "cancel");
	jabber_iq_set_id(result, xmlnode_get_attrib(packet, "id"));
	xmlnode_set_attrib(result->node, "to", 
		xmlnode_get_attrib(packet, "from"));
	xmlnode_insert_child(error, item_not_found);
	xmlnode_insert_child(result->node, error);
	
	jabber_iq_send(result);
}

void
jabber_ibb_parse(JabberStream *js, xmlnode *packet)
{
	xmlnode *data = xmlnode_get_child_with_namespace(packet, "data",
		XEP_0047_NAMESPACE);
	xmlnode *close = xmlnode_get_child_with_namespace(packet, "close", 
		XEP_0047_NAMESPACE);
	xmlnode *open = xmlnode_get_child_with_namespace(packet, "open",
		XEP_0047_NAMESPACE);
	const gchar *sid = 
		data ? xmlnode_get_attrib(data, "sid") : 
			close ? xmlnode_get_attrib(close, "sid") : NULL;
	JabberIBBSession *sess = 
		sid ? g_hash_table_lookup(jabber_ibb_sessions, sid) : NULL;
	const gchar *who = xmlnode_get_attrib(packet, "from");
	
	if (sess) {
		
		if (strcmp(who, jabber_ibb_session_get_who(sess)) != 0) {
			/* the iq comes from a different JID than the remote JID of the
			  session, ignore it */
			purple_debug_error("jabber", 
				"Got IBB iq from wrong JID, ignoring\n");
		} else if (data) {
			const gchar *seq_attr = xmlnode_get_attrib(data, "seq");
			guint16 seq = 0;

			/* reject the data, and set the session in error if we get an
			  out-of-order packet */
			if (seq_attr && (seq = atoi(seq_attr)) &&
					seq == jabber_ibb_session_get_recv_seq(sess)) {
				/* sequence # is the expected... */
				JabberIq *result = jabber_iq_new(js, JABBER_IQ_RESULT);
				
				jabber_iq_set_id(result, xmlnode_get_attrib(packet, "id"));
				xmlnode_set_attrib(result->node, "to", 
					xmlnode_get_attrib(packet, "from"));
				
				if (sess->data_received_cb) {
					gchar *base64 = xmlnode_get_data(data);
					gsize size;
					gpointer rawdata = purple_base64_decode(base64, &size);
					
					g_free(base64);
					
					if (rawdata) {
						purple_debug_info("jabber", 
							"got %" G_GSIZE_FORMAT " bytes of data on IBB stream\n",
							size);
						if (size > jabber_ibb_session_get_block_size(sess)) {
							purple_debug_error("jabber",
								"IBB: received a too large packet\n");
							if (sess->error_cb)
								sess->error_cb(sess);
							g_free(rawdata);
							return;
						} else {
							purple_debug_info("jabber", 
								"calling IBB callback for received data\n");
							sess->data_received_cb(sess, rawdata, size);
						}
						g_free(rawdata);
					} else {
						purple_debug_error("jabber", 
							"IBB: invalid BASE64 data received\n");
						if (sess->error_cb)
							sess->error_cb(sess);
						return;

					}
				}
				
				(sess->recv_seq)++;
				jabber_iq_send(result);
				
			} else {
				purple_debug_error("jabber", 
					"Received an out-of-order/invalid IBB packet\n");
				sess->state = JABBER_IBB_SESSION_ERROR;
				
				if (sess->error_cb) {
					sess->error_cb(sess);
				}
			}
		} else if (close) {
			sess->state = JABBER_IBB_SESSION_CLOSED;
			purple_debug_info("jabber", "IBB: received close\n");
			
			if (sess->closed_cb) {
				purple_debug_info("jabber", "IBB: calling closed handler\n");
				sess->closed_cb(sess);
			}
		
		} else {
			/* this should never happen */
			purple_debug_error("jabber", "Received bogus iq for IBB session\n");
		}
	} else if (open) {
		JabberIq *result;
		const GList *iterator;
		
		/* run all open handlers registered until one returns true */
		for (iterator = open_handlers ; iterator ; 
			 iterator = g_list_next(iterator)) {
			JabberIBBOpenHandler *handler = iterator->data;

			if (handler(js, packet)) {
				result = jabber_iq_new(js, JABBER_IQ_RESULT);
				xmlnode_set_attrib(result->node, "to", 
					xmlnode_get_attrib(packet, "from"));
				jabber_iq_set_id(result, xmlnode_get_attrib(packet, "id"));
				jabber_iq_send(result);
				return;
			}
		}
		/* no open callback returned success, reject */
		jabber_ibb_send_error_response(js, packet);
	} else {
		/* send error reply */
		jabber_ibb_send_error_response(js, packet);
	}
}

void
jabber_ibb_register_open_handler(JabberIBBOpenHandler *cb)
{
	open_handlers = g_list_append(open_handlers, cb);
}

void
jabber_ibb_unregister_open_handler(JabberIBBOpenHandler *cb)
{
	open_handlers = g_list_remove(open_handlers, cb);
}

void
jabber_ibb_init(void)
{
	jabber_ibb_sessions = g_hash_table_new(g_str_hash, g_str_equal);
}

void
jabber_ibb_uninit(void)
{
	g_hash_table_destroy(jabber_ibb_sessions);
	g_list_free(open_handlers);
	jabber_ibb_sessions = NULL;
	open_handlers = NULL;
}

