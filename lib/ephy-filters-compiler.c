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

#include "ephy-filters-compiler.h"

#include <string.h>

#include <json-glib/json-glib.h>

/*
 * Turning filters into the rules that WebKit's content blockers use.
 *
 * WebKit's content blockers are the only way a request can be stopped.
 * Their rules are in Safari's format: a trigger that says when a rule
 * applies, and an action that says what to do about it.
 *
 * Two things come in here:
 *
 *  - Adblock Plus filter lists, which is what the popular ad blockers
 *    use, and what the browser's own lists are in.
 *
 *  - The rules of browser.declarativeNetRequest, which is what an
 *    extension declares when it wants requests stopped.
 *
 * The URL patterns in both are in the Adblock Plus syntax, so the same
 * translation does both.
 */

/* The resource types a content blocker knows about. */
static const char * const all_resource_types[] = {
  "document", "child-document", "style-sheet", "script", "image", "font",
  "raw", "svg-document", "media", "popup", "ping", "fetch", "websocket",
  "other"
};

typedef struct {
  GPtrArray *rules;       /* JsonObject *, the rules so far */
  GHashTable *badfilters; /* char * -> the pattern it turns off */
  guint total;
  guint network;
  guint cosmetic;
  guint skipped;
} Compiler;

static void
compiler_init (Compiler *compiler)
{
  compiler->rules = g_ptr_array_new_with_free_func (g_free);
  compiler->badfilters = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  compiler->total = 0;
  compiler->network = 0;
  compiler->cosmetic = 0;
  compiler->skipped = 0;
}

static void
compiler_clear (Compiler *compiler)
{
  g_clear_pointer (&compiler->rules, g_ptr_array_unref);
  g_clear_pointer (&compiler->badfilters, g_hash_table_unref);
}

/**
 * translate_pattern:
 * @pattern: a URL pattern in the Adblock Plus syntax
 *
 * Turns a URL pattern into the regular expression that a content
 * blocker's url-filter is.
 *
 * The pattern is matched anywhere in the URL, which is what a content
 * blocker's filter is, and what an Adblock Plus pattern means when it
 * does not start with || or |.
 *
 * Returns: (transfer full): the expression
 */
static char *
translate_pattern (const char *pattern)
{
  GString *regex = g_string_new (NULL);
  gsize length = strlen (pattern);
  const char *p = pattern;

  /* A | at the end means the end of the URL, and the $ that says so goes
   * at the end of the expression, which is the only place it is allowed. */
  gboolean at_end = FALSE;

  if (length > 0 && p[length - 1] == '|') {
    length--;
    at_end = TRUE;
  }

  if (length >= 2 && p[0] == '|' && p[1] == '|') {
    /* || is the start of a domain: the scheme, and any subdomains. */
    g_string_append (regex, "^[a-z-]+://([^/]+\\.)?");
    p += 2;
    length -= 2;
  } else if (length >= 1 && p[0] == '|') {
    /* A single | at the start is the start of the URL. */
    g_string_append (regex, "^");
    p += 1;
    length -= 1;
  }

  for (gsize i = 0; i < length; i++) {
    char c = p[i];

    switch (c) {
    case '*':
      g_string_append (regex, ".*");
      break;
    case '^':
      /* A separator: anything that is not part of a name. */
      g_string_append (regex, "[^a-zA-Z0-9_.%-]");
      break;
    case '|':
      g_string_append (regex, "\\|");
      break;
    case '\\':
      /* A backslash means the character after it is a literal one. */
      if (i + 1 < length) {
        i++;
        if ((unsigned char) p[i] >= 0x80) {
          g_string_append_c (regex, p[i]);
        } else if (g_ascii_isalnum (p[i]) || strchr ("-_~:/#@!&';,=% ", p[i])) {
          g_string_append_c (regex, p[i]);
        } else {
          g_string_append_printf (regex, "\\%c", p[i]);
        }
      }
      break;
    default:
      if ((unsigned char) c >= 0x80) {
        /* Part of a character that is not ASCII. It is left as it is,
         * because a backslash in the middle of it would break it. */
        g_string_append_c (regex, c);
      } else if (g_ascii_isalnum (c) || strchr ("-_~:/#@!&';,=% ", c)) {
        /* A filter matches the URL as it is written, so a full stop is a
         * full stop and not "any character". The characters that mean
         * something to a regular expression are escaped. */
        g_string_append_c (regex, c);
      } else {
        g_string_append_printf (regex, "\\%c", c);
      }
      break;
    }
  }

  if (at_end)
    g_string_append (regex, "$");

  return g_string_free (regex, FALSE);
}

