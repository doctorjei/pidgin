/*
 * purple - Bonjour Protocol Plugin
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "internal.h"
#include "util.h"
#include "debug.h"
#include "notify.h"
#include "proxy.h"
#include "ft.h"
#include "buddy.h"
#include "bonjour.h"
#include "bonjour_ft.h"
#include "cipher.h"

static void
bonjour_bytestreams_init(PurpleXfer *xfer);
static void
bonjour_bytestreams_connect(PurpleXfer *xfer);
static void
bonjour_xfer_init(PurpleXfer *xfer);
static void
bonjour_xfer_receive(PurpleConnection *pc, const char *id, const char *from,
		     const int filesize, const char *filename, int option);
static void bonjour_free_xfer(PurpleXfer *xfer);

/* Look for specific xfer handle */
static unsigned int next_id = 0;

static void
xep_ft_si_reject(PurpleXfer *xfer, char *to)
{
	xmlnode *error_node = NULL;
	xmlnode *tmp_node = NULL;
	XepIq *iq = NULL;
	XepXfer *xf = NULL;

	if(!to || !xfer)
		return;
	xf = xfer->data;
	if(!xf)
		return;

	purple_debug_info("bonjour", "xep file transfer stream initialization error.\n");
	iq = xep_iq_new(xf->data, XEP_IQ_ERROR, to, xf->sid);
	if(iq == NULL)
		return;

	error_node = xmlnode_new_child(iq->node, "error");
	xmlnode_set_attrib(error_node, "code", "403");
	xmlnode_set_attrib(error_node, "type", "cancel");

	tmp_node = xmlnode_new_child(error_node, "forbidden");
	xmlnode_set_namespace(tmp_node, "urn:ietf:params:xml:ns:xmpp-stanzas");

	tmp_node = xmlnode_new_child(error_node, "text");
	xmlnode_set_namespace(tmp_node, "urn:ietf:params:xml:ns:xmpp-stanzas");
	xmlnode_insert_data(tmp_node, "Offer Declined", -1);

	xep_iq_send_and_free(iq);
}

static void bonjour_xfer_cancel_send(PurpleXfer *xfer)
{
	purple_debug_info("bonjour", "Bonjour-xfer-cancel-send.\n");
	bonjour_free_xfer(xfer);
}

static void bonjour_xfer_request_denied(PurpleXfer *xfer)
{
	purple_debug_info("bonjour", "Bonjour-xfer-request-denied.\n");
	xep_ft_si_reject(xfer, xfer->who);
	bonjour_free_xfer(xfer);
}

static void bonjour_xfer_cancel_recv(PurpleXfer *xfer)
{
	purple_debug_info("bonjour", "Bonjour-xfer-cancel-recv.\n");
	bonjour_free_xfer(xfer);
}


static void bonjour_xfer_end(PurpleXfer *xfer)
{
	purple_debug_info("bonjour", "Bonjour-xfer-end.\n");
	bonjour_free_xfer(xfer);
}

static PurpleXfer*
bonjour_si_xfer_find(BonjourData *bd, const char *sid, const char *from)
{
	GList *xfers = NULL;
	PurpleXfer *xfer = NULL;
	XepXfer *xf = NULL;

	if(!sid || !from || !bd)
		return NULL;

	purple_debug_info("bonjour", "Look for sid=%s from=%s xferlists.\n",
			  sid, from);

	for(xfers = bd->xfer_lists; xfers; xfers = xfers->next) {
		xfer = xfers->data;
		if(xfer == NULL)
			break;
		xf = xfer->data;
		if(xf == NULL)
			break;
		if(xf->sid && xfer->who && !strcmp(xf->sid, sid) &&
				!strcmp(xfer->who, from))
			return xfer;
	}

	purple_debug_info("bonjour", "Look for xfer list fail\n");

	return NULL;
}

