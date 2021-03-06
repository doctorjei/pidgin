/*
 * purple
 * Copyright (C) Pidgin Developers <devel@pidgin.im>
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if !defined(PURPLE_GLOBAL_HEADER_INSIDE) && !defined(PURPLE_COMPILATION)
# error "only <purple.h> may be included directly"
#endif

#ifndef PURPLE_ATTENTION_TYPE_H
#define PURPLE_ATTENTION_TYPE_H

#include <glib.h>
#include <glib-object.h>

/**
 * SECTION:purpleattentiontype
 * @section_id: libpurple-purpleattentiontype
 * @title: Attention Types
 */

#define PURPLE_TYPE_ATTENTION_TYPE (purple_attention_type_get_type())

/**
 * PurpleAttentionType:
 *
 * Represents "nudges" and "buzzes" that you may send to a buddy to attract
 * their attention (or vice-versa).
 */
typedef struct _PurpleAttentionType PurpleAttentionType;

G_BEGIN_DECLS

/**
 * purple_attention_type_get_type:
 *
 * Returns: The #GType for the #PurpleAttentionType boxed structure.
 */
GType purple_attention_type_get_type(void);

PurpleAttentionType *purple_attention_type_copy(PurpleAttentionType *attn);

/**
 * purple_attention_type_new:
 * @unlocalized_name: A non-localized string that can be used by UIs in need of such
 *               non-localized strings.  This should be the same as @name,
 *               without localization.
 * @name: A localized string that the UI may display for the event. This
 *             should be the same string as @unlocalized_name, with localization.
 * @incoming_description: A localized description shown when the event is received.
 * @outgoing_description: A localized description shown when the event is sent.
 *
 * Creates a new #PurpleAttentionType object and sets its mandatory parameters.
 *
 * Returns: A pointer to the new object.
 */
PurpleAttentionType *purple_attention_type_new(const gchar *unlocalized_name, const gchar *name,
								const gchar *incoming_description, const gchar *outgoing_description);

/**
 * purple_attention_type_get_name:
 * @type: The attention type.
 *
 * Get the attention type's name as displayed by the UI.
 *
 * Returns: The name.
 */
const gchar *purple_attention_type_get_name(const PurpleAttentionType *type);

/**
 * purple_attention_type_set_name:
 * @type: The attention type.
 * @name: The localized name that will be displayed by UIs. This should be
 *             the same string given as the unlocalized name, but with
 *             localization.
 *
 * Sets the displayed name of the attention-demanding event.
 */
void purple_attention_type_set_name(PurpleAttentionType *type, const gchar *name);

/**
 * purple_attention_type_get_incoming_desc:
 * @type: The attention type.
 *
 * Get the attention type's description shown when the event is received.
 *
 * Returns: The description.
 */
const gchar *purple_attention_type_get_incoming_desc(const PurpleAttentionType *type);

/**
 * purple_attention_type_set_incoming_desc:
 * @type: The attention type.
 * @desc: The localized description for incoming events.
 *
 * Sets the description of the attention-demanding event shown in conversations
 * when the event is received.
 */
void purple_attention_type_set_incoming_desc(PurpleAttentionType *type, const gchar *desc);

/**
 * purple_attention_type_get_outgoing_desc:
 * @type: The attention type.
 *
 * Get the attention type's description shown when the event is sent.
 *
 * Returns: The description.
 */
const gchar *purple_attention_type_get_outgoing_desc(const PurpleAttentionType *type);

/**
 * purple_attention_type_set_outgoing_desc:
 * @type: The attention type.
 * @desc: The localized description for outgoing events.
 *
 * Sets the description of the attention-demanding event shown in conversations
 * when the event is sent.
 */
void purple_attention_type_set_outgoing_desc(PurpleAttentionType *type, const gchar *desc);

/**
 * purple_attention_type_get_icon_name:
 * @type: The attention type.
 *
 * Get the attention type's icon name.
 *
 * Note: Icons are optional for attention events.
 *
 * Returns: The icon name or %NULL if unset/empty.
 */
const gchar *purple_attention_type_get_icon_name(const PurpleAttentionType *type);

/**
 * purple_attention_type_set_icon_name:
 * @type: The attention type.
 * @name: The icon's name.
 *
 * Sets the name of the icon to display for the attention event; this is optional.
 *
 * Note: Icons are optional for attention events.
 */
void purple_attention_type_set_icon_name(PurpleAttentionType *type, const gchar *name);

/**
 * purple_attention_type_get_unlocalized_name:
 * @type: The attention type
 *
 * Get the attention type's unlocalized name; this is useful for some UIs.
 *
 * Returns: The unlocalized name.
 */
const gchar *purple_attention_type_get_unlocalized_name(const PurpleAttentionType *type);

/**
 * purple_attention_type_set_unlocalized_name:
 * @type: The attention type.
 * @ulname: The unlocalized name.  This should be the same string given as
 *               the localized name, but without localization.
 *
 * Sets the unlocalized name of the attention event; some UIs may need this,
 * thus it is required.
 */
void purple_attention_type_set_unlocalized_name(PurpleAttentionType *type, const gchar *ulname);

G_END_DECLS

#endif /* PURPLE_ATTENTION_TYPE_H */
