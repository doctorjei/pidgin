/*
 *
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

#include "internal.h"
#include "tls-certificate.h"
#include "debug.h"
#include "util.h"

/* Makes a filename path for a certificate. If id is NULL,
 * just return the directory
 */
static gchar *
make_certificate_path(const gchar *id)
{
	return g_build_filename(purple_user_dir(),
				"certificates", "tls",
				id != NULL ? purple_escape_filename(id) : NULL,
				NULL);
}

/* Creates the certificate directory if it doesn't exist,
 * returns TRUE if it's successful or it already exists,
 * returns FALSE if there was an error.
 */
static gboolean
ensure_certificate_dir(GError **error)
{
	gchar *dir = make_certificate_path(NULL);
	gboolean ret = TRUE;

	if (purple_build_dir(dir, 0700) != 0) {
		g_set_error_literal(error, G_FILE_ERROR,
				g_file_error_from_errno(errno),
				g_strerror(errno));
		ret = FALSE;
	}

	g_free(dir);
	return ret;
}

GList *
purple_tls_certificate_list_ids()
{
	gchar *dir_path;
	GDir *dir;
	const gchar *entry;
	GList *idlist = NULL;
	GError *error = NULL;

	/* Ensure certificate directory exists */

	if (!ensure_certificate_dir(&error)) {
		purple_debug_error("tls-certificate",
				"Error creating certificate directory: %s",
				error->message);
		g_clear_error(&error);
		return NULL;
	}

	/* Open certificate directory */

	dir_path = make_certificate_path(NULL);
	dir = g_dir_open(dir_path, 0, &error);

	if (dir == NULL) {
		purple_debug_error("tls-certificate",
				"Error opening certificate directory (%s): %s",
				dir_path, error->message);
		g_free(dir_path);
		g_clear_error(&error);
		return NULL;
	}

	g_free(dir_path);

	/* Traverse the directory listing and create an idlist */

	while ((entry = g_dir_read_name(dir)) != NULL) {
		/* Unescape the filename
		 * (GLib owns original string)
		 */
		const char *unescaped = purple_unescape_filename(entry);

		/* Copy the entry name into our list
		 * (Purple own the escaped string)
		 */
		idlist = g_list_prepend(idlist, g_strdup(unescaped));
	}

	g_dir_close(dir);

	return idlist;
}

void
purple_tls_certificate_free_ids(GList *ids)
{
	g_list_free_full(ids, g_free);
}

GTlsCertificate *
purple_tls_certificate_new_from_id(const gchar *id, GError **error)
{
	GTlsCertificate *cert;
	gchar *path;

	g_return_val_if_fail(id != NULL && id[0] != '\0', NULL);

	/* Load certificate from file if it exists */

	path = make_certificate_path(id);
	cert = g_tls_certificate_new_from_file(path, error);
	g_free(path);

	return cert;
}

gboolean
purple_tls_certificate_trust(const gchar *id, GTlsCertificate *certificate,
		GError **error)
{
	gchar *path;
	gchar *pem = NULL;
	gboolean ret;

	g_return_val_if_fail(id != NULL && id[0] != '\0', FALSE);
	g_return_val_if_fail(G_IS_TLS_CERTIFICATE(certificate), FALSE);

	/* Ensure certificate directory exists */

	if (!ensure_certificate_dir(error)) {
		return FALSE;
	}

	/* Get the text representation of the certificate */

	g_object_get(certificate, "certificate-pem", &pem, NULL);
	g_return_val_if_fail(pem != NULL, FALSE);

	/* Save certificate text to a fail */

	path = make_certificate_path(id);
	ret = g_file_set_contents(path, pem, -1, error);
	g_free(path);
	g_free(pem);

	return ret;
}

gboolean
purple_tls_certificate_distrust(const gchar *id, GError **error)
{
	gchar *path;
	gboolean ret = TRUE;

	g_return_val_if_fail(id != NULL && id[0] != '\0', FALSE);

	/* Delete certificate file if it exists */

	path = make_certificate_path(id);

	if (g_unlink(path) != 0) {
		g_set_error_literal(error, G_FILE_ERROR,
				g_file_error_from_errno(errno),
				g_strerror(errno));
		ret = FALSE;
	}

	g_free(path);

	return ret;
}

/* Called when a GTlsConnection (which this handler has been connected to)
 *   has an error validating its certificate.
 * Returns TRUE if the certificate is already trusted, so the connection
 *   can continue.
 * Returns FALSE if the certificate is not trusted, causing the
 *   connection's handshake to fail.
 */
static gboolean
accept_certificate_cb(GTlsConnection *conn, GTlsCertificate *peer_cert,
		GTlsCertificateFlags errors, gpointer user_data)
{
	GTlsCertificate *trusted_cert;
	GSocketConnectable *connectable;
	const gchar *identity;

	g_return_val_if_fail(G_IS_TLS_CLIENT_CONNECTION(conn), FALSE);
	g_return_val_if_fail(G_IS_TLS_CERTIFICATE(peer_cert), FALSE);

	/* Get the certificate identity from the GTlsClientConnection */

	connectable = g_tls_client_connection_get_server_identity(
			G_TLS_CLIENT_CONNECTION(conn));

	g_return_val_if_fail(G_IS_SOCKET_CONNECTABLE(connectable), FALSE);

	/* identity is owned by the connectable */
	if (G_IS_NETWORK_ADDRESS(connectable)) {
		identity = g_network_address_get_hostname(
				G_NETWORK_ADDRESS(connectable));
	} else if (G_IS_NETWORK_SERVICE(connectable)) {
		identity = g_network_service_get_domain(
				G_NETWORK_SERVICE(connectable));
	} else {
		g_return_val_if_reached(FALSE);
	}

	/* See if a trusted certificate matching the peer certificate exists */

	trusted_cert = purple_tls_certificate_new_from_id(identity, NULL);

	if (trusted_cert != NULL &&
			g_tls_certificate_is_same(peer_cert, trusted_cert)) {
		/* It's manually trusted. Accept certificate */
		g_object_unref(trusted_cert);
		return TRUE;
	}

	/* Certificate failed and isn't trusted. Fail certificate */

	g_clear_object(&trusted_cert);
	return FALSE;
}

gpointer
purple_tls_certificate_attach_to_tls_connection(GTlsConnection *conn)
{
	return g_object_connect(conn, "signal::accept-certificate",
			accept_certificate_cb, NULL, NULL);
}

/* Called when GSocketClient signals an event.
 * Calls purple_tls_certificate_attach_to_tls_connection() on the client's
 * connection when it's about to handshake.
 */
static void
socket_client_event_cb(GSocketClient *client, GSocketClientEvent event,
		GSocketConnectable *connectable, GIOStream *connection,
		gpointer user_data)
{
	if (event == G_SOCKET_CLIENT_TLS_HANDSHAKING) {
		/* Attach libpurple's certificate subsystem to the
		 * GTlsConnection right before it starts the handshake
		 */
		purple_tls_certificate_attach_to_tls_connection(
				G_TLS_CONNECTION(connection));
	}
}

gpointer
purple_tls_certificate_attach_to_socket_client(GSocketClient *client)
{
	return g_object_connect(client, "signal::event",
			socket_client_event_cb, NULL, NULL);
}