static void
xep_ft_si_offer(PurpleXfer *xfer, const gchar *to)
{
	xmlnode *si_node = NULL;
	xmlnode *feature = NULL;
	xmlnode *field = NULL;
	xmlnode *option = NULL;
	xmlnode *value = NULL;
	xmlnode *file = NULL;
	xmlnode *x = NULL;
	XepIq *iq = NULL;
	XepXfer *xf = NULL;
	BonjourData *bd = NULL;
	char buf[32];

	xf = xfer->data;
	if(!xf)
		return;

	bd = xf->data;
	if(!bd)
		return;

	purple_debug_info("bonjour", "xep file transfer stream initialization offer-id=%d.\n", next_id);

	/* Assign stream id. */
	memset(buf, 0, 32);
	g_snprintf(buf, sizeof(buf), "%u", next_id++);
	iq = xep_iq_new(xf->data, XEP_IQ_SET, to, buf);
	if(iq == NULL)
		return;

	g_free(xf->sid);
	xf->sid = g_strdup(buf);
	/*Construct Stream initialization offer message.*/
	si_node = xmlnode_new_child(iq->node, "si");
	xmlnode_set_namespace(si_node, "http://jabber.org/protocol/si");

	file = xmlnode_new_child(si_node, "file");
	xmlnode_set_namespace(file, "http://jabber.org/protocol/si/profile/file-transfer");
	xmlnode_set_attrib(file, "name", xfer->filename);
	memset(buf, 0, 32);
	g_snprintf(buf, sizeof(buf), "%" G_GSIZE_FORMAT, xfer->size);
	xmlnode_set_attrib(file, "size", buf);

	feature = xmlnode_new_child(si_node, "feature");
	xmlnode_set_namespace(feature, "http://jabber.org/protocol/feature-neg");

	x = xmlnode_new_child(feature, "x");
	xmlnode_set_namespace(x, "jabber:x:data");
	xmlnode_set_attrib(x, "type", "form");

	field = xmlnode_new_child(x, "field");
	xmlnode_set_attrib(field, "var", "stream-method");
	xmlnode_set_attrib(field, "type", "list-single");

	if (xf->mode & XEP_BYTESTREAMS) {
		option = xmlnode_new_child(field, "option");
		value = xmlnode_new_child(option, "value");
		xmlnode_insert_data(value, "http://jabber.org/protocol/bytestreams", -1);
	}
	if (xf->mode & XEP_IBB) {
		option = xmlnode_new_child(field, "option");
		value = xmlnode_new_child(option, "value");
		xmlnode_insert_data(value, "http://jabber.org/protocol/ibb", -1);
	}

	xep_iq_send_and_free(iq);
}

static void
xep_ft_si_reject2(BonjourData *bd, const char *to, const char *sid)
{
	xmlnode *error_node = NULL;
	xmlnode *tmp_node = NULL;
	XepIq *iq = NULL;

	g_return_if_fail(bd != NULL);

	if(!to || !sid)
		return;

	purple_debug_info("bonjour", "xep file transfer stream initialization error.\n");
	iq = xep_iq_new(bd, XEP_IQ_ERROR, to, sid);
	if(iq == NULL)
		return;

	error_node = xmlnode_new_child(iq->node, "error");
	xmlnode_set_attrib(error_node, "code", "403");
	xmlnode_set_attrib(error_node, "type", "cancel");

	tmp_node = xmlnode_new_child(error_node, "forbidden");
	xmlnode_set_namespace(tmp_node, "urn:ietf:params:xml:ns:xmpp-stanzas");

	tmp_node = xmlnode_new_child(error_node, "text");
	xmlnode_set_namespace(tmp_node, "urn:ietf:params:xml:ns:xmpp-stanzas");
	xmlnode_insert_data(tmp_node, "Offer Declined", -1);

	xep_iq_send_and_free(iq);
}

