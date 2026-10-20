/*
 * Tests for the filter compiler.
 */

#include "config.h"

#include <string.h>

#include <glib.h>
#include <json-glib/json-glib.h>

#include "ephy-filters-compiler.h"

/* The rules, as the text that goes to WebKit. */
static char *
compile (const char  *filters,
         guint       *network,
         guint       *cosmetic,
         guint       *skipped)
{
  g_autofree char *json = NULL;
  g_autoptr (GError) error = NULL;
  EphyFiltersCompileStats stats = { 0 };

  json = ephy_filters_compile_abp (filters, &stats, &error);
  g_assert_no_error (error);
  g_assert_nonnull (json);

  if (network)
    *network = stats.network;
  if (cosmetic)
    *cosmetic = stats.cosmetic;
  if (skipped)
    *skipped = stats.skipped;

  return g_steal_pointer (&json);
}

static void
test_simple_pattern (void)
{
  g_autofree char *json = compile ("||example.com^ads\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"url-filter\":\"^[a-z-]+://([^/]+\\\\.)?example\\\\.com[^a-zA-Z0-9_.%-]ads\""));
  g_assert_nonnull (strstr (json, "\"action\":{\"type\":\"block\"}"));
}

static void
test_pattern_in_the_middle (void)
{
  g_autofree char *json = compile ("/ads/banner\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"url-filter\":\"/ads/banner\""));
}

static void
test_star (void)
{
  g_autofree char *json = compile ("||example.com/*/ads\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"url-filter\":\"^[a-z-]+://([^/]+\\\\.)?example\\\\.com/.*/ads\""));
}

static void
test_third_party (void)
{
  g_autofree char *json = compile ("||example.com^$third-party\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"load-type\":[\"third-party\"]"));
}

static void
test_domain (void)
{
  g_autofree char *json = compile ("||example.com^$domain=one.com|two.com\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"if-domain\":[\"one.com\",\"two.com\"]"));
}

static void
test_excluded_domain (void)
{
  g_autofree char *json = compile ("||example.com^$domain=~one.com\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"unless-domain\":[\"one.com\"]"));
}

static void
test_resource_types (void)
{
  g_autofree char *json = compile ("||example.com^$script,image\n", NULL, NULL, NULL);

  g_assert_nonnull (strstr (json, "\"resource-type\":[\"script\",\"image\"]"));
}

static void
test_excluded_resource_type (void)
{
  g_autofree char *json = compile ("||example.com^$~script\n", NULL, NULL, NULL);

  /* Everything but scripts. */
  g_assert_null (strstr (json, "\"script\""));
  g_assert_nonnull (strstr (json, "\"resource-type\":["));
}

static void
test_a_type_and_an_excluded_type (void)
{
  g_autofree char *json = compile ("||example.com^$script,~image\n", NULL, NULL, NULL);

  /* The rule is for scripts, but not for images. */
  g_assert_nonnull (strstr (json, "\"resource-type\":[\"script\"]"));
  g_assert_null (strstr (json, "\"image\""));
}

static void
test_exception_comes_first (void)
{
  g_autofree char *json = compile ("||example.com^ads\n@@||example.com^ads\n", NULL, NULL, NULL);
  const char *exception;
  const char *block;

  exception = strstr (json, "ignore-previous-rules");
  block = strstr (json, "\"type\":\"block\"");

  g_assert_nonnull (exception);
  g_assert_nonnull (block);
  g_assert_true (exception < block);
}

static void
test_cosmetic_rules_are_left_out (void)
{
  guint network = 0, cosmetic = 0, skipped = 0;
  g_autofree char *json = compile ("||example.com^ads\nexample.com##.ad-banner\n", &network, &cosmetic, &skipped);

  g_assert_cmpuint (network, ==, 1);
  g_assert_cmpuint (cosmetic, ==, 1);
  g_assert_cmpuint (skipped, ==, 0);
  g_assert_null (strstr (json, ".ad-banner"));
}