/**
 * resource_type_for:
 * @name: the type as the filters name it
 *
 * The name a content blocker knows a resource type by.
 *
 * Returns: (transfer none) (nullable): the name, or %NULL if there is none
 */
static const char *
resource_type_for (const char *name)
{
  static const struct {
    const char *filter;
    const char *blocker;
  } types[] = {
    { "main-frame", "document" },
    { "sub-frame", "child-document" },
    { "frame", "child-document" },
    { "stylesheet", "style-sheet" },
    { "script", "script" },
    { "image", "image" },
    { "font", "font" },
    { "object", "raw" },
    { "object-subrequest", "raw" },
    { "xmlhttprequest", "raw" },
    { "xhr", "raw" },
    { "ping", "ping" },
    { "media", "media" },
    { "websocket", "websocket" },
    { "other", "other" },
    { "popup", "popup" },
  };

  for (size_t i = 0; i < G_N_ELEMENTS (types); i++) {
    if (g_strcmp0 (types[i].filter, name) == 0)
      return types[i].blocker;
  }

  return NULL;
}

/**
 * resource_types_except:
 * @excluded: the types to leave out
 *
 * Every resource type apart from the ones given, which is how a filter
 * says "everything but this", since a content blocker has no way of
 * saying that directly.
 *
 * Returns: (transfer full) (element-type utf8): the types
 */
static GPtrArray *
resource_types_except (GPtrArray *excluded)
{
  GPtrArray *types = g_ptr_array_new_with_free_func (g_free);
  guint i;

  for (i = 0; i < G_N_ELEMENTS (all_resource_types); i++) {
    const char *type = all_resource_types[i];
    gboolean found = FALSE;

    if (excluded) {
      for (guint j = 0; j < excluded->len; j++) {
        if (g_strcmp0 (g_ptr_array_index (excluded, j), type) == 0) {
          found = TRUE;
          break;
        }
      }
    }

    if (!found)
      g_ptr_array_add (types, g_strdup (type));
  }

  return types;
}

static GPtrArray *
split_list (const char *value)
{
  GPtrArray *list = g_ptr_array_new_with_free_func (g_free);
  g_auto(GStrv) parts = NULL;

  if (!value)
    return list;

  parts = g_strsplit (value, "|", -1);
  for (guint i = 0; parts[i]; i++) {
    if (*parts[i])
      g_ptr_array_add (list, g_strdup (parts[i]));
  }

  return list;
}

/**
 * append_json_string:
 * @string: the text being built
 * @value: the value
 *
 * Writes a string as a JSON string.
 */
static void
append_json_string (GString *string,
                    const char *value)
{
  g_string_append_c (string, '"');

  for (const char *p = value; *p; p++) {
    switch (*p) {
    case '"':
      g_string_append (string, "\\\"");
      break;
    case '\\':
      g_string_append (string, "\\\\");
      break;
    case '\n':
      g_string_append (string, "\\n");
      break;
    case '\r':
      g_string_append (string, "\\r");
      break;
    case '\t':
      g_string_append (string, "\\t");
      break;
    default:
      if ((unsigned char) *p < 0x20)
        g_string_append_printf (string, "\\u%04x", *p);
      else
        g_string_append_c (string, *p);
      break;
    }
  }

  g_string_append_c (string, '"');
}

