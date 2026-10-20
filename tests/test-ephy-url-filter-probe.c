/*
 * Which URL filter patterns will WebKit take?
 *
 * The content blocker's url-filter is not quite a regular expression and
 * not quite a pattern either, so the only way to know is to ask.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <glib.h>
#include <webkit/webkit.h>

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

static gboolean
try_pattern (WebKitUserContentFilterStore *store,
             const char                   *pattern)
{
  g_autofree char *json = NULL;
  g_autoptr (GBytes) bytes = NULL;
  SaveData data = { 0 };
  gboolean ok;

  {
    g_autoptr (GString) escaped = g_string_new (NULL);

    for (const char *p = pattern; *p; p++) {
      if (*p == '\\' || *p == '"')
        g_string_append_c (escaped, '\\');
      g_string_append_c (escaped, *p);
    }

    json = g_strdup_printf ("[{\"trigger\":{\"url-filter\":\"%s\"},\"action\":{\"type\":\"block\"}}]",
                            escaped->str);
  }
  bytes = g_bytes_new_take (g_strdup (json), strlen (json));

  data.loop = g_main_loop_new (NULL, FALSE);
  webkit_user_content_filter_store_save (store, "probe", bytes, NULL, filter_saved_cb, &data);
  g_main_loop_run (data.loop);

  ok = data.filter != NULL;
  g_print ("  %-4s %s\n", ok ? "yes" : "no", pattern);
  if (!ok && data.error)
    g_print ("       %s\n", data.error->message);

  g_clear_object (&data.filter);
  g_clear_error (&data.error);
  g_main_loop_unref (data.loop);

  return ok;
}

int
main (int argc, char **argv)
{
  g_autoptr (WebKitUserContentFilterStore) store = NULL;
  static const char * const patterns[] = {
    "example.com",
    ".*example\\.com.*",
    "^https://example\\.com",
    "example\\.com$",
    "^https://example\\.com$",
    "^https://([^/]+\\.)?example\\.com",
    "^https://(?:[^/?#]+\\.)?example\\.com",
    ".*[0-9]+.*",
    "^.*abc.*",
    "a\\.b",
    "\\.ads\\/",
    "^https?://",
    ".*",
    "ads\\.js$",
    "a{2,}",
    "a{2}",
    "[^abc]",
    "[a-z0-9]",
    "\\w",
    "\\d\\+",
    "a\\+b",
    "a\\(b",
    "a\\?b",
    "a\\{2\\}",
    "a\\/b",
    "http:\\/\\/",
    "\\[\\w\\W\\]",
    "a|b",
    "^a$",
    "^(a|b)$",
    ".*\\.ads\\/.*",
    "^https://[^/]+/ads",
    "a\\|b",
    "\\[a\\]",
    "\\{2\\}",
    "\\\\w",
    "a\\.b\\$",
    "^[a-z-]+://(?:[^/?#]+\\.)?example\\.com",
    "^https://example\\.com/.*\\?n=",
  };

  store = webkit_user_content_filter_store_new ("/tmp/filter-probe");

  for (size_t i = 0; i < G_N_ELEMENTS (patterns); i++)
    try_pattern (store, patterns[i]);

  return 0;
}