static void
xep_ft_si_result(PurpleXfer *xfer, char *to)
{
	xmlnode *si_node = NULL;
	xmlnode *feature = NULL;
	xmlnode *field = NULL;
	xmlnode *value = NULL;
	xmlnode *x = NULL;
	XepIq *iq = NULL;
	XepXfer *xf = NULL;

	if(!to || !xfer)
		return;
	xf = xfer->data;
	if(!xf)
		return;

	purple_debug_info("bonjour", "xep file transfer stream initialization result.\n");
	iq = xep_iq_new(xf->data, XEP_IQ_RESULT, to, xf->sid);
	if(iq == NULL)
		return;

	si_node = xmlnode_new_child(iq->node, "si");
	xmlnode_set_namespace(si_node, "http://jabber.org/protocol/si");

	feature = xmlnode_new_child(si_node, "feature");
	xmlnode_set_namespace(feature, "http://jabber.org/protocol/feature-neg");

	x = xmlnode_new_child(feature, "x");
	xmlnode_set_namespace(x, "jabber:x:data");
	xmlnode_set_attrib(x, "type", "submit");

	field = xmlnode_new_child(x, "field");
	xmlnode_set_attrib(field, "var", "stream-method");

	value = xmlnode_new_child(field, "value");
	xmlnode_insert_data(value, "http://jabber.org/protocol/bytestreams", -1);

	xep_iq_send_and_free(iq);
}

static void
bonjour_free_xfer(PurpleXfer *xfer)
{
	XepXfer *xf = NULL;
	BonjourData *bd = NULL;

	if(xfer == NULL) {
		purple_debug_info("bonjour", "bonjour-free-xfer-null.\n");
		return;
	}

	purple_debug_info("bonjour", "bonjour-free-xfer-%p.\n", xfer);

	xf = (XepXfer*)xfer->data;
	if(xf != NULL) {
		bd = (BonjourData*)xf->data;
		if(bd != NULL) {
			bd->xfer_lists = g_list_remove(bd->xfer_lists, xfer);
			purple_debug_info("bonjour", "B free xfer from lists(%p).\n", bd->xfer_lists);
		}
		if (xf->proxy_connection != NULL)
			purple_proxy_connect_cancel(xf->proxy_connection);
		if (xf->listen_data != NULL)
			purple_network_listen_cancel(xf->listen_data);
		g_free(xf->jid);
		g_free(xf->proxy_host);
		g_free(xf->buddy_ip);
		g_free(xf->sid);
		g_free(xf);
		xfer->data = NULL;
	}

	purple_debug_info("bonjour", "Need close socket=%d.\n", xfer->fd);
}

PurpleXfer *
bonjour_new_xfer(PurpleConnection *gc, const char *who)
{
	PurpleXfer *xfer;
	XepXfer *xep_xfer = NULL;
	BonjourData *bd = NULL;

	if(who == NULL || gc == NULL)
		return NULL;

	purple_debug_info("bonjour", "Bonjour-new-xfer to %s.\n", who);
	bd = (BonjourData*) gc->proto_data;
	if(bd == NULL)
		return NULL;

	/* Build the file transfer handle */
	xfer = purple_xfer_new(gc->account, PURPLE_XFER_SEND, who);
	xfer->data = xep_xfer = g_new0(XepXfer, 1);
	xep_xfer->data = bd;

	purple_debug_info("bonjour", "Bonjour-new-xfer bd=%p data=%p.\n", bd, xep_xfer->data);

	xep_xfer->mode = XEP_BYTESTREAMS | XEP_IBB;
	xep_xfer->sid = NULL;

	purple_xfer_set_init_fnc(xfer, bonjour_xfer_init);
	purple_xfer_set_cancel_send_fnc(xfer, bonjour_xfer_cancel_send);
	purple_xfer_set_end_fnc(xfer, bonjour_xfer_end);

	bd->xfer_lists = g_list_append(bd->xfer_lists, xfer);

	return xfer;
}

void
bonjour_send_file(PurpleConnection *gc, const char *who, const char *file)
{

	PurpleXfer *xfer = NULL;
	if(gc == NULL || who == NULL)
		return;
	purple_debug_info("bonjour", "Bonjour-send-file to=%s.\n", who);
	xfer = bonjour_new_xfer(gc, who);
	if(xfer == NULL)
		return;
	if (file)
		purple_xfer_request_accepted(xfer, file);
	else
		purple_xfer_request(xfer);

}

