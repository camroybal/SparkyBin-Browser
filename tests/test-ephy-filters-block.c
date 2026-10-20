/*
 * A check that a request that a filter says to stop does not happen.
 *
 * This is the whole point of the content blocker: the request must not
 * reach the server, not merely be hidden afterwards.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <libsoup/soup.h>
#include <adwaita.h>
#include <webkit/webkit.h>

#include "ephy-filters-compiler.h"

typedef struct {
  GPtrArray *requests;
} ServerData;

static void
server_handler (SoupServer        *server,
                SoupServerMessage *message,
                const char        *path,
                GHashTable        *query,
                gpointer           user_data)
{
  ServerData *data = user_data;

  g_ptr_array_add (data->requests, g_strdup (path));

  soup_server_message_set_status (message, SOUP_STATUS_OK, NULL);
  soup_server_message_set_response (message, "text/html", SOUP_MEMORY_STATIC,
                                    "<html><body><h1>A page</h1>"
                                    "<img src='/tracker.gif'></body></html>", 60);
}

typedef struct {
  GMainLoop *loop;
  WebKitWebView *view;
  WebKitUserContentFilter *filter;
  GError *error;
  gboolean loaded;
} TestData;

static void
filter_saved_cb (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  TestData *data = user_data;

  data->filter = webkit_user_content_filter_store_save_finish (WEBKIT_USER_CONTENT_FILTER_STORE (source),
                                                               result, &data->error);
  g_main_loop_quit (data->loop);
}

static void
load_finished_cb (WebKitWebView *view,
                  WebKitLoadEvent event,
                  gpointer        user_data)
{
  TestData *data = user_data;

  if (event != WEBKIT_LOAD_FINISHED)
    return;

  data->loaded = TRUE;
  g_main_loop_quit (data->loop);
}

int
main (int argc, char **argv)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (SoupServer) server = NULL;
  g_autoptr (WebKitUserContentFilterStore) store = NULL;
  g_autoptr (WebKitUserContentManager) ucm = NULL;
  g_autoptr (WebKitUserContentFilter) filter = NULL;
  g_autoptr (GBytes) bytes = NULL;
  g_autofree char *json = NULL;
  g_autofree char *uri = NULL;
  ServerData server_data = { 0 };
  TestData test_data = { 0 };
  gboolean tracker_was_asked_for = FALSE;

  gtk_init ();
  adw_init ();

  server_data.requests = g_ptr_array_new_with_free_func (g_free);

  server = soup_server_new (NULL, NULL);
  soup_server_add_handler (server, NULL, server_handler, &server_data, NULL);
  soup_server_listen_local (server, 18123, 0, &error);
  g_assert_no_error (error);

  uri = g_strdup ("http://127.0.0.1:18123");

  /* The rules. */
  {
    g_autofree char *filters = g_strdup_printf ("||127.0.0.1:18123/tracker.gif\n");

    json = ephy_filters_compile_abp (filters, NULL, &error);
    g_assert_no_error (error);
    g_assert_nonnull (json);

    g_print ("the rule stops: %s/tracker.gif\n", uri);
  }

  g_mkdir_with_parents ("/tmp/filter-test", 0700);
  store = webkit_user_content_filter_store_new ("/tmp/filter-test");
  bytes = g_bytes_new_take (g_strdup (json), strlen (json));

  test_data.loop = g_main_loop_new (NULL, FALSE);
  webkit_user_content_filter_store_save (store, "test", bytes, NULL, filter_saved_cb, &test_data);
  g_main_loop_run (test_data.loop);
  filter = g_steal_pointer (&test_data.filter);
  if (!filter) {
    g_printerr ("the rules were not accepted: %s\n",
                test_data.error ? test_data.error->message : "no reason given");
    return 1;
  }

  test_data.view = WEBKIT_WEB_VIEW (webkit_web_view_new ());
  ucm = webkit_web_view_get_user_content_manager (test_data.view);
  webkit_user_content_manager_add_filter (ucm, filter);
  g_signal_connect (test_data.view, "load-changed", G_CALLBACK (load_finished_cb), &test_data);

  webkit_web_view_load_uri (test_data.view, uri);
  g_main_loop_run (test_data.loop);

  g_assert_true (test_data.loaded);

  for (guint i = 0; i < server_data.requests->len; i++) {
    const char *path = g_ptr_array_index (server_data.requests, i);

    if (g_strcmp0 (path, "/tracker.gif") == 0)
      tracker_was_asked_for = TRUE;
  }

  g_print ("the server was asked for %u things\n", server_data.requests->len);
  for (guint i = 0; i < server_data.requests->len; i++)
    g_print ("  %s\n", (const char *) g_ptr_array_index (server_data.requests, i));

  if (tracker_was_asked_for) {
    g_printerr ("the request that should have been stopped was made\n");
    return 1;
  }

  g_print ("the request was stopped\n");

  return 0;
}
