/*
 * A check that the rules the compiler makes are ones WebKit will take.
 *
 * It compiles a list of filters and hands the result to WebKit's content
 * blocker, which is the only thing that can actually stop a request.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <webkit/webkit.h>

#include "ephy-filters-compiler.h"

typedef struct {
  GMainLoop *loop;
  WebKitUserContentFilter *filter;
  GError *error;
} SaveData;

static void
filter_saved_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  SaveData *data = user_data;

  data->filter = webkit_user_content_filter_store_save_finish (WEBKIT_USER_CONTENT_FILTER_STORE (source),
                                                                result, &data->error);
  g_main_loop_quit (data->loop);
}

int
main (int argc, char **argv)
{
  g_autofree char *contents = NULL;
  g_autofree char *json = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (WebKitUserContentFilterStore) store = NULL;
  g_autoptr (GBytes) bytes = NULL;
  EphyFiltersCompileStats stats = { 0 };
  SaveData data = { 0 };
  gsize length;

  if (argc < 2) {
    g_printerr ("usage: %s FILTERS\n", argv[0]);
    return 1;
  }

  if (!g_file_get_contents (argv[1], &contents, &length, &error)) {
    g_printerr ("Cannot read %s: %s\n", argv[1], error->message);
    return 1;
  }

  json = ephy_filters_compile_abp (contents, &stats, &error);
  if (!json) {
    g_printerr ("Cannot compile: %s\n", error->message);
    return 1;
  }

  g_print ("%u lines, %u rules, %u cosmetic, %u skipped\n",
           stats.total, stats.network, stats.cosmetic, stats.skipped);
  g_print ("the rules are %u bytes\n", (guint) strlen (json));

  if (argc > 2) {
    g_autoptr (GError) write_error = NULL;

    if (!g_file_set_contents (argv[2], json, -1, &write_error)) {
      g_printerr ("Cannot write %s: %s\n", argv[2], write_error->message);
      return 1;
    }
  }

  /* Now the part that matters: will WebKit take them? */
  store = webkit_user_content_filter_store_new ("/tmp/filter-store");
  bytes = g_bytes_new_take (g_strdup (json), strlen (json));

  data.loop = g_main_loop_new (NULL, FALSE);
  webkit_user_content_filter_store_save (store, "test", bytes, NULL, filter_saved_cb, &data);
  g_main_loop_run (data.loop);

  if (!data.filter) {
    g_printerr ("WebKit would not take the rules: %s\n",
                data.error ? data.error->message : "no filter");
    return 1;
  }

  g_print ("WebKit took the rules\n");

  return 0;
}
