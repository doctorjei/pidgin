/**
 * @file buddyicon.c Buddy Icon API
 * @ingroup core
 */

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
#define _PURPLE_BUDDYICON_C_

#include "internal.h"
#include "buddyicon.h"
#include "conversation.h"
#include "dbus-maybe.h"
#include "debug.h"
#include "imgstore.h"
#include "util.h"

/* NOTE: Instances of this struct are allocated without zeroing the memory, so
 * NOTE: be sure to update purple_buddy_icon_new() if you add members. */
struct _PurpleBuddyIcon
{
	PurpleAccount *account;    /**< The account the user is on.          */
	PurpleStoredImage *img;    /**< The stored image containing
	                                the icon data.                       */
	char *username;            /**< The username the icon belongs to.    */
	char *checksum;            /**< The protocol checksum.               */
	unsigned int ref_count;    /**< The buddy icon reference count.      */
};

/**
 * This is the big grand daddy hash table that contains references to
 * everybody's buddy icons.
 *
 * Key is a PurpleAccount.
 * Value is another hash table, usually referred to as "icon_cache."
 * For this inner hash table:
 *    Key is the username of the buddy whose icon is being stored.
 *    Value is the PurpleBuddyIcon for this buddy.
 */
static GHashTable *account_cache = NULL;

/**
 * This hash table contains a bunch of PurpleStoredImages that are
 * shared across all accounts.
 *
 * Key is the filename for this image as constructed by
 * purple_util_get_image_filename().  So it is the base16 encoded
 * sha-1 hash plus an appropriate file extension.  For example:
 *   "0f4972d17d1e70e751c43c90c948e72efbff9796.gif"
 *
 * The value is a PurpleStoredImage containing the icon data.  These
 * images are reference counted, and when the count reaches 0
 * imgstore.c emits the image-deleting signal and we remove the image
 * from the hash table (but it might still be saved on disk, if the
 * icon is being used by offline accounts or some such).
 */
static GHashTable *icon_data_cache = NULL;

/**
 * This hash table contains reference counts for how many times each
 * icon in the ~/.purple/icons/ directory is being used.  It's pretty
 * crazy.  It maintains the reference count across sessions, too, so
 * if you exit Pidgin then this hash table is reconstructed the next
 * time Pidgin starts.
 *
 * Key is the filename for this image as constructed by
 * purple_util_get_image_filename().  So it is the base16 encoded
 * sha-1 hash plus an appropriate file extension.  For example:
 *   "0f4972d17d1e70e751c43c90c948e72efbff9796.gif"
 *
 * The value is a GINT_TO_POINTER count of the number of times this
 * icon is used.  So if four of your buddies are using an icon, and
 * you have the icon set for two of your accounts, then this number
 * will be six.  When this reference count reaches 0 the icon will
 * be deleted from disk.
 */
static GHashTable *icon_file_cache = NULL;

/**
 * This hash table is used for both custom buddy icons on PurpleBListNodes and
 * account icons.
 */
static GHashTable *pointer_icon_cache = NULL;

static char       *cache_dir     = NULL;

/** "Should icons be cached to disk?" */
static gboolean    icon_caching  = TRUE;

static void delete_buddy_icon_settings(PurpleBListNode *node, const char *setting_name);

/*
 * Begin functions for dealing with the on-disk icon cache
 */

static void
ref_filename(const char *filename)
{
	int refs;

	g_return_if_fail(filename != NULL);

	refs = GPOINTER_TO_INT(g_hash_table_lookup(icon_file_cache, filename));

	g_hash_table_insert(icon_file_cache, g_strdup(filename),
	                    GINT_TO_POINTER(refs + 1));
}

static void
unref_filename(const char *filename)
{
	int refs;

	if (filename == NULL)
		return;

	refs = GPOINTER_TO_INT(g_hash_table_lookup(icon_file_cache, filename));

	if (refs == 1)
	{
		g_hash_table_remove(icon_file_cache, filename);
	}
	else
	{
		g_hash_table_insert(icon_file_cache, g_strdup(filename),
		                    GINT_TO_POINTER(refs - 1));
	}
}

