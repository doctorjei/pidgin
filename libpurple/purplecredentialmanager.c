/*
 * Purple - Internet Messaging Library
 * Copyright (C) Pidgin Developers <devel@pidgin.im>
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

#include <glib/gi18n-lib.h>

#include "purplecredentialmanager.h"

#include "core.h"
#include "debug.h"
#include "prefs.h"
#include "purplenoopcredentialprovider.h"
#include "purpleprivate.h"
#include "util.h"

enum {
	SIG_PROVIDER_REGISTERED,
	SIG_PROVIDER_UNREGISTERED,
	SIG_ACTIVE_PROVIDER_CHANGED,
	N_SIGNALS,
};
static guint signals[N_SIGNALS] = {0, };

typedef struct {
	GHashTable *providers;

	PurpleCredentialProvider *active_provider;
} PurpleCredentialManagerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(PurpleCredentialManager, purple_credential_manager,
                           G_TYPE_OBJECT);

static PurpleCredentialManager *default_manager = NULL;

/******************************************************************************
 * Async Callbacks
 *****************************************************************************/
static void
purple_credential_manager_read_password_callback(GObject *obj,
                                                 GAsyncResult *res,
                                                 gpointer data)
{
	PurpleCredentialProvider *provider = PURPLE_CREDENTIAL_PROVIDER(obj);
	GError *error = NULL;
	GTask *task = G_TASK(data);
	gchar *password = NULL;

	password = purple_credential_provider_read_password_finish(provider, res,
	                                                           &error);

	if(error != NULL) {
		g_task_return_error(task, error);
	} else {
		g_task_return_pointer(task, password, g_free);
	}

	/* Clean up our initial reference to the task. */
	g_object_unref(G_OBJECT(task));
}

static void
purple_credential_manager_write_password_callback(GObject *obj,
                                                  GAsyncResult *res,
                                                  gpointer data)
{
	PurpleCredentialProvider *provider = PURPLE_CREDENTIAL_PROVIDER(obj);
	GError *error = NULL;
	GTask *task = G_TASK(data);
	gboolean ret = FALSE;

	ret = purple_credential_provider_write_password_finish(provider, res,
	                                                       &error);

	if(error != NULL) {
		g_task_return_error(task, error);
	} else {
		g_task_return_boolean(task, ret);
	}

	/* Clean up our initial reference to the task. */
	g_object_unref(G_OBJECT(task));
}

static void
purple_credential_manager_clear_password_callback(GObject *obj,
                                                  GAsyncResult *res,
                                                  gpointer data)
{
	PurpleCredentialProvider *provider = PURPLE_CREDENTIAL_PROVIDER(obj);
	GError *error = NULL;
	GTask *task = G_TASK(data);
	gboolean ret = FALSE;

	ret = purple_credential_provider_clear_password_finish(provider, res,
	                                                       &error);

	if(error != NULL) {
		g_task_return_error(task, error);
	} else {
		g_task_return_boolean(task, ret);
	}

	/* Clean up our initial reference to the task. */
	g_object_unref(G_OBJECT(task));
}

/******************************************************************************
 * Purple Callbacks
 *****************************************************************************/
static void
purple_credential_manager_core_init_cb(gpointer data) {
	PurpleCredentialManager *manager = PURPLE_CREDENTIAL_MANAGER(data);
	PurpleCredentialManagerPrivate *priv = NULL;

	priv = purple_credential_manager_get_instance_private(manager);
	if(!PURPLE_IS_CREDENTIAL_PROVIDER(priv->active_provider)) {
		purple_notify_error(NULL, _("Credential Manager"),
		                    _("Failed to load the selected credential "
		                      "provider."),
		                    _("Check your system configuration or select "
		                      "another one in the preferences dialog."), NULL);
	}
}

/******************************************************************************
 * GObject Implementation
 *****************************************************************************/
static void
purple_credential_manager_finalize(GObject *obj) {
	PurpleCredentialManager *manager = NULL;
	PurpleCredentialManagerPrivate *priv = NULL;

	manager = PURPLE_CREDENTIAL_MANAGER(obj);
	priv = purple_credential_manager_get_instance_private(manager);

	g_clear_pointer(&priv->providers, g_hash_table_destroy);
	g_clear_object(&priv->active_provider);

	G_OBJECT_CLASS(purple_credential_manager_parent_class)->finalize(obj);
}