static void
append_json_array (GString *string,
                   GPtrArray *values)
{
  g_string_append_c (string, '[');

  if (values) {
    for (guint i = 0; i < values->len; i++) {
      if (i > 0)
        g_string_append_c (string, ',');
      append_json_string (string, g_ptr_array_index (values, i));
    }
  }

  g_string_append_c (string, ']');
}

/**
 * make_rule:
 * @pattern: the URL pattern
 * @resource_types: the resource types it applies to, or %NULL for all
 * @excluded_types: the resource types it does not apply to
 * @domains: the pages it applies on, or %NULL for all
 * @excluded_domains: the pages it does not apply on
 * @load_type: "first-party", "third-party" or %NULL
 * @action: the action
 *
 * Builds one rule for a content blocker.
 *
 * Returns: (transfer full) (nullable): the rule
 */
/**
 * domain_is_usable:
 * @domain: the name of a domain
 *
 * Whether a list of domains can be used. A filter may name a domain with
 * a pattern in it, and a content blocker has no way of matching a domain
 * with a pattern.
 *
 * Returns: %TRUE if the domain can be used
 */
static gboolean
domain_is_usable (const char *domain)
{
  if (!domain || !*domain)
    return FALSE;

  for (const char *p = domain; *p; p++) {
    if (!g_ascii_isalnum (*p) && !strchr (".-_", *p))
      return FALSE;
  }

  return TRUE;
}

/**
 * regex_is_usable:
 * @regex: the expression
 *
 * Whether a content blocker will take the expression.
 *
 * A content blocker understands most of what a regular expression does,
 * but not the alternation, not the repetition, and not the shorthands
 * for the character classes. A rule it will not take stops the whole list
 * from being used, so such a rule is left out.
 *
 * Returns: %TRUE if the expression can be used
 */
static gboolean
regex_is_usable (const char *regex)
{
  for (const char *p = regex; *p; p++) {
    if (*p == '\\') {
      p++;
      if (!*p)
        return FALSE;
      if (strchr ("wdsWbDS", *p))
        return FALSE;
      continue;
    }

    if (*p == '|' || *p == '{' || *p == '}')
      return FALSE;
  }

  return TRUE;
}

static gboolean
pattern_is_usable (const char *pattern)
{
  for (const char *p = pattern; *p; p++) {
    if ((unsigned char) *p >= 0x80)
      return FALSE;
  }

  return TRUE;
}

static char *
make_rule (const char    *pattern,
           GPtrArray     *resource_types,
           GPtrArray     *excluded_types,
           GPtrArray     *domains,
           GPtrArray     *excluded_domains,
           const char    *load_type,
           const char    *action,
           gboolean       case_sensitive)
{
  g_autofree char *regex = NULL;
  GString *rule;

  if (!pattern || !*pattern)
    return NULL;

  /* A content blocker only takes characters that are ASCII. */
  if (!pattern_is_usable (pattern))
    return NULL;

  regex = translate_pattern (pattern);

  if (!regex_is_usable (regex))
    return NULL;

  rule = g_string_new ("{\"trigger\":{");

  g_string_append (rule, "\"url-filter\":");
  append_json_string (rule, regex);

  if (case_sensitive)
    g_string_append (rule, ",\"url-filter-is-case-sensitive\":true");

  if (excluded_types && excluded_types->len > 0) {
    GPtrArray *types = resource_types_except (excluded_types);

    g_string_append (rule, ",\"resource-type\":");
    append_json_array (rule, types);
    g_ptr_array_unref (types);
  } else if (resource_types && resource_types->len > 0) {
    g_string_append (rule, ",\"resource-type\":");
    append_json_array (rule, resource_types);
  }

  if (domains && domains->len > 0) {
    g_string_append (rule, ",\"if-domain\":");
    append_json_array (rule, domains);
  }

  if (excluded_domains && excluded_domains->len > 0) {
    g_string_append (rule, ",\"unless-domain\":");
    append_json_array (rule, excluded_domains);
  }

  if (load_type) {
    g_string_append (rule, ",\"load-type\":[");
    append_json_string (rule, load_type);
    g_string_append_c (rule, ']');
  }

  g_string_append (rule, "},\"action\":{\"type\":");
  append_json_string (rule, action);
  g_string_append (rule, "}}");

  return g_string_free (rule, FALSE);
}