static void
purple_buddy_icon_data_cache(PurpleStoredImage *img)
{
	const char *dirname;
	char *path;

	g_return_if_fail(img != NULL);

	if (!purple_buddy_icons_is_caching())
		return;

	dirname = purple_buddy_icons_get_cache_dir();
	path = g_build_filename(dirname, purple_imgstore_get_filename(img), NULL);

	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
	{
		purple_debug_info("buddyicon", "Creating icon cache directory.\n");

		if (g_mkdir(dirname, S_IRUSR | S_IWUSR | S_IXUSR) < 0)
		{
			purple_debug_error("buddyicon",
			                   "Unable to create directory %s: %s\n",
			                   dirname, g_strerror(errno));
		}
	}

	purple_util_write_data_to_file_absolute(path, purple_imgstore_get_data(img),
											purple_imgstore_get_size(img));
	g_free(path);
}

static void
purple_buddy_icon_data_uncache_file(const char *filename)
{
	const char *dirname;
	char *path;

	g_return_if_fail(filename != NULL);

	/* It's possible that there are other references to this icon
	 * cache file that are not currently loaded into memory. */
	if (GPOINTER_TO_INT(g_hash_table_lookup(icon_file_cache, filename)))
		return;

	dirname = purple_buddy_icons_get_cache_dir();
	path = g_build_filename(dirname, filename, NULL);

	if (g_file_test(path, G_FILE_TEST_EXISTS))
	{
		if (g_unlink(path))
		{
			purple_debug_error("buddyicon", "Failed to delete %s: %s\n",
			                   path, g_strerror(errno));
		}
		else
		{
			purple_debug_info("buddyicon", "Deleted cache file: %s\n", path);
		}
	}

	g_free(path);
}

/*
 * End functions for dealing with the on-disk icon cache
 */

/*
 * Begin functions for dealing with the in-memory icon cache
 */

static gboolean
value_equals(gpointer key, gpointer value, gpointer user_data)
{
	return (value == user_data);
}

static void
image_deleting_cb(const PurpleStoredImage *img, gpointer data)
{
	const char *filename = purple_imgstore_get_filename(img);

	/* If there's no filename, it can't be one of our images. */
	if (filename == NULL)
		return;

	if (img == g_hash_table_lookup(icon_data_cache, filename))
	{
		purple_buddy_icon_data_uncache_file(filename);
		g_hash_table_remove(icon_data_cache, filename);

		/* We could make this O(1) by using another hash table, but
		 * this is probably good enough. */
		g_hash_table_foreach_remove(pointer_icon_cache, value_equals, (gpointer)img);
	}
}

static PurpleStoredImage *
purple_buddy_icon_data_new(guchar *icon_data, size_t icon_len)
{
	char *file;
	PurpleStoredImage *img;

	g_return_val_if_fail(icon_data != NULL, NULL);
	g_return_val_if_fail(icon_len > 0, NULL);

	file = purple_util_get_image_filename(icon_data, icon_len);

	img = g_hash_table_lookup(icon_data_cache, file);
	if (img) {
		g_free(file);
		g_free(icon_data);
		return purple_imgstore_ref(img);
	}

	img = purple_imgstore_new(icon_data, icon_len, file);

	/* This will take ownership of file and free it as needed */
	g_hash_table_insert(icon_data_cache, file, img);

	purple_buddy_icon_data_cache(img);

	return img;
}

/*
 * End functions for dealing with the in-memory icon cache
 */

static PurpleBuddyIcon *
purple_buddy_icon_create(PurpleAccount *account, const char *username)
{
	PurpleBuddyIcon *icon;
	GHashTable *icon_cache;

	/* This does not zero.  See purple_buddy_icon_new() for
	 * information on which function allocates which member. */
	icon = g_slice_new(PurpleBuddyIcon);
	PURPLE_DBUS_REGISTER_POINTER(icon, PurpleBuddyIcon);

	icon->account = account;
	icon->username = g_strdup(username);
	icon->checksum = NULL;
	icon->ref_count = 1;

	icon_cache = g_hash_table_lookup(account_cache, account);

	if (icon_cache == NULL)
	{
		icon_cache = g_hash_table_new(g_str_hash, g_str_equal);

		g_hash_table_insert(account_cache, account, icon_cache);
	}

	g_hash_table_insert(icon_cache,
	                    (char *)purple_buddy_icon_get_username(icon), icon);
	return icon;
}

