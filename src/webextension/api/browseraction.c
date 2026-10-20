/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © 2022 Jan-Michael Brummer <jan.brummer@tabos.org>
 *
 *  This file is part of Epiphany.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "browseraction.h"

#include <time.h>

#include "ephy-embed-utils.h"
#include "ephy-json-utils.h"
#include "ephy-shell.h"
#include "ephy-window.h"

static void
browseraction_handler_set_badge_text (EphyWebExtensionSender *sender,
                                      const char             *method_name,
                                      JsonArray              *args,
                                      GTask                  *task)
{
  EphyWebExtensionManager *manager = ephy_web_extension_manager_get_default ();
  JsonObject *details;
  const char *badge_text = NULL;
  gint64 tab_id = -1;
  gint64 window_id = -1;

  details = ephy_json_array_get_object (args, 0);
  if (!details) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "tabs.setBadgeText(): Missing details");
    return;
  }

  badge_text = ephy_json_object_get_string (details, "text");
  if (!badge_text) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeText(): Missing text");
    return;
  }

  tab_id = ephy_json_object_get_int (details, "tabId");
  window_id = ephy_json_object_get_int (details, "windowId");
  if (tab_id != -1 && window_id != -1) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeText(): tabId and windowId defined. Not supported");
    return;
  }

  /* Currently only Firefox can handle them, skip it for the moment */
  if (tab_id != -1 || window_id != -1) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeText(): tabId or windowId defined. Not supported");
    return;
  }

  ephy_web_extension_manager_browseraction_set_badge_text (manager, sender->extension, badge_text);
  g_task_return_pointer (task, NULL, NULL);
}

static void
browseraction_handler_set_badge_background_color (EphyWebExtensionSender *sender,
                                                  const char             *method_name,
                                                  JsonArray              *args,
                                                  GTask                  *task)
{
  EphyWebExtensionManager *manager = ephy_web_extension_manager_get_default ();
  JsonObject *details;
  const char *badge_color = NULL;
  gint64 tab_id = -1;
  gint64 window_id = -1;
  GdkRGBA color;

  details = ephy_json_array_get_object (args, 0);
  if (!details) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "tabs.setBadgeBackgroundColor(): Missing details");
    return;
  }

  badge_color = ephy_json_object_get_string (details, "color");
  if (!badge_color) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeBackgroundColor(): Missing badge color");
    return;
  }

  tab_id = ephy_json_object_get_int (details, "tabId");
  window_id = ephy_json_object_get_int (details, "windowId");
  if (tab_id != -1 && window_id != -1) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeBackgroundColor(): tabId and windowId defined. Not supported");
    return;
  }

  /* Currently only Firefox can handle them, skip it for the moment */
  if (tab_id != -1 || window_id != -1) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeBackgroundColor(): tabId or windowId defined. Not supported");
    return;
  }

  if (!gdk_rgba_parse (&color, badge_color)) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setBadgeBackgroundColor(): Failed to parse color");
    return;
  }

  ephy_web_extension_manager_browseraction_set_badge_background_color (manager, sender->extension, &color);
  g_task_return_pointer (task, NULL, NULL);
}

/*
 * An extension can give its button a title of its own, and an icon of its
 * own.
 *
 * The icon is either a path inside the extension, or a data URL. A path
 * is read out of the extension the same way the browser serves it, so it
 * can only be a file the extension already has.
 */
static GdkPixbuf *
pixbuf_from_data_url (const char *url)
{
  g_autofree guchar *decoded = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GInputStream) stream = NULL;
  GdkPixbuf *pixbuf;
  const char *comma;
  gsize length;

  if (!g_str_has_prefix (url, "data:"))
    return NULL;

  comma = strchr (url, ',');
  if (!comma)
    return NULL;

  if (!g_str_has_prefix (comma + 1, "base64,"))
    return NULL;

  decoded = g_base64_decode (comma + strlen (",base64,"), &length);
  if (!decoded)
    return NULL;

  bytes = g_bytes_new_take (decoded, length);
  stream = g_memory_input_stream_new_from_bytes (bytes);

  pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, NULL);
  if (!pixbuf)
    return NULL;

  return pixbuf;
}

static GdkPixbuf *
pixbuf_from_path (EphyWebExtension *extension,
                  const char       *path)
{
  gconstpointer data = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GInputStream) stream = NULL;
  gsize length = 0;
  GdkPixbuf *pixbuf;

  data = ephy_web_extension_get_resource (extension, path, &length);
  if (!data)
    return NULL;

  bytes = g_bytes_new_static (data, length);
  stream = g_memory_input_stream_new_from_bytes (bytes);

  pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, NULL);
  if (!pixbuf)
    return NULL;

  return pixbuf;
}

static void
browseraction_handler_set_title (EphyWebExtensionSender *sender,
                                 const char             *method_name,
                                 JsonArray              *args,
                                 GTask                  *task)
{
  EphyWebExtensionManager *manager = ephy_web_extension_manager_get_default ();
  JsonObject *details;
  const char *title;

  details = ephy_json_array_get_object (args, 0);
  if (!details) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setTitle(): Missing details");
    return;
  }

  title = ephy_json_object_get_string (details, "title");
  if (!title) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setTitle(): Missing title");
    return;
  }

  ephy_web_extension_manager_browseraction_set_title (manager, sender->extension, title);
  g_task_return_pointer (task, NULL, NULL);
}

static void
browseraction_handler_set_icon (EphyWebExtensionSender *sender,
                                const char             *method_name,
                                JsonArray              *args,
                                GTask                  *task)
{
  EphyWebExtensionManager *manager = ephy_web_extension_manager_get_default ();
  JsonObject *details;
  const char *path = NULL;
  const char *image_details = NULL;

  GdkPixbuf *pixbuf = NULL;

  details = ephy_json_array_get_object (args, 0);
  if (!details) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT, "browserAction.setIcon(): Missing details");
    return;
  }

  path = ephy_json_object_get_string (details, "path");
  image_details = ephy_json_object_get_string (details, "imageDetails");

  if (path)
    pixbuf = pixbuf_from_path (sender->extension, path);
  else if (image_details)
    pixbuf = pixbuf_from_data_url (image_details);

  if (!pixbuf) {
    g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_INVALID_ARGUMENT,
                             "browserAction.setIcon(): Could not read the icon");
    return;
  }

  ephy_web_extension_manager_browseraction_set_icon (manager, sender->extension, pixbuf);
  g_object_unref (pixbuf);

  g_task_return_pointer (task, NULL, NULL);
}

static EphyWebExtensionApiHandler browseraction_handlers[] = {
  {"setBadgeText", browseraction_handler_set_badge_text},
  {"setBadgeBackgroundColor", browseraction_handler_set_badge_background_color},
  {"setTitle", browseraction_handler_set_title},
  {"setIcon", browseraction_handler_set_icon},
};

void
ephy_web_extension_api_browseraction_handler (EphyWebExtensionSender *sender,
                                              const char             *method_name,
                                              JsonArray              *args,
                                              GTask                  *task)
{
  for (guint idx = 0; idx < G_N_ELEMENTS (browseraction_handlers); idx++) {
    EphyWebExtensionApiHandler handler = browseraction_handlers[idx];

    if (g_strcmp0 (handler.name, method_name) == 0) {
      handler.execute (sender, method_name, args, task);
      return;
    }
  }

  g_warning ("%s(): '%s' not implemented by SparkyBin Browser!", __FUNCTION__, method_name);
  g_task_return_new_error (task, WEB_EXTENSION_ERROR, WEB_EXTENSION_ERROR_NOT_IMPLEMENTED, "Not Implemented");
}
