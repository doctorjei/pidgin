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

#ifndef PURPLE_FACEBOOK_HTTP_H
#define PURPLE_FACEBOOK_HTTP_H

/**
 * SECTION:http
 * @section_id: facebook-http
 * @short_description: <filename>http.h</filename>
 * @title: HTTP Utilities
 *
 * The HTTP utilities.
 */

#include <glib.h>
#include <libsoup/soup.h>

/**
 * FB_HTTP_ERROR:
 *
 * The #GQuark of the domain of HTTP errors.
 */
#define FB_HTTP_ERROR  fb_http_error_quark()

/**
 * FbHttpParams:
 *
 * Represents a set of key/value HTTP parameters.
 */
typedef GHashTable FbHttpParams;

/**
 * FbHttpError:
 * @FB_HTTP_ERROR_SUCCESS: There is no error.
 * @FB_HTTP_ERROR_NOMATCH: The name does not match anything.
 *
 * The error codes for the #FB_HTTP_ERROR domain.
 */
typedef enum
{
	FB_HTTP_ERROR_SUCCESS = 0,
	FB_HTTP_ERROR_NOMATCH
} FbHttpError;

/**
 * fb_http_error_quark:
 *
 * Gets the #GQuark of the domain of HTTP errors.
 *
 * Returns: The #GQuark of the domain.
 */
GQuark
fb_http_error_quark(void);

/**
 * fb_http_error_chk:
 * @res: The #SoupMessage.
 * @error: The return location for the #GError or #NULL.
 *
 * Checks a #SoupMessage for success. This optionally assigns an
 * appropriate #GError upon failure.
 *
 * Returns: #TRUE if the request was successful, otherwise #FALSE.
 */
gboolean fb_http_error_chk(SoupMessage *res, GError **error);

/**
 * fb_http_params_new:
 *
 * Creates a new #FbHttpParams. The returned #FbHttpParams should be
 * freed with #fb_http_params_free() when no longer needed. Optionally,
 * instead of freeing, the returned #FbHttpParams can be closed with
 * #fb_http_params_close().
 *
 * Returns: The new #FbHttpParams.
 */
FbHttpParams *
fb_http_params_new(void);

/**
 * fb_http_params_new_parse:
 * @data: The string containing HTTP parameters.
 * @isurl: #TRUE if @data is a URL, otherwise #FALSE.
 *
 * Creates a new #FbHttpParams. This parses the #FbHttpParams from a
 * string, which can be a URL. The returned #FbHttpParams should be
 * freed with #fb_http_params_free() when no longer needed. Optionally,
 * instead of freeing, the returned #FbHttpParams can be closed with
 * #fb_http_params_close().
 *
 * Returns: The new #FbHttpParams.
 */
FbHttpParams *
fb_http_params_new_parse(const gchar *data, gboolean isurl);

/**
 * fb_http_params_free:
 * @params: The #FbHttpParams.
 *
 * Frees all memory used by the #FbHttpParams.
 */
void
fb_http_params_free(FbHttpParams *params);

/**
 * fb_http_params_get_bool:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @error: The return location for the #GError or #NULL.
 *
 * Gets a boolean value from the #FbHttpParams. This optionally assigns
 * an appropriate #GError upon failure.
 *
 * Return: The boolean value.
 */
gboolean
fb_http_params_get_bool(FbHttpParams *params, const gchar *name,
                        GError **error);

/**
 * fb_http_params_get_dbl:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @error: The return location for the #GError or #NULL.
 *
 * Gets a floating point value from the #FbHttpParams. This optionally
 * assigns an appropriate #GError upon failure.
 *
 * Return: The floating point value.
 */
gdouble
fb_http_params_get_dbl(FbHttpParams *params, const gchar *name,
                       GError **error);

/**
 * fb_http_params_get_int:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @error: The return location for the #GError or #NULL.
 *
 * Gets an integer value from the #FbHttpParams. This optionally
 * assigns an appropriate #GError upon failure.
 *
 * Return: The integer value.
 */
gint64
fb_http_params_get_int(FbHttpParams *params, const gchar *name,
                       GError **error);

/**
 * fb_http_params_get_str:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @error: The return location for the #GError or #NULL.
 *
 * Gets a string value from the #FbHttpParams. This optionally assigns
 * an appropriate #GError upon failure.
 *
 * Return: The string value.
 */
const gchar *
fb_http_params_get_str(FbHttpParams *params, const gchar *name,
                       GError **error);

/**
 * fb_http_params_dup_str:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @error: The return location for the #GError or #NULL.
 *
 * Gets a duplicated string value from the #FbHttpParams. This
 * optionally assigns an appropriate #GError upon failure. The returned
 * string should be freed with #g_free() when no longer needed.
 *
 * Return: The duplicated string value.
 */
gchar *
fb_http_params_dup_str(FbHttpParams *params, const gchar *name,
                       GError **error);

/**
 * fb_http_params_set_bool:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @value: The value.
 *
 * Sets a boolean value to the #FbHttpParams.
 */
void
fb_http_params_set_bool(FbHttpParams *params, const gchar *name,
		        gboolean value);

/**
 * fb_http_params_set_dbl:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @value: The value.
 *
 * Sets a floating point value to the #FbHttpParams.
 */
void
fb_http_params_set_dbl(FbHttpParams *params, const gchar *name, gdouble value);

/**
 * fb_http_params_set_int:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @value: The value.
 *
 * Sets an integer value to the #FbHttpParams.
 */
void
fb_http_params_set_int(FbHttpParams *params, const gchar *name, gint64 value);

/**
 * fb_http_params_set_str:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @value: The value.
 *
 * Sets a string value to the #FbHttpParams.
 */
void
fb_http_params_set_str(FbHttpParams *params, const gchar *name,
                       const gchar *value);

/**
 * fb_http_params_set_strf:
 * @params: The #FbHttpParams.
 * @name: The parameter name.
 * @format: The format string literal.
 * @...: The arguments for @format.
 *
 * Sets a formatted string value to the #FbHttpParams.
 */
void
fb_http_params_set_strf(FbHttpParams *params, const gchar *name,
                        const gchar *format, ...)
                        G_GNUC_PRINTF(3, 4);

/**
 * fb_http_urlcmp:
 * @url1: The first URL.
 * @url2: The second URL.
 * @protocol: #TRUE to match the protocols, otherwise #FALSE.
 *
 * Compares two URLs. This is more reliable than just comparing two URL
 * strings, as it avoids casing in some areas, while not in others. It
 * can also, optionally, ignore the matching of the URL protocol.
 *
 * Returns: #TRUE if the URLs match, otherwise #FALSE.
 */
gboolean
fb_http_urlcmp(const gchar *url1, const gchar *url2, gboolean protocol);

#endif /* PURPLE_FACEBOOK_HTTP_H */