PurpleBuddyIcon *
purple_buddy_icon_new(PurpleAccount *account, const char *username,
                      void *icon_data, size_t icon_len,
                      const char *checksum)
{
	PurpleBuddyIcon *icon;

	g_return_val_if_fail(account   != NULL, NULL);
	g_return_val_if_fail(username  != NULL, NULL);
	g_return_val_if_fail(icon_data != NULL, NULL);
	g_return_val_if_fail(icon_len  > 0,    NULL);

	/* purple_buddy_icons_find() does allocation, so be
	 * sure to update it as well when members are added. */
	icon = purple_buddy_icons_find(account, username);

	/* purple_buddy_icon_create() sets account & username */
	if (icon == NULL)
		icon = purple_buddy_icon_create(account, username);

	/* purple_buddy_icon_set_data() sets img, but it
	 * references img first, so we need to initialize it */
	icon->img = NULL;
	purple_buddy_icon_set_data(icon, icon_data, icon_len, checksum);

	return icon;
}

PurpleBuddyIcon *
purple_buddy_icon_ref(PurpleBuddyIcon *icon)
{
	g_return_val_if_fail(icon != NULL, NULL);

	icon->ref_count++;

	return icon;
}

void
purple_buddy_icon_unref(PurpleBuddyIcon *icon)
{
	if (icon == NULL)
		return;

	g_return_if_fail(icon->ref_count > 0);

	icon->ref_count--;

	if (icon->ref_count == 0)
	{
		GHashTable *icon_cache = g_hash_table_lookup(account_cache, purple_buddy_icon_get_account(icon));

		if (icon_cache != NULL)
			g_hash_table_remove(icon_cache, purple_buddy_icon_get_username(icon));

		g_free(icon->username);
		g_free(icon->checksum);
		purple_imgstore_unref(icon->img);

		PURPLE_DBUS_UNREGISTER_POINTER(icon);
		g_slice_free(PurpleBuddyIcon, icon);
	}
}

void
purple_buddy_icon_update(PurpleBuddyIcon *icon)
{
	PurpleIMConversation *im;
	PurpleAccount *account;
	const char *username;
	PurpleBuddyIcon *icon_to_set;
	GSList *buddies;

	g_return_if_fail(icon != NULL);

	account  = purple_buddy_icon_get_account(icon);
	username = purple_buddy_icon_get_username(icon);

	/* If no data exists (icon->img == NULL), then call the functions below
	 * with NULL to unset the icon.  They will then unref the icon and it should
	 * be destroyed.  The only way it wouldn't be destroyed is if someone
	 * else is holding a reference to it, in which case they can kill
	 * the icon when they realize it has no data. */
	icon_to_set = icon->img ? icon : NULL;

	/* Ensure that icon remains valid throughout */
	purple_buddy_icon_ref(icon);

	buddies = purple_blist_find_buddies(account, username);
	while (buddies != NULL)
	{
		PurpleBuddy *buddy = (PurpleBuddy *)buddies->data;
		char *old_icon;

		purple_buddy_set_icon(buddy, icon_to_set);
		old_icon = g_strdup(purple_blist_node_get_string((PurpleBListNode *)buddy,
		                                                 "buddy_icon"));
		if (icon->img && purple_buddy_icons_is_caching())
		{
			const char *filename = purple_imgstore_get_filename(icon->img);
			purple_blist_node_set_string((PurpleBListNode *)buddy,
			                             "buddy_icon",
			                             filename);

			if (icon->checksum && *icon->checksum)
			{
				purple_blist_node_set_string((PurpleBListNode *)buddy,
				                             "icon_checksum",
				                             icon->checksum);
			}
			else
			{
				purple_blist_node_remove_setting((PurpleBListNode *)buddy,
				                                 "icon_checksum");
			}
			ref_filename(filename);
		}
		else if (!icon->img)
		{
			purple_blist_node_remove_setting((PurpleBListNode *)buddy, "buddy_icon");
			purple_blist_node_remove_setting((PurpleBListNode *)buddy, "icon_checksum");
		}
		unref_filename(old_icon);
		g_free(old_icon);

		buddies = g_slist_delete_link(buddies, buddies);
	}

	im = purple_conversations_find_im_with_account(username, account);

	if (im != NULL)
		purple_im_conversation_set_icon(im, icon_to_set);

	/* icon's refcount was incremented above */
	purple_buddy_icon_unref(icon);
}