static void
purple_credential_manager_init(PurpleCredentialManager *manager) {
	PurpleCredentialManagerPrivate *priv = NULL;
	PurpleCredentialProvider *noop = NULL;

	priv = purple_credential_manager_get_instance_private(manager);

	priv->providers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
	                                        g_object_unref);

	noop = purple_noop_credential_provider_new();
	purple_credential_manager_register_provider(manager, noop, NULL);
	g_object_unref(G_OBJECT(noop));

	/* Connect to the core-initialized signal so we can alert the user if we
	 * were unable to find their credential provider.
	 */
	purple_signal_connect(purple_get_core(), "core-initialized", manager,
	                      PURPLE_CALLBACK(purple_credential_manager_core_init_cb),
	                      manager);
}

static void
purple_credential_manager_class_init(PurpleCredentialManagerClass *klass) {
	GObjectClass *obj_class = G_OBJECT_CLASS(klass);

	obj_class->finalize = purple_credential_manager_finalize;

	/**
	 * PurpleCredentialManager::provider-registered:
	 * @manager: The #PurpleCredentialManager instance.
	 * @provider: The #PurpleCredentialProvider that was registered.
	 *
	 * Emitted after @provider has been registered in @manager.
	 *
	 * Since: 3.0.0
	 */
	signals[SIG_PROVIDER_REGISTERED] = g_signal_new(
		"provider-registered",
		G_OBJECT_CLASS_TYPE(klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET(PurpleCredentialManagerClass, provider_registered),
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		PURPLE_TYPE_CREDENTIAL_PROVIDER);

	/**
	 * PurpleCredentialManager::provider-unregistered:
	 * @manager: The #PurpleCredentialManager instance.
	 * @provider: The #PurpleCredentialProvider that was unregistered.
	 *
	 * Emitted after @provider has been unregistered from @manager.
	 *
	 * Since: 3.0.0
	 */
	signals[SIG_PROVIDER_UNREGISTERED] = g_signal_new(
		"provider-unregistered",
		G_OBJECT_CLASS_TYPE(klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET(PurpleCredentialManagerClass, provider_unregistered),
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		1,
		PURPLE_TYPE_CREDENTIAL_PROVIDER);

	/**
	 * PurpleCredentialManager::active-provider-changed:
	 * @manager: The #PurpleCredentialManager instance.
	 * @old: The #PurpleCredentialProvider that was previously active.
	 * @current: The #PurpleCredentialProvider that is now currently active.
	 *
	 * Emitted after @provider has become the active provider for @manager.
	 *
	 * Since: 3.0.0
	 */
	signals[SIG_ACTIVE_PROVIDER_CHANGED] = g_signal_new(
		"active-provider-changed",
		G_OBJECT_CLASS_TYPE(klass),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET(PurpleCredentialManagerClass, active_provider_changed),
		NULL,
		NULL,
		NULL,
		G_TYPE_NONE,
		2,
		PURPLE_TYPE_CREDENTIAL_PROVIDER,
		PURPLE_TYPE_CREDENTIAL_PROVIDER);
}

/******************************************************************************
 * Private API
 *****************************************************************************/
void
purple_credential_manager_startup(void) {
	if(default_manager == NULL) {
		default_manager = g_object_new(PURPLE_TYPE_CREDENTIAL_MANAGER, NULL);
	}
}

void
purple_credential_manager_shutdown(void) {
	if(PURPLE_IS_CREDENTIAL_MANAGER(default_manager)) {
		PurpleCredentialManagerPrivate *priv = NULL;
		guint size = 0;

		priv = purple_credential_manager_get_instance_private(default_manager);

		size = g_hash_table_size(priv->providers);
		if(size > 0) {
			g_warning("purple_credential_manager_shutdown called while %d "
				      "providers were still registered. Skipping shutdown",
				      size);
		} else {
			g_clear_object(&default_manager);
		}
	}
}

/******************************************************************************
 * Public API
 *****************************************************************************/
PurpleCredentialManager *
purple_credential_manager_get_default(void) {
	return default_manager;
}

gboolean
purple_credential_manager_register_provider(PurpleCredentialManager *manager,
                                            PurpleCredentialProvider *provider,
                                            GError **error)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	const gchar *id = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);
	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_PROVIDER(provider), FALSE);

	if(!purple_credential_provider_is_valid(provider, error)) {
		/* purple_credential_provider_is_valid sets the error on failure. */

		return FALSE;
	}

	priv = purple_credential_manager_get_instance_private(manager);

	id = purple_credential_provider_get_id(provider);
	if(g_hash_table_lookup(priv->providers, id) != NULL) {
		g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		            _("provider %s is already registered"), id);

		return FALSE;
	}

	g_hash_table_insert(priv->providers, g_strdup(id), g_object_ref(provider));

	g_signal_emit(G_OBJECT(manager), signals[SIG_PROVIDER_REGISTERED], 0,
	              provider);

	/* If we don't currently have an active provider, check if the newly
	 * registered provider has the id of the stored provider in preferences.
	 * If it is, go ahead and make it the active provider.
	 */
	if(!PURPLE_IS_CREDENTIAL_PROVIDER(priv->active_provider)) {
		const gchar *wanted = NULL;

		wanted = purple_prefs_get_string("/purple/credentials/active-provider");

		if(purple_strequal(wanted, id)) {
			purple_credential_manager_set_active_provider(manager, id, error);
		}
	}

	return TRUE;
}

