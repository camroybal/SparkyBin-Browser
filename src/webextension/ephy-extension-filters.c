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

#include "ephy-extension-filters.h"

#include <string.h>

#include "ephy-debug.h"
#include "ephy-file-helpers.h"
#include "ephy-filters-compiler.h"
#include "ephy-prefs.h"
#include "ephy-settings.h"

/*
 * The filter lists that come with an extension.
 *
 * An ad blocker has lists of the things it stops, and they are in the
 * extension itself. The browser can use them: it compiles them into the
 * rules that WebKit's content blockers understand, and puts them on the
 * pages. A request that matches one of them does not happen.
 *
 * This is how Safari's uBlock Origin works. There is no way for the
 * extension to stop a request by itself, because WebKit does not let
 * anything cancel a request once it has started.
 */

struct _EphyExtensionFilters {
  GObject parent_instance;

  GPtrArray *filters; /* WebKitUserContentFilter *, the compiled rules */
  WebKitUserContentFilterStore *store; /* where the compiled rules are kept */
};

G_DEFINE_FINAL_TYPE (EphyExtensionFilters, ephy_extension_filters, G_TYPE_OBJECT)

static void
ephy_extension_filters_finalize (GObject *object)
{
  EphyExtensionFilters *self = EPHY_EXTENSION_FILTERS (object);

  g_clear_pointer (&self->filters, g_ptr_array_unref);
  g_clear_object (&self->store);

  G_OBJECT_CLASS (ephy_extension_filters_parent_class)->finalize (object);
}

static void
ephy_extension_filters_class_init (EphyExtensionFiltersClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ephy_extension_filters_finalize;
}

static void
ephy_extension_filters_init (EphyExtensionFilters *self)
{
  self->filters = g_ptr_array_new_with_free_func ((GDestroyNotify) webkit_user_content_filter_unref);
}

/**
 * ephy_extension_filters_new:
 *
 * Creates the place where the filter lists that come with an extension
 * are kept.
 *
 * Returns: (transfer full): the new object
 */
EphyExtensionFilters *
ephy_extension_filters_new (void)
{
  return g_object_new (EPHY_TYPE_EXTENSION_FILTERS, NULL);
}

typedef struct {
  EphyExtensionFilters *self;
  char *identifier;
  char *name;
  char *text;
  gsize length;
  char *json;
  EphyFiltersCompileStats stats;
} CompileData;

static void
compile_data_free (CompileData *data)
{
  g_clear_object (&data->self);
  g_free (data->identifier);
  g_free (data->name);
  g_free (data->text);
  g_free (data->json);
  g_free (data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CompileData, compile_data_free)

static void
filter_saved_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (CompileData) data = user_data;
  g_autoptr (WebKitUserContentFilter) filter = NULL;
  g_autoptr (GError) error = NULL;

  filter = webkit_user_content_filter_store_save_finish (WEBKIT_USER_CONTENT_FILTER_STORE (source),
                                                          result, &error);
  if (!filter) {
    if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("The filter list '%s' cannot be used: %s", data->identifier, error->message);
    return;
  }

  LOG ("The filter list '%s' is ready", data->identifier);

  g_ptr_array_add (data->self->filters, g_steal_pointer (&filter));
}

static void extension_filter_load_cb (GObject *source, GAsyncResult *result, gpointer user_data);

/**
 * ephy_extension_filters_add:
 * @self: the filters
 * @name: the name of the list
 * @text: the filters, in the Adblock Plus format
 * @length: how long the filters are
 *
 * Adds a list of filters and compiles it.
 *
 * Returns: %TRUE if the list was accepted
 */