void
purple_buddy_icon_set_data(PurpleBuddyIcon *icon, guchar *data,
                           size_t len, const char *checksum)
{
	PurpleStoredImage *old_img;

	g_return_if_fail(icon != NULL);

	old_img = icon->img;
	icon->img = NULL;

	if (data != NULL)
	{
		if (len > 0)
			icon->img = purple_buddy_icon_data_new(data, len);
		else
			g_free(data);
	}

	g_free(icon->checksum);
	icon->checksum = g_strdup(checksum);

	purple_buddy_icon_update(icon);

	purple_imgstore_unref(old_img);
}

PurpleAccount *
purple_buddy_icon_get_account(const PurpleBuddyIcon *icon)
{
	g_return_val_if_fail(icon != NULL, NULL);

	return icon->account;
}

const char *
purple_buddy_icon_get_username(const PurpleBuddyIcon *icon)
{
	g_return_val_if_fail(icon != NULL, NULL);

	return icon->username;
}

const char *
purple_buddy_icon_get_checksum(const PurpleBuddyIcon *icon)
{
	g_return_val_if_fail(icon != NULL, NULL);

	return icon->checksum;
}

gconstpointer
purple_buddy_icon_get_data(const PurpleBuddyIcon *icon, size_t *len)
{
	g_return_val_if_fail(icon != NULL, NULL);

	if (icon->img)
	{
		if (len != NULL)
			*len = purple_imgstore_get_size(icon->img);

		return purple_imgstore_get_data(icon->img);
	}

	return NULL;
}

const char *
purple_buddy_icon_get_extension(const PurpleBuddyIcon *icon)
{
	if (icon->img != NULL)
		return purple_imgstore_get_extension(icon->img);

	return NULL;
}

void
purple_buddy_icons_set_for_user(PurpleAccount *account, const char *username,
                                void *icon_data, size_t icon_len,
                                const char *checksum)
{
	GHashTable *icon_cache;
	PurpleBuddyIcon *icon = NULL;

	g_return_if_fail(account  != NULL);
	g_return_if_fail(username != NULL);

	icon_cache = g_hash_table_lookup(account_cache, account);

	if (icon_cache != NULL)
		icon = g_hash_table_lookup(icon_cache, username);

	if (icon != NULL)
		purple_buddy_icon_set_data(icon, icon_data, icon_len, checksum);
	else if (icon_data && icon_len > 0)
	{
		PurpleBuddyIcon *icon = purple_buddy_icon_new(account, username, icon_data, icon_len, checksum);

		/* purple_buddy_icon_new() calls
		 * purple_buddy_icon_set_data(), which calls
		 * purple_buddy_icon_update(), which has the buddy list
		 * and conversations take references as appropriate.
		 * This function doesn't return icon, so we can't
		 * leave a reference dangling. */
		purple_buddy_icon_unref(icon);
	}
	else
	{
		/* If the buddy list or a conversation was holding a
		 * reference, we'd have found the icon in the cache.
		 * Since we know we're deleting the icon, we only
		 * need a subset of purple_buddy_icon_update(). */

		GSList *buddies = purple_blist_find_buddies(account, username);
		while (buddies != NULL)
		{
			PurpleBuddy *buddy = (PurpleBuddy *)buddies->data;

			unref_filename(purple_blist_node_get_string((PurpleBListNode *)buddy, "buddy_icon"));
			purple_blist_node_remove_setting((PurpleBListNode *)buddy, "buddy_icon");
			purple_blist_node_remove_setting((PurpleBListNode *)buddy, "icon_checksum");

			buddies = g_slist_delete_link(buddies, buddies);
		}
	}
}