gboolean
purple_credential_manager_unregister_provider(PurpleCredentialManager *manager,
                                              PurpleCredentialProvider *provider,
                                              GError **error)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	const gchar *id = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);
	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_PROVIDER(provider), FALSE);

	priv = purple_credential_manager_get_instance_private(manager);
	id = purple_credential_provider_get_id(provider);

	if(provider == priv->active_provider) {
		g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		            _("provider %s is currently in use"), id);

		return FALSE;
	}

	if(g_hash_table_remove(priv->providers, id)) {
		g_signal_emit(G_OBJECT(manager), signals[SIG_PROVIDER_UNREGISTERED], 0,
		              provider);

		return TRUE;
	}

	g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
	            _("provider %s is not registered"), id);

	return FALSE;
}

gboolean
purple_credential_manager_set_active_provider(PurpleCredentialManager *manager,
                                              const gchar *id, GError **error)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	PurpleCredentialProvider *old = NULL, *provider = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);

	priv = purple_credential_manager_get_instance_private(manager);

	/* First look up the new provider if we're given one. */
	if(id != NULL) {
		provider = g_hash_table_lookup(priv->providers, id);
		if(!PURPLE_IS_CREDENTIAL_PROVIDER(provider)) {
			g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
			            "no credential provider found with id %s", id);

			return FALSE;
		}
	}

	if(PURPLE_IS_CREDENTIAL_PROVIDER(priv->active_provider)) {
		old = PURPLE_CREDENTIAL_PROVIDER(g_object_ref(priv->active_provider));
	}

	if(g_set_object(&priv->active_provider, provider)) {
		g_signal_emit(G_OBJECT(manager), signals[SIG_ACTIVE_PROVIDER_CHANGED],
		              0, old, priv->active_provider);
	}

	g_clear_object(&old);

	purple_debug_info("credential-manager", "set active provider to '%s'", id);

	return TRUE;
}

PurpleCredentialProvider *
purple_credential_manager_get_active_provider(PurpleCredentialManager *manager)
{
	PurpleCredentialManagerPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), NULL);

	priv = purple_credential_manager_get_instance_private(manager);

	return priv->active_provider;
}

void
purple_credential_manager_read_password_async(PurpleCredentialManager *manager,
                                              PurpleAccount *account,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer data)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	GTask *task = NULL;

	g_return_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager));
	g_return_if_fail(PURPLE_IS_ACCOUNT(account));

	priv = purple_credential_manager_get_instance_private(manager);

	task = g_task_new(manager, cancellable, callback, data);

	if(priv->active_provider == NULL) {
		GError *error = NULL;

		error = g_error_new_literal(PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		                            _("can not read password, no active "
		                              "credential provider"));

		g_task_return_error(task, error);
		g_object_unref(G_OBJECT(task));

		return;
	}

	purple_credential_provider_read_password_async(priv->active_provider,
	                                               account,
	                                               cancellable,
	                                               purple_credential_manager_read_password_callback,
	                                               task);
}

gchar *
purple_credential_manager_read_password_finish(PurpleCredentialManager *manager,
                                               GAsyncResult *result,
                                               GError **error)
{
	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), NULL);

	return g_task_propagate_pointer(G_TASK(result), error);
}

