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

#include <glib-object.h>

G_BEGIN_DECLS

#define EPHY_TYPE_PALETTE (ephy_palette_get_type ())

G_DECLARE_FINAL_TYPE (EphyPalette, ephy_palette, EPHY, PALETTE, GObject)

/*
 * A set of colours, in the form libadwaita uses. Only the ones that are
 * not derived from the others are here; libadwaita works out the rest.
 */
struct _EphyPalette {
  GObject parent_instance;

  char *name;
  char *slug;

  char *bg;
  char *fg;
  char *surface;
  char *card;
  char *accent;
  char *accent_fg;
  char *success;
  char *warning;
  char *error;
};

/* The palettes that can be picked, in the order they are shown. The first
 * one is the desktop's own colours. */
GPtrArray *ephy_palette_get_all (void);

/* The palette with this slug, or NULL if there is none. */
EphyPalette *ephy_palette_find (const char *slug);

/* The CSS that sets the colours, as a string the caller must free. */
char *ephy_palette_to_css (EphyPalette *palette);

G_END_DECLS