char *purple_buddy_icon_get_full_path(PurpleBuddyIcon *icon)
{
	char *path;

	g_return_val_if_fail(icon != NULL, NULL);

	if (icon->img == NULL)
		return NULL;

	path = g_build_filename(purple_buddy_icons_get_cache_dir(),
	                        purple_imgstore_get_filename(icon->img), NULL);
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
	{
		g_free(path);
		return NULL;
	}
	return path;
}

const char *
purple_buddy_icons_get_checksum_for_user(PurpleBuddy *buddy)
{
	return purple_blist_node_get_string((PurpleBListNode*)buddy,
	                                    "icon_checksum");
}

static gboolean
read_icon_file(const char *path, guchar **data, size_t *len)
{
	GError *err = NULL;

	if (!g_file_get_contents(path, (gchar **)data, len, &err))
	{
		purple_debug_error("buddyicon", "Error reading %s: %s\n",
		                   path, err->message);
		g_error_free(err);

		return FALSE;
	}

	return TRUE;
}

PurpleBuddyIcon *
purple_buddy_icons_find(PurpleAccount *account, const char *username)
{
	GHashTable *icon_cache;
	PurpleBuddyIcon *icon = NULL;

	g_return_val_if_fail(account  != NULL, NULL);
	g_return_val_if_fail(username != NULL, NULL);

	icon_cache = g_hash_table_lookup(account_cache, account);

	if ((icon_cache == NULL) || ((icon = g_hash_table_lookup(icon_cache, username)) == NULL))
	{
		/* The icon is not currently cached in memory--try reading from disk */
		PurpleBuddy *b = purple_blist_find_buddy(account, username);
		const char *protocol_icon_file;
		const char *dirname;
		gboolean caching;
		guchar *data;
		size_t len;

		if (!b)
			return NULL;

		protocol_icon_file = purple_blist_node_get_string((PurpleBListNode*)b, "buddy_icon");

		if (protocol_icon_file == NULL)
			return NULL;

		dirname = purple_buddy_icons_get_cache_dir();

		caching = purple_buddy_icons_is_caching();
		/* By disabling caching temporarily, we avoid a loop
		 * and don't have to add special code through several
		 * functions. */
		purple_buddy_icons_set_caching(FALSE);

		if (protocol_icon_file != NULL)
		{
			char *path = g_build_filename(dirname, protocol_icon_file, NULL);
			if (read_icon_file(path, &data, &len))
			{
				const char *checksum;

				icon = purple_buddy_icon_create(account, username);
				icon->img = NULL;
				checksum = purple_blist_node_get_string((PurpleBListNode*)b, "icon_checksum");
				purple_buddy_icon_set_data(icon, data, len, checksum);
			}
			else
				delete_buddy_icon_settings((PurpleBListNode*)b, "buddy_icon");

			g_free(path);
		}

		purple_buddy_icons_set_caching(caching);
	}

	return (icon ? purple_buddy_icon_ref(icon) : NULL);
}

PurpleStoredImage *
purple_buddy_icons_find_account_icon(PurpleAccount *account)
{
	PurpleStoredImage *img;
	const char *account_icon_file;
	const char *dirname;
	char *path;
	guchar *data;
	size_t len;

	g_return_val_if_fail(account != NULL, NULL);

	if ((img = g_hash_table_lookup(pointer_icon_cache, account)))
	{
		return purple_imgstore_ref(img);
	}

	account_icon_file = purple_account_get_string(account, "buddy_icon", NULL);

	if (account_icon_file == NULL)
		return NULL;

	dirname = purple_buddy_icons_get_cache_dir();
	path = g_build_filename(dirname, account_icon_file, NULL);

	if (read_icon_file(path, &data, &len))
	{
		g_free(path);
		img = purple_buddy_icons_set_account_icon(account, data, len);
		return purple_imgstore_ref(img);
	}
	g_free(path);

	return NULL;
}

