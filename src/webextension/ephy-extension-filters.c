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
#include "ephy-filters-compiler.h"

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
  guint counter; /* so that each list gets a name of its own */
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
  self->store = webkit_user_content_filter_store_new (g_get_tmp_dir ());
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
} CompileData;

static void
compile_data_free (CompileData *data)
{
  g_clear_object (&data->self);
  g_free (data->identifier);
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
    g_warning ("The filter list '%s' cannot be used: %s", data->identifier, error->message);
    return;
  }

  LOG ("The filter list '%s' is ready", data->identifier);

  g_ptr_array_add (data->self->filters, g_steal_pointer (&filter));
}

/**
 * ephy_extension_filters_add:
 * @self: the filters
 * @name: the name of the list
 * @text: the filters, in the Adblock Plus format
 * @length: how long the filters are
 *
 * Adds a list of filters and compiles it.
 *
 * Returns: %TRUE if the list was understood
 */
gboolean
ephy_extension_filters_add (EphyExtensionFilters *self,
                             const char           *name,
                             const char           *text,
                             gsize                 length)
{
  g_autofree char *json = NULL;
  g_autofree char *identifier = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autoptr (GError) error = NULL;
  EphyFiltersCompileStats stats = { 0 };
  CompileData *data;

  if (!text || !length)
    return FALSE;

  json = ephy_filters_compile_abp (text, &stats, &error);
  if (!json) {
    g_warning ("The filter list '%s' cannot be read: %s", name, error->message);
    return FALSE;
  }

  if (stats.network == 0) {
    LOG ("The filter list '%s' has nothing in it that stops a request", name);
    return FALSE;
  }

  LOG ("The filter list '%s' has %u rules out of %u lines", name, stats.network, stats.total);

  /* The rules are kept in a store of their own, so that they do not get in
   * the way of the lists the browser downloads for itself. */
  identifier = g_strdup_printf ("extension-%s-%u", name, ++self->counter);

  bytes = g_bytes_new_take (g_strdup (json), strlen (json));

  data = g_new0 (CompileData, 1);
  data->self = g_object_ref (self);
  data->identifier = g_strdup (identifier);

  webkit_user_content_filter_store_save (self->store, identifier, bytes, NULL, filter_saved_cb, data);

  return TRUE;
}

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

  if (!self || !view || self->filters->len == 0)
    return;

  ucm = webkit_web_view_get_user_content_manager (view);

  for (guint i = 0; i < self->filters->len; i++) {
    WebKitUserContentFilter *filter = g_ptr_array_index (self->filters, i);

    webkit_user_content_manager_add_filter (ucm, filter);
  }
}