static void
bonjour_xfer_init(PurpleXfer *xfer)
{
	PurpleBuddy *buddy = NULL;
	BonjourBuddy *bd = NULL;
	XepXfer *xf = NULL;

	if(xfer == NULL)
		return;
	xf = (XepXfer*)xfer->data;
	if(xf == NULL)
		return;
	purple_debug_info("bonjour", "Bonjour-xfer-init.\n");

	buddy = purple_find_buddy(xfer->account, xfer->who);
	/* this buddy is offline. */
	if (buddy == NULL)
		return;

	bd = (BonjourBuddy *)buddy->proto_data;
	xf->buddy_ip = g_strdup(bd->ip);
	if (purple_xfer_get_type(xfer) == PURPLE_XFER_SEND) {
		/* initiate file transfer, send SI offer. */
		purple_debug_info("bonjour", "Bonjour xfer type is PURPLE_XFER_SEND.\n");
		xep_ft_si_offer(xfer, xfer->who);

	} else {
		/* accept file transfer request, send SI result. */
		xep_ft_si_result(xfer, xfer->who);
		purple_debug_info("bonjour", "Bonjour xfer type is PURPLE_XFER_RECEIVE.\n");
	}
	return;
}


void
xep_si_parse(PurpleConnection *pc, xmlnode *packet, PurpleBuddy *pb)
{
	const char *type = NULL, *from = NULL, *id = NULL;
	xmlnode *si = NULL, *file = NULL;
	BonjourData *bd = NULL;
	PurpleXfer *xfer = NULL;
	const char *filename = NULL, *filesize_str = NULL;
	int filesize = 0, option = XEP_BYTESTREAMS;

	if(pc == NULL || packet == NULL || pb == NULL)
		return;
	bd = (BonjourData*) pc->proto_data;
	if(bd == NULL)
		return;

	purple_debug_info("bonjour", "xep-si-parse.\n");

	type = xmlnode_get_attrib(packet, "type");
	from = pb->name;
	id = xmlnode_get_attrib(packet, "id");
	if(type) {
		if(!strcmp(type, "set")){
			si = xmlnode_get_child(packet,"si");
			purple_debug_info("bonjour", "si offer Message type - SET.\n");
			file = xmlnode_get_child(si, "file");
			/**/
			filename = xmlnode_get_attrib(file, "name");
			if((filesize_str = xmlnode_get_attrib(file, "size")))
				filesize = atoi(filesize_str);
			bonjour_xfer_receive(pc, id, from, filesize, filename, option);
		} else if(!strcmp(type, "result")){
			si = xmlnode_get_child(packet,"si");
			purple_debug_info("bonjour", "si offer Message type - RESULT.\n");
			xfer = bonjour_si_xfer_find(bd, id, from);
			if(xfer == NULL){
				purple_debug_info("bonjour", "xfer find fail.\n");
				xep_ft_si_reject2((BonjourData *)pc->proto_data, from, id);
			} else {
				bonjour_bytestreams_init(xfer);
			}
		} else if(!strcmp(type, "error")){
			purple_debug_info("bonjour", "si offer Message type - ERROR.\n");
			xfer = bonjour_si_xfer_find(bd, id, from);
			if(xfer == NULL){
				purple_debug_info("bonjour", "xfer find fail.\n");
			} else {
				purple_xfer_cancel_remote(xfer);
			}
		} else {
			purple_debug_info("bonjour", "si offer Message type - Unknown-%d.\n", type);
		}
	}
}

