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

#ifndef PURPLE_NOVELL_NOVELL_H
#define PURPLE_NOVELL_NOVELL_H

#include <gmodule.h>

#include <purple.h>

#define NOVELL_TYPE_PROTOCOL             (novell_protocol_get_type())
#define NOVELL_PROTOCOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), NOVELL_TYPE_PROTOCOL, NovellProtocol))
#define NOVELL_PROTOCOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), NOVELL_TYPE_PROTOCOL, NovellProtocolClass))
#define NOVELL_IS_PROTOCOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), NOVELL_TYPE_PROTOCOL))
#define NOVELL_IS_PROTOCOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), NOVELL_TYPE_PROTOCOL))
#define NOVELL_PROTOCOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), NOVELL_TYPE_PROTOCOL, NovellProtocolClass))

typedef struct
{
	PurpleProtocol parent;
} NovellProtocol;

typedef struct
{
	PurpleProtocolClass parent_class;
} NovellProtocolClass;

/**
 * Returns the GType for the NovellProtocol object.
 */
G_MODULE_EXPORT GType novell_protocol_get_type(void);

#endif /* PURPLE_NOVELL_NOVELL_H */