/**
 * parse_abp_options:
 * @options: the options after the $ in a filter
 * @resource_types: (out) (optional) (element-type utf8): the types
 * @excluded_types: (out) (optional) (element-type utf8): the types left out
 * @domains: (out) (optional) (element-type utf8): the pages
 * @excluded_domains: (out) (optional) (element-type utf8): the pages left out
 * @load_type: (out) (optional): the load type
 * @is_exception: (out) (optional): whether the rule allows rather than blocks
 * @redirect: (out) (optional): where to send the request instead
 * @badfilter: (out) (optional): whether the rule turns another one off
 * @unsupported: (out) (optional): whether the rule asks for something that
 *   cannot be done
 *
 * Reads the options of a filter.
 *
 * Returns: %TRUE if the options were understood
 */
static gboolean
parse_abp_options (const char *options,
                    GPtrArray  **resource_types,
                    GPtrArray  **excluded_types,
                    GPtrArray  **domains,
                    GPtrArray  **excluded_domains,
                    const char **load_type,
                    gboolean    *is_exception,
                    const char **redirect,
                    gboolean    *badfilter,
                    gboolean    *unsupported)
{
  g_auto(GStrv) parts = NULL;
  gboolean excluded = FALSE;

  if (!options)
    return TRUE;

  parts = g_strsplit (options, ",", -1);

  for (guint i = 0; parts[i]; i++) {
    g_autofree char *name = NULL;
    const char *option = parts[i];
    const char *value = NULL;
    const char *equals;

    /* An option is a name and, sometimes, a value. */
    equals = strchr (parts[i], '=');
    if (equals) {
      name = g_strndup (parts[i], equals - parts[i]);
      value = equals + 1;
    } else {
      name = g_strdup (parts[i]);
    }

    option = name;

    if (*option == '~') {
      excluded = TRUE;
      option++;
    }

    if (g_strcmp0 (option, "domain") == 0) {
      if (!value)
        continue;

      /* The ~ of a list of domains is on the list, not on the name. */
      if (*value == '~') {
        value++;
        excluded = TRUE;
      }

      {
        GPtrArray *list = split_list (value);

        /* A domain with a pattern in it cannot be used. */
        for (guint j = 0; j < list->len; j++) {
          if (!domain_is_usable (g_ptr_array_index (list, j))) {
            g_ptr_array_unref (list);
            *unsupported = TRUE;
            excluded = FALSE;
            break;
          }
        }

        if (*unsupported) {
          excluded = FALSE;
          continue;
        }

        if (excluded) {
          if (!*excluded_domains)
            *excluded_domains = list;
          else
            g_ptr_array_unref (list);
        } else {
          if (!*domains)
            *domains = list;
          else
            g_ptr_array_unref (list);
        }
      }
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "redirect") == 0) {
      /* A redirect is only useful if the blocker can do it. */
      if (!value)
        return FALSE;

      *redirect = value;
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "third-party") == 0) {
      *load_type = excluded ? "first-party" : "third-party";
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "first-party") == 0) {
      *load_type = excluded ? "third-party" : "first-party";
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "badfilter") == 0) {
      *badfilter = TRUE;
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "important") == 0 ||
        g_strcmp0 (option, "all") == 0) {
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "removeparam") == 0 ||
        g_strcmp0 (option, "csp") == 0 ||
        g_strcmp0 (option, "denyallow") == 0 ||
        g_strcmp0 (option, "app") == 0) {
      /* These say what to do to a request rather than whether to stop
       * it, so the rule cannot be turned into a block. It is left out
       * rather than used wrongly. */
      *unsupported = TRUE;
      excluded = FALSE;
      continue;
    }

    if (g_strcmp0 (option, "popup") == 0 ||
        g_strcmp0 (option, "elemhide") == 0 ||
        g_strcmp0 (option, "generichide") == 0 ||
        g_strcmp0 (option, "specifichide") == 0 ||
        g_strcmp0 (option, "ghide") == 0 ||
        g_strcmp0 (option, "shide") == 0 ||
        g_strcmp0 (option, "document") == 0 ||
        g_strcmp0 (option, "subdocument") == 0) {
      /* Some of these are about hiding things, which is the extension's
       * business, and some are about things a content blocker cannot do. */
      if (g_strcmp0 (option, "document") == 0 || g_strcmp0 (option, "subdocument") == 0) {
        const char *type = resource_type_for (option);

        if (type) {
          GPtrArray *types = *resource_types ? *resource_types : g_ptr_array_new_with_free_func (g_free);

          if (excluded) {
            if (!*excluded_types)
              *excluded_types = types;
            else
              g_ptr_array_unref (types);
          } else {
            *resource_types = types;
            g_ptr_array_add (types, g_strdup (type));
          }
        }
      }
      excluded = FALSE;
      continue;
    }

    {
      const char *type = resource_type_for (option);

      if (type) {
        GPtrArray *types = *resource_types ? *resource_types : g_ptr_array_new_with_free_func (g_free);

        if (excluded) {
          if (!*excluded_types)
            *excluded_types = types;
          else
            g_ptr_array_unref (types);
        } else {
          *resource_types = types;
        }

        g_ptr_array_add (types, g_strdup (type));
        excluded = FALSE;
        continue;
      }
    }

    /* Anything else is something this does not know how to do. */
    *unsupported = TRUE;
    excluded = FALSE;
  }

  return TRUE;
}

