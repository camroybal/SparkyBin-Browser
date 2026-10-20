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

#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

/**
 * EphyFiltersCompileStats:
 * @total: how many lines the list had
 * @network: how many of them became rules that stop requests
 * @cosmetic: how many of them were for hiding things, which is the
 *   extension's business and not the browser's
 * @skipped: how many of them the browser could not use
 *
 * What a list of filters came to.
 */
typedef struct {
  guint total;
  guint network;
  guint cosmetic;
  guint skipped;
} EphyFiltersCompileStats;

/**
 * ephy_filters_compile_abp:
 * @text: the filters, in the Adblock Plus format
 * @stats: (out) (optional): what the list came to
 *
 * Turns a list of filters into the rules that WebKit's content blockers
 * understand.
 *
 * The filters are the Adblock Plus ones: a line per rule, with the
 * network rules and the cosmetic ones mixed in. Only the network rules
 * can be used here, because the cosmetic ones are about hiding things in
 * a page, which is the job of the extension that brought the list.
 *
 * Returns: (transfer full) (nullable): the rules, as JSON
 */
char *ephy_filters_compile_abp (const char            *text,
                                EphyFiltersCompileStats *stats,
                                GError               **error);

/**
 * ephy_filters_compile_dnr:
 * @rules: the rules, as the browser.declarativeNetRequest API describes
 * @stats: (out) (optional): what the rules came to
 *
 * Turns the rules of an extension's declarative net request ruleset into
 * the rules that WebKit's content blockers understand.
 *
 * Returns: (transfer full) (nullable): the rules, as JSON
 */
char *ephy_filters_compile_dnr (JsonArray              *rules,
                                EphyFiltersCompileStats *stats,
                                GError               **error);

G_END_DECLS
