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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02111-1301 USA
 */

#if !defined(PURPLE_GLOBAL_HEADER_INSIDE) && !defined(PURPLE_COMPILATION)
# error "only <purple.h> may be included directly"
#endif

#ifndef PURPLE_SMILEY_THEME_H
#define PURPLE_SMILEY_THEME_H
/**
 * SECTION:smiley-theme
 * @include:smiley-theme.h
 * @section_id: libpurple-smiley-theme
 * @short_description: a categorized set of standard smileys
 * @title: Smiley themes
 *
 * A smiley theme is a set of standard smileys, that may be displayed in user's
 * message window instead of their textual representations. It may be
 * categorized depending on the selected protocol, but it's up to the UI to
 * choose behavior.
 */

#include <glib-object.h>

#include "smiley.h"
#include "smiley-list.h"

G_BEGIN_DECLS

/**
 * PURPLE_TYPE_SMILEY_THEME:
 *
 * The standard _get_type macro for #PurpleSmileyTheme.
 */
#define PURPLE_TYPE_SMILEY_THEME  purple_smiley_theme_get_type()

/**
 * purple_smiley_theme_get_type:
 *
 * Returns: the #GType for a smiley list.
 */
G_DECLARE_DERIVABLE_TYPE(PurpleSmileyTheme, purple_smiley_theme, PURPLE,
		SMILEY_THEME, GObject)

/**
 * PurpleSmileyThemeClass:
 * @get_smileys: a callback for getting smiley list based on choosen category.
 *               The criteria for a category are being passed using the
 *               @ui_data parameter.
 * @activate: a callback being fired after activating the @theme. It may be used
 *            for loading its contents before using @get_smileys callback.
 *
 * Base class for #PurpleSmileyTheme objects.
 */
struct _PurpleSmileyThemeClass
{
	/*< private >*/
	GObjectClass parent_class;

	/*< public >*/
	PurpleSmileyList * (*get_smileys)(PurpleSmileyTheme *theme,
		gpointer ui_data);
	void (*activate)(PurpleSmileyTheme *theme);

	/*< private >*/
	void (*purple_reserved1)(void);
	void (*purple_reserved2)(void);
	void (*purple_reserved3)(void);
	void (*purple_reserved4)(void);
};

/**
 * purple_smiley_theme_get_smileys:
 * @theme: the smiley theme.
 * @ui_data: the UI-passed criteria to choose a smiley set.
 *
 * Retrieves a smiley category based on UI-provided criteria.
 *
 * You might want to use <link linkend="libpurple-smiley-parser">smiley
 * parser</link> instead. It's mostly for the UI, prpls shouldn't use it.
 *
 * Returns: (transfer none): a #PurpleSmileyList with standard smileys to use.
 */
PurpleSmileyList *
purple_smiley_theme_get_smileys(PurpleSmileyTheme *theme, gpointer ui_data);

/**
 * purple_smiley_theme_set_current:
 * @theme: the smiley theme to be set as currently used. May be %NULL.
 *
 * Sets the new smiley theme to be used for displaying messages.
 */
void
purple_smiley_theme_set_current(PurpleSmileyTheme *theme);

/**
 * purple_smiley_theme_get_current:
 *
 * Returns the currently used smiley theme.
 *
 * Returns: (transfer none): the #PurpleSmileyTheme or %NULL, if none is set.
 */
PurpleSmileyTheme *
purple_smiley_theme_get_current(void);

/**
 * _purple_smiley_theme_uninit: (skip)
 *
 * Uninitializes the smileys theme subsystem.
 */
void
_purple_smiley_theme_uninit(void);

G_END_DECLS

#endif /* PURPLE_SMILEY_THEME_H */