PurpleStoredImage *
purple_buddy_icons_set_account_icon(PurpleAccount *account,
                                    guchar *icon_data, size_t icon_len)
{
	PurpleStoredImage *old_img;
	PurpleStoredImage *img = NULL;
	char *old_icon;

	if (icon_data != NULL && icon_len > 0) {
		img = purple_buddy_icon_data_new(icon_data, icon_len);
	}

	old_icon = g_strdup(purple_account_get_string(account, "buddy_icon", NULL));
	if (img && purple_buddy_icons_is_caching())
	{
		const char *filename = purple_imgstore_get_filename(img);
		purple_account_set_string(account, "buddy_icon", filename);
		purple_account_set_int(account, "buddy_icon_timestamp", time(NULL));
		ref_filename(filename);
	}
	else
	{
		purple_account_set_string(account, "buddy_icon", NULL);
		purple_account_set_int(account, "buddy_icon_timestamp", 0);
	}
	unref_filename(old_icon);

	old_img = g_hash_table_lookup(pointer_icon_cache, account);

	if (img)
		g_hash_table_insert(pointer_icon_cache, account, img);
	else
		g_hash_table_remove(pointer_icon_cache, account);

	if (!purple_account_is_disconnected(account))
	{
		PurpleConnection *gc;
		PurplePluginProtocolInfo *prpl_info;

		gc = purple_account_get_connection(account);
		prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO(purple_connection_get_prpl(gc));

		if (prpl_info && prpl_info->set_buddy_icon)
			prpl_info->set_buddy_icon(gc, img);
	}

	if (old_img)
		purple_imgstore_unref(old_img);
	else if (old_icon)
	{
		/* The old icon may not have been loaded into memory.  In that
		 * case, we'll need to uncache the filename.  The filenames
		 * are ref-counted, so this is safe. */
		purple_buddy_icon_data_uncache_file(old_icon);
	}
	g_free(old_icon);

	return img;
}

time_t
purple_buddy_icons_get_account_icon_timestamp(PurpleAccount *account)
{
	time_t ret;

	g_return_val_if_fail(account != NULL, 0);

	ret = purple_account_get_int(account, "buddy_icon_timestamp", 0);

	/* This deals with migration cases. */
	if (ret == 0 && purple_account_get_string(account, "buddy_icon", NULL) != NULL)
	{
		ret = time(NULL);
		purple_account_set_int(account, "buddy_icon_timestamp", ret);
	}

	return ret;
}

gboolean
purple_buddy_icons_node_has_custom_icon(PurpleBListNode *node)
{
	g_return_val_if_fail(node != NULL, FALSE);

	return (purple_blist_node_get_string(node, "custom_buddy_icon") != NULL);
}

PurpleStoredImage *
purple_buddy_icons_node_find_custom_icon(PurpleBListNode *node)
{
	char *path;
	size_t len;
	guchar *data;
	PurpleStoredImage *img;
	const char *custom_icon_file, *dirname;

	g_return_val_if_fail(node != NULL, NULL);

	if ((img = g_hash_table_lookup(pointer_icon_cache, node)))
	{
		return purple_imgstore_ref(img);
	}

	custom_icon_file = purple_blist_node_get_string(node,
	                                                "custom_buddy_icon");

	if (custom_icon_file == NULL)
		return NULL;

	dirname = purple_buddy_icons_get_cache_dir();
	path = g_build_filename(dirname, custom_icon_file, NULL);

	if (read_icon_file(path, &data, &len))
	{
		g_free(path);
		img = purple_buddy_icons_node_set_custom_icon(node, data, len);
		return purple_imgstore_ref(img);
	}
	g_free(path);

	return NULL;
}