static void
compiler_add (Compiler *compiler,
              char       *rule)
{
  g_ptr_array_add (compiler->rules, rule);
}

static char *
rules_to_json (GPtrArray *rules)
{
  GString *json = g_string_new ("[");

  for (guint i = 0; i < rules->len; i++) {
    if (i > 0)
      g_string_append_c (json, ',');
    g_string_append (json, g_ptr_array_index (rules, i));
  }

  g_string_append_c (json, ']');

  return g_string_free (json, FALSE);
}

/**
 * ephy_filters_compile_abp:
 * @text: the filters, in the Adblock Plus format
 * @stats: (out) (optional): what the list came to
 * @error: (out) (optional): the error
 *
 * Turns a list of filters into the rules that WebKit's content blockers
 * understand.
 *
 * The rules that allow something are put first, because a content
 * blocker stops at the first rule that matches, and a filter list says
 * that an exception wins.
 *
 * Returns: (transfer full) (nullable): the rules, as JSON
 */
char *
ephy_filters_compile_abp (const char            *text,
                          EphyFiltersCompileStats *stats,
                          GError               **error)
{
  Compiler compiler;
  g_auto(GStrv) lines = NULL;
  char *json;
  GPtrArray *exceptions = g_ptr_array_new_with_free_func (g_object_unref);
  GPtrArray *blocks = g_ptr_array_new_with_free_func (g_object_unref);

  g_return_val_if_fail (text, NULL);

  compiler_init (&compiler);
  lines = g_strsplit (text, "\n", -1);

  /* The rules that turn another rule off are found first, because the
   * rule they turn off may come before them in the list. */
  for (guint i = 0; lines[i]; i++) {
    g_autofree char *line = g_strstrip (g_strdup (lines[i]));
    g_autofree char *owned_pattern = NULL;
    const char *pattern;
    const char *dollar;

    if (*line == '\0' || *line == '!' || *line == '[')
      continue;

    pattern = g_str_has_prefix (line, "@@") ? line + 2 : line;
    dollar = strchr (pattern, '$');
    if (!dollar)
      continue;

    owned_pattern = g_strndup (pattern, dollar - pattern);
    if (g_str_has_suffix (dollar + 1, "badfilter"))
      g_hash_table_add (compiler.badfilters, g_steal_pointer (&owned_pattern));
  }

  for (guint i = 0; lines[i]; i++) {
    g_autofree char *line = g_strstrip (g_strdup (lines[i]));
    g_autofree char *owned_pattern = NULL;
    gboolean is_exception = FALSE;
    gboolean badfilter = FALSE;
    gboolean unsupported = FALSE;
    const char *redirect = NULL;
    const char *load_type = NULL;
    const char *pattern;
    const char *options;
    const char *dollar;
    GPtrArray *resource_types = NULL;
    GPtrArray *excluded_types = NULL;
    GPtrArray *domains = NULL;
    GPtrArray *excluded_domains = NULL;
    char *rule;

    compiler.total++;

    if (*line == '\0' || *line == '!' || *line == '[')
      continue;

    /* The rules that hide things in a page are the extension's business. */
    if (strstr (line, "##") || strstr (line, "#@#") || strstr (line, "#$#") ||
        strstr (line, "#%#") || strstr (line, "$$")) {
      compiler.cosmetic++;
      continue;
    }

    if (g_str_has_prefix (line, "@@")) {
      is_exception = TRUE;
      pattern = line + 2;
    } else {
      pattern = line;
    }

    dollar = strchr (pattern, '$');
    if (dollar) {
      options = dollar + 1;
      owned_pattern = g_strndup (pattern, dollar - pattern);
      pattern = owned_pattern;
    } else {
      options = NULL;
    }

    if (!parse_abp_options (options, &resource_types, &excluded_types, &domains,
                            &excluded_domains, &load_type, &is_exception, &redirect,
                            &badfilter, &unsupported)) {
      compiler.skipped++;
      g_clear_pointer (&resource_types, g_ptr_array_unref);
      g_clear_pointer (&excluded_types, g_ptr_array_unref);
      g_clear_pointer (&domains, g_ptr_array_unref);
      g_clear_pointer (&excluded_domains, g_ptr_array_unref);
      continue;
    }

    if (badfilter) {
      g_hash_table_add (compiler.badfilters, g_strdup (pattern));
      compiler.skipped++;
      g_clear_pointer (&resource_types, g_ptr_array_unref);
      g_clear_pointer (&excluded_types, g_ptr_array_unref);
      g_clear_pointer (&domains, g_ptr_array_unref);
      g_clear_pointer (&excluded_domains, g_ptr_array_unref);
      continue;
    }

    if (unsupported) {
      compiler.skipped++;
      g_clear_pointer (&resource_types, g_ptr_array_unref);
      g_clear_pointer (&excluded_types, g_ptr_array_unref);
      g_clear_pointer (&domains, g_ptr_array_unref);
      g_clear_pointer (&excluded_domains, g_ptr_array_unref);
      continue;
    }

    /* A redirect cannot be done by a content blocker, so the rule is left
     * out rather than turned into a block, which would be worse. */
    if (redirect) {
      compiler.skipped++;
      g_clear_pointer (&resource_types, g_ptr_array_unref);
      g_clear_pointer (&excluded_types, g_ptr_array_unref);
      g_clear_pointer (&domains, g_ptr_array_unref);
      g_clear_pointer (&excluded_domains, g_ptr_array_unref);
      continue;
    }

    if (g_hash_table_contains (compiler.badfilters, pattern)) {
      compiler.skipped++;
      g_clear_pointer (&resource_types, g_ptr_array_unref);
      g_clear_pointer (&excluded_types, g_ptr_array_unref);
      g_clear_pointer (&domains, g_ptr_array_unref);
      g_clear_pointer (&excluded_domains, g_ptr_array_unref);
      continue;
    }

    rule = make_rule (pattern, resource_types, excluded_types, domains, excluded_domains,
                      load_type, is_exception ? "ignore-previous-rules" : "block", FALSE);

    g_clear_pointer (&resource_types, g_ptr_array_unref);
    g_clear_pointer (&excluded_types, g_ptr_array_unref);
    g_clear_pointer (&domains, g_ptr_array_unref);
    g_clear_pointer (&excluded_domains, g_ptr_array_unref);

    if (!rule) {
      compiler.skipped++;
      continue;
    }

    compiler.network++;

    if (is_exception)
      g_ptr_array_add (exceptions, rule);
    else
      g_ptr_array_add (blocks, rule);
  }

  /* The exceptions come first, so that they win. The rules have moved to
   * the compiler, so the lists let go of them. */
  for (guint i = 0; i < exceptions->len; i++)
    compiler_add (&compiler, g_ptr_array_index (exceptions, i));
  for (guint i = 0; i < blocks->len; i++)
    compiler_add (&compiler, g_ptr_array_index (blocks, i));

  g_ptr_array_set_free_func (exceptions, NULL);
  g_ptr_array_set_free_func (blocks, NULL);
  g_ptr_array_unref (exceptions);
  g_ptr_array_unref (blocks);

  if (stats) {
    stats->total = compiler.total;
    stats->network = compiler.network;
    stats->cosmetic = compiler.cosmetic;
    stats->skipped = compiler.skipped;
  }

  json = rules_to_json (compiler.rules);

  compiler_clear (&compiler);

  return json;
}