gboolean
ephy_extension_filters_add (EphyExtensionFilters *self,
                            const char           *name,
                            const char           *text,
                            gsize                 length)
{
  CompileData *data;

  if (!text || !length)
    return FALSE;

  /* The store is made when it is first needed, because making one looks
   * at what is in the directory. It lives next to the browser's own
   * compiled filters. */
  if (!self->store) {
    g_autofree char *cache_dir = ephy_default_cache_dir ();
    g_autofree char *filters_dir = g_build_filename (cache_dir, "adblock", NULL);
    g_autofree char *saved_filters_dir = g_build_filename (filters_dir, "compiled", NULL);

    g_mkdir_with_parents (saved_filters_dir, 0700);
    self->store = webkit_user_content_filter_store_new (saved_filters_dir);
  }

  data = g_new0 (CompileData, 1);
  data->self = g_object_ref (self);
  data->name = g_strdup (name);
  data->text = g_strndup (text, length);
  data->length = length;
  data->identifier = g_compute_checksum_for_string (G_CHECKSUM_SHA256, text, length);

  /* Try to reuse an already compiled list first. */
  webkit_user_content_filter_store_load (self->store, data->identifier, NULL,
                                         extension_filter_load_cb, data);

  return TRUE;
}

/**
 * compile_thread:
 */
static void
compile_thread (GTask        *task,
                gpointer      source_object,
                gpointer      task_data,
                GCancellable *cancellable)
{
  CompileData *data = task_data;
  g_autoptr (GError) error = NULL;
  EphyFiltersCompileStats stats = { 0 };
  char *json;

  json = ephy_filters_compile_abp (data->text, &stats, &error);
  if (!json) {
    if (error)
      g_task_return_error (task, g_steal_pointer (&error));
    else
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               "The filter list '%s' cannot be read", data->name);
    return;
  }

  if (stats.network == 0) {
    g_free (json);
    LOG ("The filter list '%s' has nothing in it that stops a request", data->name);
    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                             "The filter list has no network rules");
    return;
  }

  LOG ("The filter list '%s' has %u rules out of %u lines", data->name, stats.network, stats.total);

  data->json = json;
  data->stats = stats;
  g_task_return_boolean (task, TRUE);
}

static void
compile_done_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  CompileData *data = user_data;
  g_autoptr (GError) error = NULL;

  if (!g_task_propagate_boolean (G_TASK (result), &error)) {
    if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("The filter list '%s' cannot be used: %s", data->identifier, error->message);
    compile_data_free (data);
    return;
  }

  /* The WebKit call has to happen on the main thread. */
  {
    g_autoptr (GBytes) bytes = g_bytes_new_take (g_strdup (data->json), strlen (data->json));

    webkit_user_content_filter_store_save (data->self->store, data->identifier, bytes, NULL,
                                            filter_saved_cb, data);
  }
}

static void
extension_filter_load_cb (GObject      *source,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  CompileData *data = user_data;
  g_autoptr (WebKitUserContentFilter) filter = NULL;
  g_autoptr (GError) error = NULL;

  filter = webkit_user_content_filter_store_load_finish (WEBKIT_USER_CONTENT_FILTER_STORE (source),
                                                         result, &error);
  if (filter) {
    LOG ("The filter list '%s' is ready", data->identifier);
    g_ptr_array_add (data->self->filters, g_steal_pointer (&filter));
    compile_data_free (data);
    return;
  }

  /* Load failed: compile in a thread, then save on the main thread. */
  {
    g_autoptr (GTask) task = g_task_new (NULL, NULL, compile_done_cb, data);

    g_task_set_task_data (task, data, NULL);
    g_task_run_in_thread (task, compile_thread);
  }
}

/* Need extension_filter_load_cb declared before add uses it; forward decl above. */

/**
 * ephy_extension_filters_apply:
 * @self: the filters
 * @view: the page
 *
 * Puts the rules on a page.
 */
void
ephy_extension_filters_apply (EphyExtensionFilters *self,
                              WebKitWebView       *view)
{
  WebKitUserContentManager *ucm;

  if (!self || !view)
    return;

  if (!g_settings_get_boolean (EPHY_SETTINGS_WEB, EPHY_PREFS_WEB_ENABLE_ADBLOCK))
    return;

  if (self->filters->len == 0)
    return;

  ucm = webkit_web_view_get_user_content_manager (view);

  for (guint i = 0; i < self->filters->len; i++) {
    WebKitUserContentFilter *filter = g_ptr_array_index (self->filters, i);

    webkit_user_content_manager_add_filter (ucm, filter);
  }
}
