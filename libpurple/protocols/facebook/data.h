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

#ifndef _FACEBOOK_DATA_H_
#define _FACEBOOK_DATA_H_

#include "connection.h"
#include "glibcompat.h"

#define FB_TYPE_DATA             (fb_data_get_type())
#define FB_DATA(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), FB_TYPE_DATA, FbData))
#define FB_DATA(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), FB_TYPE_DATA, FbData))
#define FB_DATA_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), FB_TYPE_DATA, FbDataClass))
#define FB_IS_DATA(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), FB_TYPE_DATA))
#define FB_IS_DATA_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), FB_TYPE_DATA))
#define FB_DATA_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), FB_TYPE_DATA, FbDataClass))

typedef struct _FbData FbData;
typedef struct _FbDataClass FbDataClass;
typedef struct _FbDataPrivate FbDataPrivate;

struct _FbData
{
	GObject parent;
	FbDataPrivate *priv;
};

struct _FbDataClass
{
	GObjectClass parent_class;
};


GType
fb_data_get_type(void);

FbData *
fb_data_new(PurpleConnection *gc);

gboolean
fb_data_load(FbData *fata);

void
fb_data_save(FbData *fata);

FbApi *
fb_data_get_api(FbData *fata);

gint
fb_data_get_chatid(FbData *fata);

PurpleConnection *
fb_data_get_connection(FbData *fata);

PurpleRoomlist *
fb_data_get_roomlist(FbData *fata);

void
fb_data_set_roomlist(FbData *fata, PurpleRoomlist *list);

#endif /* _FACEBOOK_DATA_H_ */