/**
 * dnr_resource_types:
 * @array: the resource types as the API names them
 *
 * The names a content blocker knows the resource types by.
 *
 * Returns: (transfer full) (element-type utf8): the types
 */
static GPtrArray *
dnr_resource_types (JsonArray *array)
{
  GPtrArray *types = g_ptr_array_new_with_free_func (g_free);

  if (!array)
    return types;

  for (guint i = 0; i < json_array_get_length (array); i++) {
    const char *name = json_array_get_string_element (array, i);
    const char *type = name ? resource_type_for (name) : NULL;

    if (type)
      g_ptr_array_add (types, g_strdup (type));
  }

  return types;
}

static GPtrArray *
dnr_domains (JsonArray *array)
{
  GPtrArray *domains = g_ptr_array_new_with_free_func (g_free);

  if (!array)
    return domains;

  for (guint i = 0; i < json_array_get_length (array); i++) {
    const char *name = json_array_get_string_element (array, i);

    if (name && *name)
      g_ptr_array_add (domains, g_strdup (name));
  }

  return domains;
}

/**
 * ephy_filters_compile_dnr:
 * @rules: the rules, as the browser.declarativeNetRequest API describes
 * @stats: (out) (optional): what the rules came to
 * @error: (out) (optional): the error
 *
 * Turns the rules of an extension's declarative net request ruleset into
 * the rules that WebKit's content blockers understand.
 *
 * The rules that allow something are put first, because a content
 * blocker stops at the first rule that matches.
 *
 * Returns: (transfer full) (nullable): the rules, as JSON
 */
