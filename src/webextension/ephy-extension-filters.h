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

#include <webkit/webkit.h>

G_BEGIN_DECLS

#define EPHY_TYPE_EXTENSION_FILTERS (ephy_extension_filters_get_type ())

G_DECLARE_FINAL_TYPE (EphyExtensionFilters, ephy_extension_filters, EPHY, EXTENSION_FILTERS, GObject)

/**
 * ephy_extension_filters_new:
 *
 * Creates the place where the filter lists that come with an extension
 * are kept.
 *
 * Returns: (transfer full): the new object
 */
EphyExtensionFilters *ephy_extension_filters_new (void);

/**
 * ephy_extension_filters_add:
 * @self: the filters
 * @name: the name of the list
 * @text: the filters, in the Adblock Plus format
 *
 * Adds a list of filters and compiles it.
 *
 * The rules are put on the pages once they are compiled, and on any page
 * that is made after that.
 *
 * Returns: %TRUE if the list was understood
 */
gboolean ephy_extension_filters_add (EphyExtensionFilters *self,
                                     const char           *name,
                                     const char           *text,
                                     gsize                 length);

/**
 * ephy_extension_filters_apply:
 * @self: the filters
 * @view: the page
 *
 * Puts the rules on a page.
 */
void ephy_extension_filters_apply (EphyExtensionFilters *self,
                                   WebKitWebView       *view);

G_END_DECLS