PurpleStoredImage *
purple_buddy_icons_node_set_custom_icon(PurpleBListNode *node,
                                        guchar *icon_data, size_t icon_len)
{
	char *old_icon;
	PurpleStoredImage *old_img;
	PurpleStoredImage *img = NULL;

	g_return_val_if_fail(node != NULL, NULL);

	if (!PURPLE_IS_CONTACT(node) &&
	    !PURPLE_IS_CHAT(node) &&
	    !PURPLE_IS_GROUP(node)) {
		return NULL;
	}

	old_img = g_hash_table_lookup(pointer_icon_cache, node);

	if (icon_data != NULL && icon_len > 0) {
		img = purple_buddy_icon_data_new(icon_data, icon_len);
	}

	old_icon = g_strdup(purple_blist_node_get_string(node,
	                                                 "custom_buddy_icon"));
	if (img && purple_buddy_icons_is_caching()) {
		const char *filename = purple_imgstore_get_filename(img);
		purple_blist_node_set_string(node, "custom_buddy_icon",
		                             filename);
		ref_filename(filename);
	} else {
		purple_blist_node_remove_setting(node, "custom_buddy_icon");
	}
	unref_filename(old_icon);

	if (img)
		g_hash_table_insert(pointer_icon_cache, node, img);
	else
		g_hash_table_remove(pointer_icon_cache, node);

	if (PURPLE_IS_CONTACT(node)) {
		PurpleBListNode *child;
		for (child = purple_blist_node_get_first_child(node);
		     child;
			 child = purple_blist_node_get_sibling_next(child))
		{
			PurpleBuddy *buddy;
			PurpleIMConversation *im;

			if (!PURPLE_IS_BUDDY(child))
				continue;

			buddy = (PurpleBuddy *)child;

			im = purple_conversations_find_im_with_account(purple_buddy_get_name(buddy), purple_buddy_get_account(buddy));
			if (im)
				purple_conversation_update(PURPLE_CONVERSATION(im), PURPLE_CONVERSATION_UPDATE_ICON);

			/* Is this call necessary anymore? Can the buddies
			 * themselves need updating when the custom buddy
			 * icon changes? */
			purple_blist_node_update((PurpleBListNode*)buddy);
		}
	} else if (PURPLE_IS_CHAT(node)) {
		PurpleChatConversation *chat = NULL;

		chat = purple_conversations_find_chat_with_account(purple_chat_get_name((PurpleChat*)node), purple_chat_get_account((PurpleChat*)node));
		if (chat) {
			purple_conversation_update(PURPLE_CONVERSATION(chat), PURPLE_CONVERSATION_UPDATE_ICON);
		}
	}

	purple_blist_node_update(node);

	if (old_img) {
		purple_imgstore_unref(old_img);
	} else if (old_icon) {
		/* The old icon may not have been loaded into memory.  In that
		 * case, we'll need to uncache the filename.  The filenames
		 * are ref-counted, so this is safe. */
		purple_buddy_icon_data_uncache_file(old_icon);
	}
	g_free(old_icon);

	return img;
}

PurpleStoredImage *
purple_buddy_icons_node_set_custom_icon_from_file(PurpleBListNode *node,
                                                  const gchar *filename)
{
	size_t len = 0;
	guchar *data = NULL;

	g_return_val_if_fail(node != NULL, NULL);

	if (!PURPLE_IS_CONTACT(node) &&
	    !PURPLE_IS_CHAT(node) &&
	    !PURPLE_IS_GROUP(node)) {
		return NULL;
	}

	if (filename != NULL) {
		if (!read_icon_file(filename, &data, &len)) {
			return NULL;
		}
	}

	return purple_buddy_icons_node_set_custom_icon(node, data, len);
}

static void
delete_buddy_icon_settings(PurpleBListNode *node, const char *setting_name)
{
	purple_blist_node_remove_setting(node, setting_name);

	if (purple_strequal(setting_name, "buddy_icon"))
	{
		purple_blist_node_remove_setting(node, "avatar_hash");
		purple_blist_node_remove_setting(node, "icon_checksum");
	}
}

void
_purple_buddy_icons_account_loaded_cb()
{
	const char *dirname = purple_buddy_icons_get_cache_dir();
	GList *cur;

	for (cur = purple_accounts_get_all(); cur != NULL; cur = cur->next)
	{
		PurpleAccount *account = cur->data;
		const char *account_icon_file = purple_account_get_string(account, "buddy_icon", NULL);

		if (account_icon_file != NULL)
		{
			char *path = g_build_filename(dirname, account_icon_file, NULL);
			if (!g_file_test(path, G_FILE_TEST_EXISTS))
			{
				purple_account_set_string(account, "buddy_icon", NULL);
			} else {
				ref_filename(account_icon_file);
			}
			g_free(path);
		}
	}
}

