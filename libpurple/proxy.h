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
 */

#if !defined(PURPLE_GLOBAL_HEADER_INSIDE) && !defined(PURPLE_COMPILATION)
# error "only <purple.h> may be included directly"
#endif

#ifndef PURPLE_PROXY_H
#define PURPLE_PROXY_H
/**
 * SECTION:proxy
 * @section_id: libpurple-proxy
 * @short_description: <filename>proxy.h</filename>
 * @title: Proxy API
 */

#include <glib.h>
#include <gio/gio.h>
#include "eventloop.h"

/**
 * PurpleProxyType:
 * @PURPLE_PROXY_USE_GLOBAL: Use the global proxy information.
 * @PURPLE_PROXY_NONE:       No proxy.
 * @PURPLE_PROXY_HTTP:       HTTP proxy.
 * @PURPLE_PROXY_SOCKS4:     SOCKS 4 proxy.
 * @PURPLE_PROXY_SOCKS5:     SOCKS 5 proxy.
 * @PURPLE_PROXY_USE_ENVVAR: Use environmental settings.
 * @PURPLE_PROXY_TOR:        Use a Tor proxy (SOCKS 5 really).
 *
 * A type of proxy connection.
 */
typedef enum
{
	PURPLE_PROXY_USE_GLOBAL = -1,
	PURPLE_PROXY_NONE = 0,
	PURPLE_PROXY_HTTP,
	PURPLE_PROXY_SOCKS4,
	PURPLE_PROXY_SOCKS5,
	PURPLE_PROXY_USE_ENVVAR,
	PURPLE_PROXY_TOR

} PurpleProxyType;

/**
 * PurpleProxyInfo:
 *
 * Information on proxy settings.
 */
typedef struct _PurpleProxyInfo PurpleProxyInfo;


#include "account.h"

G_BEGIN_DECLS

/**************************************************************************/
/* Proxy structure API                                                    */
/**************************************************************************/

/**
 * purple_proxy_info_get_type:
 *
 * Returns: The #GType for proxy information.
 */
GType purple_proxy_info_get_type(void);

/**
 * purple_proxy_info_new:
 *
 * Creates a proxy information structure.
 *
 * Returns: The proxy information structure.
 */
PurpleProxyInfo *purple_proxy_info_new(void);

/**
 * purple_proxy_info_destroy:
 * @info: The proxy information structure to destroy.
 *
 * Destroys a proxy information structure.
 */
void purple_proxy_info_destroy(PurpleProxyInfo *info);

/**
 * purple_proxy_info_set_proxy_type:
 * @info: The proxy information.
 * @type: The proxy type.
 *
 * Sets the type of proxy.
 */
void purple_proxy_info_set_proxy_type(PurpleProxyInfo *info, PurpleProxyType type);

/**
 * purple_proxy_info_set_host:
 * @info: The proxy information.
 * @host: The host.
 *
 * Sets the proxy host.
 */
void purple_proxy_info_set_host(PurpleProxyInfo *info, const char *host);

/**
 * purple_proxy_info_set_port:
 * @info: The proxy information.
 * @port: The port.
 *
 * Sets the proxy port.
 */
void purple_proxy_info_set_port(PurpleProxyInfo *info, int port);

/**
 * purple_proxy_info_set_username:
 * @info:     The proxy information.
 * @username: The username.
 *
 * Sets the proxy username.
 */
void purple_proxy_info_set_username(PurpleProxyInfo *info, const char *username);

/**
 * purple_proxy_info_set_password:
 * @info:     The proxy information.
 * @password: The password.
 *
 * Sets the proxy password.
 */
void purple_proxy_info_set_password(PurpleProxyInfo *info, const char *password);

/**
 * purple_proxy_info_get_proxy_type:
 * @info: The proxy information.
 *
 * Returns the proxy's type.
 *
 * Returns: The type.
 */
PurpleProxyType purple_proxy_info_get_proxy_type(const PurpleProxyInfo *info);

/**
 * purple_proxy_info_get_host:
 * @info: The proxy information.
 *
 * Returns the proxy's host.
 *
 * Returns: The host.
 */
const char *purple_proxy_info_get_host(const PurpleProxyInfo *info);

/**
 * purple_proxy_info_get_port:
 * @info: The proxy information.
 *
 * Returns the proxy's port.
 *
 * Returns: The port.
 */
int purple_proxy_info_get_port(const PurpleProxyInfo *info);

/**
 * purple_proxy_info_get_username:
 * @info: The proxy information.
 *
 * Returns the proxy's username.
 *
 * Returns: The username.
 */
const char *purple_proxy_info_get_username(const PurpleProxyInfo *info);

/**
 * purple_proxy_info_get_password:
 * @info: The proxy information.
 *
 * Returns the proxy's password.
 *
 * Returns: The password.
 */
const char *purple_proxy_info_get_password(const PurpleProxyInfo *info);

/**************************************************************************/
/* Global Proxy API                                                       */
/**************************************************************************/

/**
 * purple_global_proxy_get_info:
 *
 * Returns purple's global proxy information.
 *
 * Returns: The global proxy information.
 */
PurpleProxyInfo *purple_global_proxy_get_info(void);

/**
 * purple_global_proxy_set_info:
 * @info:     The proxy information.
 *
 * Set purple's global proxy information.
 */
void purple_global_proxy_set_info(PurpleProxyInfo *info);

/**************************************************************************/
/* Proxy API                                                              */
/**************************************************************************/

/**
 * purple_proxy_get_handle:
 *
 * Returns the proxy subsystem handle.
 *
 * Returns: The proxy subsystem handle.
 */
void *purple_proxy_get_handle(void);

/**
 * purple_proxy_init:
 *
 * Initializes the proxy subsystem.
 */
void purple_proxy_init(void);

/**
 * purple_proxy_uninit:
 *
 * Uninitializes the proxy subsystem.
 */
void purple_proxy_uninit(void);

/**
 * purple_proxy_get_setup:
 * @account: The account for which the configuration is needed.
 *
 * Returns configuration of a proxy.
 *
 * Returns: The configuration of a proxy.
 */
PurpleProxyInfo *purple_proxy_get_setup(PurpleAccount *account);

/**
 * purple_proxy_get_proxy_resolver:
 * @account: The account for which to get the proxy resolver.
 * @error: Return location for a GError, or NULL.
 *
 * Returns a #GProxyResolver capable of resolving which proxy
 * to use for this account, if any. This object can be given to a
 * #GSocketClient for automatic proxy handling or can be used
 * directly if desired.
 *
 * Returns: (transfer full): NULL if there was an error with the
 *         account's (or system) proxy settings, or a reference to
 *         a #GProxyResolver on success.
 */
GProxyResolver *purple_proxy_get_proxy_resolver(PurpleAccount *account,
		GError **error);

G_END_DECLS

#endif /* PURPLE_PROXY_H */