void
purple_credential_manager_write_password_async(PurpleCredentialManager *manager,
                                               PurpleAccount *account,
                                               const gchar *password,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer data)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	GTask *task = NULL;

	g_return_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager));
	g_return_if_fail(PURPLE_IS_ACCOUNT(account));

	priv = purple_credential_manager_get_instance_private(manager);

	task = g_task_new(manager, cancellable, callback, data);

	if(!purple_account_get_remember_password(account)) {
		GError *error = NULL;
		const gchar *name = purple_account_get_username(account);

		error = g_error_new(PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		                    _("account \"%s\" is not marked to be stored"),
		                    name);

		g_task_return_error(task, error);
		g_object_unref(G_OBJECT(task));

		return;
	}

	if(priv->active_provider == NULL) {
		GError *error = NULL;

		error = g_error_new_literal(PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		                            _("can not write password, no active "
		                              "credential provider"));

		g_task_return_error(task, error);
		g_object_unref(G_OBJECT(task));

		return;
	}

	purple_credential_provider_write_password_async(priv->active_provider,
	                                                account, password,
	                                                cancellable,
	                                                purple_credential_manager_write_password_callback,
	                                                task);
}

gboolean
purple_credential_manager_write_password_finish(PurpleCredentialManager *manager,
                                                GAsyncResult *result,
                                                GError **error)
{
	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);

	return g_task_propagate_boolean(G_TASK(result), error);
}

void
purple_credential_manager_clear_password_async(PurpleCredentialManager *manager,
                                               PurpleAccount *account,
                                               GCancellable *cancellable,
                                               GAsyncReadyCallback callback,
                                               gpointer data)
{
	PurpleCredentialManagerPrivate *priv = NULL;
	GTask *task = NULL;

	g_return_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager));
	g_return_if_fail(PURPLE_IS_ACCOUNT(account));

	priv = purple_credential_manager_get_instance_private(manager);

	task = g_task_new(manager, cancellable, callback, data);

	if(priv->active_provider == NULL) {
		GError *error = NULL;

		error = g_error_new_literal(PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		                            _("can not clear password, no active "
		                              "credential provider"));

		g_task_return_error(task, error);
		g_object_unref(G_OBJECT(task));

		return;
	}

	purple_credential_provider_clear_password_async(priv->active_provider,
	                                                account,
	                                                cancellable,
	                                                purple_credential_manager_clear_password_callback,
	                                                task);
}

gboolean
purple_credential_manager_clear_password_finish(PurpleCredentialManager *manager,
                                                GAsyncResult *result,
                                                GError **error)
{
	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);

	return g_task_propagate_boolean(G_TASK(result), error);
}

PurpleRequestFields *
purple_credential_manager_read_settings(PurpleCredentialManager *manager,
                                        GError **error)
{
	PurpleCredentialManagerPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);

	priv = purple_credential_manager_get_instance_private(manager);

	if(priv->active_provider == NULL) {
		g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		            _("can not read settings, no active credential provider"));

		return NULL;
	}

	return purple_credential_provider_read_settings(priv->active_provider);
}

gboolean
purple_credential_manager_write_settings(PurpleCredentialManager *manager,
                                         PurpleRequestFields *fields,
                                         GError **error)
{
	PurpleCredentialManagerPrivate *priv = NULL;

	g_return_val_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager), FALSE);

	priv = purple_credential_manager_get_instance_private(manager);

	if(priv->active_provider == NULL) {
		g_set_error(error, PURPLE_CREDENTIAL_MANAGER_DOMAIN, 0,
		            _("can not write settings, no active credential provider"));

		return FALSE;
	}

	return purple_credential_provider_write_settings(priv->active_provider,
	                                                 fields);
}

void
purple_credential_manager_foreach_provider(PurpleCredentialManager *manager,
                                           PurpleCredentialManagerForeachFunc func,
                                           gpointer data)
{
	GHashTableIter iter;
	PurpleCredentialManagerPrivate *priv = NULL;
	gpointer value;

	g_return_if_fail(PURPLE_IS_CREDENTIAL_MANAGER(manager));
	g_return_if_fail(func != NULL);

	priv = purple_credential_manager_get_instance_private(manager);

	g_hash_table_iter_init(&iter, priv->providers);
	while(g_hash_table_iter_next(&iter, NULL, &value)) {
		func(PURPLE_CREDENTIAL_PROVIDER(value), data);
	}
}