char *
ephy_filters_compile_dnr (JsonArray              *rules,
                          EphyFiltersCompileStats *stats,
                          GError               **error)
{
  Compiler compiler;
  GPtrArray *exceptions = g_ptr_array_new_with_free_func (g_object_unref);
  GPtrArray *blocks = g_ptr_array_new_with_free_func (g_object_unref);
  char *json;

  g_return_val_if_fail (rules, NULL);

  compiler_init (&compiler);

  for (guint i = 0; i < json_array_get_length (rules); i++) {
    JsonObject *rule = json_array_get_object_element (rules, i);
    JsonObject *action;
    JsonObject *condition;
    const char *action_type;
    const char *url_filter = NULL;
    const char *regex_filter = NULL;
    const char *domain_type = NULL;
    gboolean case_sensitive = FALSE;
    gboolean is_exception = FALSE;
    GPtrArray *resource_types = NULL;
    GPtrArray *excluded_types = NULL;
    GPtrArray *domains = NULL;
    GPtrArray *excluded_domains = NULL;
    char *compiled;

    compiler.total++;

    if (!rule) {
      compiler.skipped++;
      continue;
    }

    action = json_object_has_member (rule, "action")
               ? json_object_get_object_member (rule, "action")
               : NULL;
    condition = json_object_has_member (rule, "condition")
                  ? json_object_get_object_member (rule, "condition")
                  : NULL;

    if (!action || !condition) {
      compiler.skipped++;
      continue;
    }

    action_type = json_object_has_member (action, "type")
                    ? json_object_get_string_member (action, "type")
                    : NULL;
    if (!action_type) {
      compiler.skipped++;
      continue;
    }

    if (g_strcmp0 (action_type, "block") == 0) {
      /* nothing to do */
    } else if (g_strcmp0 (action_type, "allow") == 0 ||
               g_strcmp0 (action_type, "allowAllRequests") == 0) {
      is_exception = TRUE;
    } else if (g_strcmp0 (action_type, "upgradeScheme") == 0) {
      /* A content blocker cannot do this one. */
      compiler.skipped++;
      continue;
    } else {
      /* redirect, modifyHeaders and anything new. */
      compiler.skipped++;
      continue;
    }

    if (json_object_has_member (condition, "urlFilter"))
      url_filter = json_object_get_string_member (condition, "urlFilter");
    if (json_object_has_member (condition, "regexFilter"))
      regex_filter = json_object_get_string_member (condition, "regexFilter");


    if (!url_filter && !regex_filter) {
      compiler.skipped++;
      continue;
    }

    if (json_object_has_member (condition, "isUrlFilterCaseSensitive"))
      case_sensitive = json_object_get_boolean_member (condition, "isUrlFilterCaseSensitive");

    resource_types = dnr_resource_types (json_object_has_member (condition, "resourceTypes")
                                           ? json_object_get_array_member (condition, "resourceTypes")
                                           : NULL);
    excluded_types = dnr_resource_types (json_object_has_member (condition, "excludedResourceTypes")
                                           ? json_object_get_array_member (condition, "excludedResourceTypes")
                                           : NULL);
    domains = dnr_domains (json_object_has_member (condition, "initiatorDomains")
                             ? json_object_get_array_member (condition, "initiatorDomains")
                             : NULL);
    excluded_domains = dnr_domains (json_object_has_member (condition, "excludedInitiatorDomains")
                                      ? json_object_get_array_member (condition, "excludedInitiatorDomains")
                                      : NULL);

    if (json_object_has_member (condition, "domainType"))
      domain_type = json_object_get_string_member (condition, "domainType");

    if (regex_filter) {
      /* A regular expression is used as it is. */
      g_autofree char *pattern = g_strdup (regex_filter);

      compiled = make_rule (pattern, resource_types, excluded_types, domains, excluded_domains,
                            domain_type, is_exception ? "ignore-previous-rules" : "block",
                            case_sensitive);
    } else {
      compiled = make_rule (url_filter, resource_types, excluded_types, domains, excluded_domains,
                            domain_type, is_exception ? "ignore-previous-rules" : "block",
                            case_sensitive);
    }

    g_clear_pointer (&resource_types, g_ptr_array_unref);
    g_clear_pointer (&excluded_types, g_ptr_array_unref);
    g_clear_pointer (&domains, g_ptr_array_unref);
    g_clear_pointer (&excluded_domains, g_ptr_array_unref);

    if (!compiled) {
      compiler.skipped++;
      continue;
    }

    compiler.network++;

    if (is_exception)
      g_ptr_array_add (exceptions, compiled);
    else
      g_ptr_array_add (blocks, compiled);
  }

  for (guint i = 0; i < exceptions->len; i++)
    compiler_add (&compiler, g_ptr_array_index (exceptions, i));
  for (guint i = 0; i < blocks->len; i++)
    compiler_add (&compiler, g_ptr_array_index (blocks, i));

  g_ptr_array_set_free_func (exceptions, NULL);
  g_ptr_array_set_free_func (blocks, NULL);
  g_ptr_array_unref (exceptions);
  g_ptr_array_unref (blocks);

  if (stats) {
    stats->total = compiler.total;
    stats->network = compiler.network;
    stats->cosmetic = 0;
    stats->skipped = compiler.skipped;
  }

  json = rules_to_json (compiler.rules);

  compiler_clear (&compiler);

  return json;
}