static void
test_comments_are_left_out (void)
{
  guint network = 0, cosmetic = 0, skipped = 0;
  g_autofree char *json = compile ("! a comment\n[Adblock Plus 2.0]\n||example.com^ads\n", &network, &cosmetic, &skipped);

  g_assert_cmpuint (network, ==, 1);
  g_assert_null (strstr (json, "comment"));
}

static void
test_unsupported_options_are_left_out (void)
{
  guint network = 0, cosmetic = 0, skipped = 0;
  g_autofree char *json = compile ("||example.com^ads$removeparam=foo\n", &network, &cosmetic, &skipped);

  g_assert_cmpuint (network, ==, 0);
  g_assert_cmpuint (skipped, ==, 1);
  g_assert_null (strstr (json, "ads"));
}

static void
test_redirect_is_left_out (void)
{
  guint network = 0, cosmetic = 0, skipped = 0;
  g_autofree char *json = compile ("||example.com^ads$redirect=nooptext\n", &network, &cosmetic, &skipped);

  /* A redirect that cannot be done must not turn into a block. */
  g_assert_cmpuint (network, ==, 0);
  g_assert_cmpuint (skipped, ==, 1);
  g_assert_null (strstr (json, "ads"));
}

static void
test_badfilter (void)
{
  guint network = 0, cosmetic = 0, skipped = 0;
  g_autofree char *json = compile ("||example.com^ads\n||example.com^ads$badfilter\n", &network, &cosmetic, &skipped);

  g_assert_cmpuint (network, ==, 0);
  g_assert_null (strstr (json, "ads"));
}

static void
test_dnr_rules (void)
{
  g_autofree char *json = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (JsonArray) input = json_array_new ();
  JsonObject *rule = json_object_new ();
  JsonObject *action = json_object_new ();
  JsonObject *condition = json_object_new ();
  JsonArray *types = json_array_new ();

  json_array_add_string_element (types, "script");
  json_object_set_string_member (condition, "urlFilter", "||ads.example.com^");
  json_object_set_array_member (condition, "resourceTypes", types);
  json_object_set_string_member (action, "type", "block");
  json_object_set_object_member (rule, "action", action);
  json_object_set_object_member (rule, "condition", condition);
  json_array_add_object_element (input, rule);

  json = ephy_filters_compile_dnr (input, NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (json);

  g_assert_nonnull (strstr (json, "\"url-filter\":\"^[a-z-]+://([^/]+\\\\.)?ads\\\\.example\\\\.com[^a-zA-Z0-9_.%-]\""));
  g_assert_nonnull (strstr (json, "\"action\":{\"type\":\"block\"}"));
  g_assert_nonnull (strstr (json, "\"resource-type\":[\"script\"]"));
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/filters/simple-pattern", test_simple_pattern);
  g_test_add_func ("/filters/pattern-in-the-middle", test_pattern_in_the_middle);
  g_test_add_func ("/filters/star", test_star);
  g_test_add_func ("/filters/third-party", test_third_party);
  g_test_add_func ("/filters/domain", test_domain);
  g_test_add_func ("/filters/excluded-domain", test_excluded_domain);
  g_test_add_func ("/filters/resource-types", test_resource_types);
  g_test_add_func ("/filters/excluded-resource-type", test_excluded_resource_type);
  g_test_add_func ("/filters/a-type-and-an-excluded-type", test_a_type_and_an_excluded_type);
  g_test_add_func ("/filters/exception-comes-first", test_exception_comes_first);
  g_test_add_func ("/filters/cosmetic-rules-are-left-out", test_cosmetic_rules_are_left_out);
  g_test_add_func ("/filters/comments-are-left-out", test_comments_are_left_out);
  g_test_add_func ("/filters/unsupported-options-are-left-out", test_unsupported_options_are_left_out);
  g_test_add_func ("/filters/redirect-is-left-out", test_redirect_is_left_out);
  g_test_add_func ("/filters/badfilter", test_badfilter);
  g_test_add_func ("/filters/dnr-rules", test_dnr_rules);

  return g_test_run ();
}