void
_purple_buddy_icons_blist_loaded_cb()
{
	PurpleBListNode *node = purple_blist_get_root();
	const char *dirname = purple_buddy_icons_get_cache_dir();

	while (node != NULL)
	{
		if (PURPLE_IS_BUDDY(node))
		{
			const char *filename;

			filename = purple_blist_node_get_string(node, "buddy_icon");
			if (filename != NULL)
			{
				char *path = g_build_filename(dirname, filename, NULL);
				if (!g_file_test(path, G_FILE_TEST_EXISTS))
				{
					purple_blist_node_remove_setting(node,
					                                 "buddy_icon");
					purple_blist_node_remove_setting(node,
					                                 "icon_checksum");
				}
				else
					ref_filename(filename);
				g_free(path);
			}
		}
		else if (PURPLE_IS_CONTACT(node) ||
		         PURPLE_IS_CHAT(node) ||
		         PURPLE_IS_GROUP(node))
		{
			const char *filename;

			filename = purple_blist_node_get_string(node, "custom_buddy_icon");
			if (filename != NULL)
			{
				char *path = g_build_filename(dirname, filename, NULL);
				if (!g_file_test(path, G_FILE_TEST_EXISTS))
				{
					purple_blist_node_remove_setting(node,
					                                 "custom_buddy_icon");
				}
				else
					ref_filename(filename);
				g_free(path);
			}
		}
		node = purple_blist_node_next(node, TRUE);
	}
}

void
purple_buddy_icons_set_caching(gboolean caching)
{
	icon_caching = caching;
}

gboolean
purple_buddy_icons_is_caching(void)
{
	return icon_caching;
}

void
purple_buddy_icons_set_cache_dir(const char *dir)
{
	g_return_if_fail(dir != NULL);

	g_free(cache_dir);
	cache_dir = g_strdup(dir);
}

const char *
purple_buddy_icons_get_cache_dir(void)
{
	return cache_dir;
}

void *
purple_buddy_icons_get_handle()
{
	static int handle;

	return &handle;
}

void
purple_buddy_icons_init()
{
	account_cache = g_hash_table_new_full(
		g_direct_hash, g_direct_equal,
		NULL, (GFreeFunc)g_hash_table_destroy);

	icon_data_cache = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                        g_free, NULL);
	icon_file_cache = g_hash_table_new_full(g_str_hash, g_str_equal,
	                                        g_free, NULL);
	pointer_icon_cache = g_hash_table_new(g_direct_hash, g_direct_equal);

    if (!cache_dir)
		cache_dir = g_build_filename(purple_user_dir(), "icons", NULL);

	purple_signal_connect(purple_imgstore_get_handle(), "image-deleting",
	                      purple_buddy_icons_get_handle(),
	                      G_CALLBACK(image_deleting_cb), NULL);
}

void
purple_buddy_icons_uninit()
{
	purple_signals_disconnect_by_handle(purple_buddy_icons_get_handle());

	g_hash_table_destroy(account_cache);
	g_hash_table_destroy(icon_data_cache);
	g_hash_table_destroy(icon_file_cache);
	g_hash_table_destroy(pointer_icon_cache);
	g_free(cache_dir);

	cache_dir = NULL;
}

void purple_buddy_icon_get_scale_size(PurpleBuddyIconSpec *spec, int *width, int *height)
{
	int new_width, new_height;

	new_width = *width;
	new_height = *height;

	if (*width < spec->min_width)
		new_width = spec->min_width;
	else if (*width > spec->max_width)
		new_width = spec->max_width;

	if (*height < spec->min_height)
		new_height = spec->min_height;
	else if (*height > spec->max_height)
		new_height = spec->max_height;

	/* preserve aspect ratio */
	if ((double)*height * (double)new_width >
		(double)*width * (double)new_height) {
			new_width = 0.5 + (double)*width * (double)new_height / (double)*height;
	} else {
			new_height = 0.5 + (double)*height * (double)new_width / (double)*width;
	}

	*width = new_width;
	*height = new_height;
}