void
xep_bytestreams_parse(PurpleConnection *pc, xmlnode *packet, PurpleBuddy *pb)
{
	const char *type = NULL, *from = NULL, *id = NULL;
	xmlnode *query = NULL, *streamhost = NULL;
	BonjourData *bd = NULL;
	PurpleXfer *xfer = NULL;
	XepXfer *xf = NULL;
	const char *jid=NULL, *host=NULL, *port=NULL;
	int portnum;

	if(pc == NULL || packet == NULL || pb == NULL)
		return;

	bd = (BonjourData*) pc->proto_data;
	if(bd == NULL)
		return;

	purple_debug_info("bonjour", "xep-bytestreams-parse.\n");

	type = xmlnode_get_attrib(packet, "type");
	from = pb->name;
	query = xmlnode_get_child(packet,"query");
	if(type) {
		if(!strcmp(type, "set")){
			purple_debug_info("bonjour", "bytestream offer Message type - SET.\n");

			id = xmlnode_get_attrib(query, "sid");
			xfer = bonjour_si_xfer_find(bd, id, from);

			if(xfer){
				xf = (XepXfer*)xfer->data;
				for(streamhost = xmlnode_get_child(query, "streamhost");
						streamhost;
						streamhost = xmlnode_get_next_twin(streamhost)) {

					if((jid = xmlnode_get_attrib(streamhost, "jid")) &&
					   (host = xmlnode_get_attrib(streamhost, "host")) &&
					   (port = xmlnode_get_attrib(streamhost, "port")) &&
					   (portnum = atoi(port))) {

						if(!strcmp(host, xf->buddy_ip)) {
							xf->jid = g_strdup(jid);
							xf->proxy_host = g_strdup(host);
							xf->proxy_port = portnum;
							purple_debug_info("bonjour", "bytestream offer parse"
									  "jid=%s host=%s port=%d.\n", jid, host, portnum);
							bonjour_bytestreams_connect(xfer);
							break;
						}

					} else {
						purple_debug_info("bonjour", "bytestream offer Message parse error.\n");
					}
				}
			} else {

			}

		} else {
			purple_debug_info("bonjour", "bytestream offer Message type - Unknown-%d.\n", type);
		}
	}
}

static void
bonjour_xfer_receive(PurpleConnection *pc, const char *id, const char *from,
		     const int filesize, const char *filename, int option)
{
	PurpleXfer *xfer = NULL;
	XepXfer *xf = NULL;
	BonjourData *bd = NULL;

	if(pc == NULL || id == NULL || from == NULL || filename == NULL)
		return;

	bd = (BonjourData*) pc->proto_data;
	if(bd == NULL)
		return;

	purple_debug_info("bonjour", "bonjour-xfer-receive.\n");

	/* Build the file transfer handle */
	xfer = purple_xfer_new(pc->account, PURPLE_XFER_RECEIVE, from);
	xfer->data = xf = g_new0(XepXfer, 1);
	xf->data = bd;
	purple_xfer_set_filename(xfer, filename);
	xf->sid = g_strdup(id);

	if(filesize > 0)
		purple_xfer_set_size(xfer, filesize);
	purple_xfer_set_init_fnc(xfer, bonjour_xfer_init);
	purple_xfer_set_request_denied_fnc(xfer, bonjour_xfer_request_denied);
	purple_xfer_set_cancel_recv_fnc(xfer, bonjour_xfer_cancel_recv);
	purple_xfer_set_end_fnc(xfer, bonjour_xfer_end);

	bd->xfer_lists = g_list_append(bd->xfer_lists, xfer);

	purple_xfer_request(xfer);
}

