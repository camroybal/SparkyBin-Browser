/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil, c-basic-offset: 2 -*- */
/*
 *  Copyright © 2026 The SparkyBin Browser contributors
 *
 *  This file is part of SparkyBin Browser.
 *
 *  SparkyBin Browser is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  SparkyBin Browser is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SparkyBin Browser.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "permissions.h"

#include "ephy-web-extension-manager.h"

/*
 * The permissions an extension was installed with. Asking whether it has
 * one is a question about the manifest, so it can be answered.
 *
 * Asking for one that it does not have cannot: the browser has no way to
 * ask the user, and an extension that asks for a permission at runtime is
 * asking for one it was not installed with.
 */

static gboolean
has_permission (EphyWebExtension *extension,
                const char       *permission)
{
  return ephy_web_extension_has_permission (extension, permission);
}

static gboolean
has_origin (EphyWebExtension *extension,
            const char       *origin)
{
  const char * const *host_permissions;

  host_permissions = ephy_web_extension_get_host_permissions (extension);

  for (guint i = 0; host_permissions && host_permissions[i]; i++) {
    if (g_strcmp0 (host_permissions[i], origin) == 0)
      return TRUE;
  }

  return FALSE;
}

static void
permissions_handler_contains (EphyWebExtensionSender *sender,
                              const char             *method_name,
                              JsonArray              *args,
                              GTask                  *task)
{
  JsonObject *permissions;
  JsonArray *array;
  gboolean found = TRUE;

  if (json_array_get_length (args) == 0 ||
      !json_array_get_object_element (args, 0)) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT,
                             "permissions.contains(): Missing permissions argument");
    return;
  }

  permissions = json_array_get_object_element (args, 0);

  array = json_object_has_member (permissions, "permissions")
            ? json_object_get_array_member (permissions, "permissions")
            : NULL;
  if (array) {
    for (guint i = 0; i < json_array_get_length (array); i++) {
      const char *permission = json_array_get_string_element (array, i);

      if (!permission || !has_permission (sender->extension, permission)) {
        found = FALSE;
        break;
      }
    }
  }

  array = json_object_has_member (permissions, "origins")
            ? json_object_get_array_member (permissions, "origins")
            : NULL;
  if (found && array) {
    for (guint i = 0; i < json_array_get_length (array); i++) {
      const char *origin = json_array_get_string_element (array, i);

      if (!origin || !has_origin (sender->extension, origin)) {
        found = FALSE;
        break;
      }
    }
  }

  g_task_return_boolean (task, found);
}

static void
permissions_handler_get_all (EphyWebExtensionSender *sender,
                             const char             *method_name,
                             JsonArray              *args,
                             GTask                  *task)
{
  g_autoptr (JsonBuilder) builder = json_builder_new ();
  g_autoptr (JsonNode) root = NULL;
  g_autoptr (GPtrArray) permissions = NULL;
  const char * const *host_permissions;
  g_autofree char *own_origin = NULL;

  permissions = ephy_web_extension_get_permissions (sender->extension);
  host_permissions = ephy_web_extension_get_host_permissions (sender->extension);

  /* The extension's own origin is always allowed, but it was not asked
   * for, so it is not in the list of origins. */
  own_origin = g_strdup_printf ("ephy-webextension://%s/*",
                                ephy_web_extension_get_guid (sender->extension));

  json_builder_begin_object (builder);

  json_builder_set_member_name (builder, "permissions");
  json_builder_begin_array (builder);
  for (guint i = 0; i < permissions->len; i++)
    json_builder_add_string_value (builder, g_ptr_array_index (permissions, i));
  json_builder_end_array (builder);

  json_builder_set_member_name (builder, "origins");
  json_builder_begin_array (builder);
  for (guint i = 0; host_permissions && host_permissions[i]; i++) {
    if (g_strcmp0 (host_permissions[i], own_origin) == 0)
      continue;
    json_builder_add_string_value (builder, host_permissions[i]);
  }
  json_builder_end_array (builder);

  json_builder_end_object (builder);

  root = json_builder_get_root (builder);

  g_task_return_pointer (task, json_to_string (root, FALSE), g_free);
}

static void
permissions_handler_request (EphyWebExtensionSender *sender,
                              const char             *method_name,
                              JsonArray              *args,
                              GTask                  *task)
{
  g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_NOT_IMPLEMENTED,
                           "permissions.request(): Not Implemented");
}

static void
permissions_handler_remove (EphyWebExtensionSender *sender,
                             const char             *method_name,
                             JsonArray              *args,
                             GTask                  *task)
{
  g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_NOT_IMPLEMENTED,
                           "permissions.remove(): Not Implemented");
}

static EphyWebExtensionApiHandler permissions_handlers[] = {
  {"contains", permissions_handler_contains},
  {"getAll", permissions_handler_get_all},
  {"remove", permissions_handler_remove},
  {"request", permissions_handler_request},
};

void
ephy_web_extension_api_permissions_handler (EphyWebExtensionSender *sender,
                                            const char             *method_name,
                                            JsonArray              *args,
                                            GTask                  *task)
{
  for (guint idx = 0; idx < G_N_ELEMENTS (permissions_handlers); idx++) {
    EphyWebExtensionApiHandler handler = permissions_handlers[idx];

    if (g_strcmp0 (handler.name, method_name) == 0) {
      handler.execute (sender, method_name, args, task);
      return;
    }
  }

  g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_NOT_IMPLEMENTED,
                           "permissions.%s(): Not Implemented", method_name);
}
