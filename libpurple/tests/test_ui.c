/*
 * pidgin
 *
 * Pidgin is the legal property of its developers, whose names are too numerous
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

#include "purple.h"

#include <glib.h>
#include <glib/gprintf.h>

#include <signal.h>
#include <string.h>
#ifdef _WIN32
#  include <conio.h>
#else
#  include <unistd.h>
#endif

#include "test_ui.h"

/*** Conversation uiops ***/
static void
test_write_conv(PurpleConversation *conv, PurpleMessage *msg)
{
	gchar *timestamp = purple_message_format_timestamp(msg, "(%H:%M:%S)");

	printf("(%s) %s %s: %s\n",
		purple_conversation_get_name(conv),
		timestamp,
		purple_message_get_author_alias(msg),
		purple_message_get_contents(msg));

	g_free(timestamp);
}

static PurpleConversationUiOps test_conv_uiops = {
	.write_conv = test_write_conv
};

static void
test_ui_init(void)
{
	purple_conversations_set_ui_ops(&test_conv_uiops);
}

static PurpleCoreUiOps test_core_uiops = {
	.ui_init = test_ui_init
};

void
test_ui_purple_init(void) {
#ifndef _WIN32
	/* libpurple's built-in DNS resolution forks processes to perform
	 * blocking lookups without blocking the main process.  It does not
	 * handle SIGCHLD itself, so if the UI does not you quickly get an army
	 * of zombie subprocesses marching around.
	 */
	signal(SIGCHLD, SIG_IGN);
#endif

	/* set the magic PURPLE_PLUGINS_SKIP environment variable */
	g_setenv("PURPLE_PLUGINS_SKIP", "1", TRUE);

	/* Set a custom user directory (optional) */
	purple_util_set_user_dir(TEST_DATA_DIR);

	/* We do not want any debugging for now to keep the noise to a minimum. */
	purple_debug_set_enabled(FALSE);

	/* Set the core-uiops, which is used to
	 * 	- initialize the ui specific preferences.
	 * 	- initialize the debug ui.
	 * 	- initialize the ui components for all the modules.
	 * 	- uninitialize the ui components for all the modules when the core terminates.
	 */
	purple_core_set_ui_ops(&test_core_uiops);

	/* Now that all the essential stuff has been set, let's try to init the core. It's
	 * necessary to provide a non-NULL name for the current ui to the core. This name
	 * is used by stuff that depends on this ui, for example the ui-specific plugins. */
	if (!purple_core_init("test-ui")) {
		/* Initializing the core failed. Terminate. */
		fprintf(stderr,
				"libpurple initialization failed. Dumping core.\n"
				"Please report this!\n");
		abort();
	}

	/* Set path to search for plugins. The core (libpurple) takes care of loading the
	 * core-plugins, which includes the in-tree protocols. So it is not essential to add
	 * any path here, but it might be desired, especially for ui-specific plugins. */
	purple_plugins_add_search_path(TEST_DATA_DIR);
	purple_plugins_refresh();

	/* Load the preferences. */
	purple_prefs_load();

	/* Load the desired plugins. The client should save the list of loaded plugins in
	 * the preferences using purple_plugins_save_loaded() */
	purple_plugins_load_saved("/purple/test_ui/plugins/saved");
}