static void
bonjour_sock5_request_cb(gpointer data, gint source, PurpleInputCondition cond)
{
	PurpleXfer *xfer = data;
	XepXfer *xf = NULL;
	int acceptfd;
	int len = 0;

	if(xfer == NULL)
		return;

	xf = xfer->data;
	if(xf == NULL)
		return;

	switch(xf->sock5_req_state){
	case 0x00:
		acceptfd = accept(source, NULL, 0);
		if(acceptfd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {

		} else if(acceptfd == -1) {

		} else {
			purple_debug_info("bonjour", "Conjour-sock5-request-cb. state= %d, accept=%d\n", xf->sock5_req_state, acceptfd);
			purple_input_remove(xfer->watcher);
			close(source);
			xfer->watcher = purple_input_add(acceptfd, PURPLE_INPUT_READ,
							 bonjour_sock5_request_cb, xfer);
			xf->sock5_req_state++;
			xf->rxlen = 0;
		}
		break;
	case 0x01:
		xfer->fd = source;
		len = read(source, xf->rx_buf + xf->rxlen, 3);
		if(len < 0 && errno == EAGAIN)
			return;
		else if(len <= 0){
			purple_input_remove(xfer->watcher);
			xfer->watcher = 0;
			close(source);
			purple_xfer_cancel_remote(xfer);
			return;
		} else {
			purple_input_remove(xfer->watcher);
			xfer->watcher = purple_input_add(source, PURPLE_INPUT_WRITE,
							 bonjour_sock5_request_cb, xfer);
			xf->sock5_req_state++;
			xf->rxlen = 0;
			bonjour_sock5_request_cb(xfer, source, PURPLE_INPUT_WRITE);
		}
		break;
	case 0x02:
		xf->tx_buf[0] = 0x05;
		xf->tx_buf[1] = 0x00;
		len = write(source, xf->tx_buf, 2);
		if (len < 0 && errno == EAGAIN)
			return;
		else if (len < 0) {
			purple_input_remove(xfer->watcher);
			xfer->watcher = 0;
			close(source);
			purple_xfer_cancel_remote(xfer);
			return;
		} else {
			purple_input_remove(xfer->watcher);
			xfer->watcher = purple_input_add(source, PURPLE_INPUT_READ,
							 bonjour_sock5_request_cb, xfer);
			xf->sock5_req_state++;
			xf->rxlen = 0;
		}
		break;
	case 0x03:
		len = read(source, xf->rx_buf + xf->rxlen, 20);
		if(len<=0){
		} else {
			purple_input_remove(xfer->watcher);
			xfer->watcher = purple_input_add(source, PURPLE_INPUT_WRITE,
							 bonjour_sock5_request_cb, xfer);
			xf->sock5_req_state++;
			xf->rxlen = 0;
			bonjour_sock5_request_cb(xfer, source, PURPLE_INPUT_WRITE);
		}
		break;
	case 0x04:
		xf->tx_buf[0] = 0x05;
		xf->tx_buf[1] = 0x00;
		xf->tx_buf[2] = 0x00;
		xf->tx_buf[3] = 0x03;
		xf->tx_buf[4] = strlen(xf->buddy_ip);
		memcpy(xf->tx_buf + 5, xf->buddy_ip, strlen(xf->buddy_ip));
		xf->tx_buf[5+strlen(xf->buddy_ip)] = 0x00;
		xf->tx_buf[6+strlen(xf->buddy_ip)] = 0x00;
		len = write(source, xf->tx_buf, 7 + strlen(xf->buddy_ip));
		if (len < 0 && errno == EAGAIN) {
			return;
		} else if (len < 0) {
			purple_input_remove(xfer->watcher);
			xfer->watcher = 0;
			close(source);
			purple_xfer_cancel_remote(xfer);
			return;
		} else {
			purple_input_remove(xfer->watcher);
			xfer->watcher = 0;
			xf->rxlen = 0;
			/*close(source);*/
			purple_xfer_start(xfer, source, NULL, -1);
		}
		break;
	default:
		break;
	}
	return;
}

static void
bonjour_bytestreams_listen(int sock, gpointer data)
{
	PurpleXfer *xfer = data;
	XepXfer *xf;
	XepIq *iq;
	xmlnode *query, *streamhost;
	char *port;
	const char *next_ip;
	const char *local_ip = NULL;
	char token [] = ";";

	purple_debug_info("bonjour", "Bonjour-bytestreams-listen. sock=%d.\n", sock);
	if (sock < 0 || xfer == NULL) {
		/*purple_xfer_cancel_local(xfer);*/
		return;
	}

	xfer->watcher = purple_input_add(sock, PURPLE_INPUT_READ,
					 bonjour_sock5_request_cb, xfer);
	xf = (XepXfer*)xfer->data;
	xf->listen_data = NULL;

	iq = xep_iq_new(xf->data, XEP_IQ_SET, xfer->who, xf->sid);

	query = xmlnode_new_child(iq->node, "query");
	xmlnode_set_namespace(query, "http://jabber.org/protocol/bytestreams");
	xmlnode_set_attrib(query, "sid", xf->sid);
	xmlnode_set_attrib(query, "mode", "tcp");

	xfer->local_port = purple_network_get_port_from_fd(sock);

	local_ip = purple_network_get_my_ip_ext2(sock);
	/* cheat a little here - the intent of the "const" attribute is to make it clear that the string doesn't need to be freed */
	next_ip = strtok((char *)local_ip, token);

	while(next_ip != NULL) {
		streamhost = xmlnode_new_child(query, "streamhost");
		xmlnode_set_attrib(streamhost, "jid", xf->sid);
		xmlnode_set_attrib(streamhost, "host", next_ip);
		port = g_strdup_printf("%hu", xfer->local_port);
		xmlnode_set_attrib(streamhost, "port", port);
		g_free(port);
		next_ip = strtok(NULL, token);
	}

	xep_iq_send_and_free(iq);
}

static void
bonjour_bytestreams_init(PurpleXfer *xfer)
{
	XepXfer *xf = NULL;
	if(xfer == NULL)
		return;
	purple_debug_info("bonjour", "Bonjour-bytestreams-init.\n");
	xf = xfer->data;
	purple_network_listen_map_external(FALSE);
	xf->listen_data = purple_network_listen_range(0, 0, SOCK_STREAM,
						      bonjour_bytestreams_listen, xfer);
	purple_network_listen_map_external(TRUE);
	if (xf->listen_data == NULL) {
		purple_xfer_cancel_local(xfer);
	}
	return;
}

static void
bonjour_bytestreams_connect_cb(gpointer data, gint source, const gchar *error_message)
{
	PurpleXfer *xfer = data;
	XepXfer *xf = xfer->data;

	if(data == NULL || source < 0)
		return;

	purple_proxy_info_destroy(xf->proxy_info);
	xf->proxy_connection = NULL;
	xf->proxy_info = NULL;
	/* Here, start the file transfer.*/
	purple_xfer_start(xfer, source, NULL, -1);
}

static void
bonjour_bytestreams_connect(PurpleXfer *xfer)
{
	XepXfer *xf = NULL;
	char dstaddr[41];
	unsigned char hashval[20];
	char *p = NULL;
	int i;

	if(xfer == NULL)
		return;

	purple_debug_info("bonjour", "bonjour-bytestreams-connect.\n");

	xf = (XepXfer*)xfer->data;
	if(!xf)
		return;

	p = g_strdup_printf("%s@%s", xf->sid, xfer->who);
	purple_cipher_digest_region("sha1", (guchar *)p, strlen(p),
				    sizeof(hashval), hashval, NULL);
	g_free(p);

	memset(dstaddr, 0, 41);
	p = dstaddr;
	for(i = 0; i < 20; i++, p += 2)
		snprintf(p, 3, "%02x", hashval[i]);

	xf->proxy_info = purple_proxy_info_new();
	purple_proxy_info_set_type(xf->proxy_info, PURPLE_PROXY_SOCKS5);
	purple_proxy_info_set_host(xf->proxy_info, xf->proxy_host);
	purple_proxy_info_set_port(xf->proxy_info, xf->proxy_port);
	xf->proxy_connection = purple_proxy_connect_socks5(NULL, xf->proxy_info,
							   dstaddr, 0,
							   bonjour_bytestreams_connect_cb, xfer);

	if(xf->proxy_connection == NULL) {
		purple_proxy_info_destroy(xf->proxy_info);
		xf->proxy_info = NULL;
	}
}

