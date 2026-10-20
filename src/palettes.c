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

#include "palettes.h"

#include <string.h>

/*
 * The colours of each palette. The desktop's own colours come first, under
 * the name System, and are the ones the browser uses when nothing has been
 * picked.
 *
 * The rest are named after the projects they come from. The colours are
 * those the projects use themselves.
 */
static const struct {
  const char *name;
  const char *slug;
  const char *bg;
  const char *fg;
  const char *surface;
  const char *card;
  const char *accent;
  const char *accent_fg;
  const char *success;
  const char *warning;
  const char *error;
} palette_data[] = {
  { "System", "system", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },

  { "Gruvbox Dark", "gruvbox-dark",
    "#282828", "#ebdbb2", "#32302f", "#3c3836",
    "#fabd2f", "#282828", "#b8bb26", "#fe8019", "#ff7f6b" },

  { "Gruvbox Light", "gruvbox-light",
    "#fbf1c7", "#3c3836", "#f2e5bc", "#ebdbb2",
    "#976000", "#fbf1c7", "#666100", "#af3a03", "#9d0006" },

  { "Dracula", "dracula",
    "#282a36", "#f8f8f2", "#21222c", "#343746",
    "#bd93f9", "#282a36", "#50fa7b", "#ffb86c", "#ff5555" },

  { "Catppuccin Mocha", "catppuccin-mocha",
    "#1e1e2e", "#cdd6f4", "#181825", "#313244",
    "#cba6f7", "#1e1e2e", "#a6e3a1", "#f9e2af", "#f38ba8" },

  { "Catppuccin Latte", "catppuccin-latte",
    "#eff1f5", "#4c4f69", "#e6e9ef", "#ccd0da",
    "#8839ef", "#eff1f5", "#176600", "#7d4c00", "#d20f39" },

  { "Nord", "nord",
    "#2e3440", "#d8dee9", "#3b4252", "#434c5e",
    "#88c0d0", "#2e3440", "#a3be8c", "#ebcb8b", "#ffa8ae" },

  { "Tokyo Night", "tokyo-night",
    "#1a1b26", "#c0caf5", "#16161e", "#24283b",
    "#7aa2f7", "#1a1b26", "#9ece6a", "#e0af68", "#f7768e" },

  { "Solarized Dark", "solarized-dark",
    "#002b36", "#93a1a1", "#073642", "#002b36",
    "#3395dd", "#002b36", "#859900", "#b58900", "#fb554d" },

  { "One Dark", "one-dark",
    "#282c34", "#abb2bf", "#21252b", "#2c313c",
    "#61afef", "#282c34", "#98c379", "#e5c07b", "#eb7880" },

  { "Rosé Pine", "rose-pine",
    "#191724", "#e0def4", "#1f1d2e", "#26233a",
    "#c4a7e7", "#191724", "#9ccfd8", "#f6c177", "#eb6f92" },

  { "Kanagawa", "kanagawa",
    "#1f1f28", "#dcd7ba", "#16161d", "#2a2a37",
    "#7e9cd8", "#1f1f28", "#98bb6c", "#e6c384", "#ff5b50" },

  { "Everforest", "everforest",
    "#2d353b", "#d3c6aa", "#232a2e", "#343f44",
    "#a7c080", "#2d353b", "#a7c080", "#dbbc7f", "#e67e80" },

  { "Monokai Pro", "monokai-pro",
    "#222222", "#e6e0d5", "#1c1c1c", "#2d2a2e",
    "#ffd866", "#222222", "#78dce8", "#ff9c43", "#ff6188" },

  { "Ayu Mirage", "ayu-mirage",
    "#1f2430", "#cbccc6", "#1c1f26", "#242c38",
    "#ffcc66", "#1f2430", "#bae67e", "#ffb454", "#f28779" },

  { "Poimandres", "poimandres",
    "#1a1b26", "#e4f0fb", "#12131c", "#24273a",
    "#89ddff", "#1a1b26", "#5de4c7", "#f6d373", "#d0679d" },
};

G_DEFINE_FINAL_TYPE (EphyPalette, ephy_palette, G_TYPE_OBJECT)

static void
ephy_palette_finalize (GObject *object)
{
  EphyPalette *palette = EPHY_PALETTE (object);

  g_clear_pointer (&palette->name, g_free);
  g_clear_pointer (&palette->slug, g_free);
  g_clear_pointer (&palette->bg, g_free);
  g_clear_pointer (&palette->fg, g_free);
  g_clear_pointer (&palette->surface, g_free);
  g_clear_pointer (&palette->card, g_free);
  g_clear_pointer (&palette->accent, g_free);
  g_clear_pointer (&palette->accent_fg, g_free);
  g_clear_pointer (&palette->success, g_free);
  g_clear_pointer (&palette->warning, g_free);
  g_clear_pointer (&palette->error, g_free);

  G_OBJECT_CLASS (ephy_palette_parent_class)->finalize (object);
}

static void
ephy_palette_class_init (EphyPaletteClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ephy_palette_finalize;
}

static void
ephy_palette_init (EphyPalette *palette)
{
}

/**
 * ephy_palette_get_all:
 *
 * Every palette that can be picked, in the order they are shown.
 *
 * Returns: (transfer full) (element-type EphyPalette): the palettes
 */
GPtrArray *
ephy_palette_get_all (void)
{
  GPtrArray *palettes = g_ptr_array_new_with_free_func (g_object_unref);

  for (size_t i = 0; i < G_N_ELEMENTS (palette_data); i++) {
    EphyPalette *palette = g_object_new (EPHY_TYPE_PALETTE, NULL);

    palette->name = g_strdup (palette_data[i].name);
    palette->slug = g_strdup (palette_data[i].slug);
    palette->bg = g_strdup (palette_data[i].bg);
    palette->fg = g_strdup (palette_data[i].fg);
    palette->surface = g_strdup (palette_data[i].surface);
    palette->card = g_strdup (palette_data[i].card);
    palette->accent = g_strdup (palette_data[i].accent);
    palette->accent_fg = g_strdup (palette_data[i].accent_fg);
    palette->success = g_strdup (palette_data[i].success);
    palette->warning = g_strdup (palette_data[i].warning);
    palette->error = g_strdup (palette_data[i].error);

    g_ptr_array_add (palettes, palette);
  }

  return palettes;
}

/**
 * ephy_palette_find:
 * @slug: the name of the palette
 *
 * Finds a palette by its name.
 *
 * Returns: (transfer full) (nullable): the palette, or %NULL
 */
EphyPalette *
ephy_palette_find (const char *slug)
{
  g_autoptr (GPtrArray) palettes = NULL;
  EphyPalette *found = NULL;

  if (!slug)
    return NULL;

  palettes = ephy_palette_get_all ();

  for (guint i = 0; i < palettes->len; i++) {
    EphyPalette *palette = g_ptr_array_index (palettes, i);

    if (g_strcmp0 (palette->slug, slug) == 0) {
      found = g_object_ref (palette);
      break;
    }
  }

  return found;
}

/**
 * ephy_palette_to_css:
 * @palette: a palette
 *
 * Builds the stylesheet that sets the colours. The names are the ones
 * libadwaita uses, so the rest of the toolkit follows from these.
 *
 * Returns: (transfer full): the stylesheet
 */
char *
ephy_palette_to_css (EphyPalette *palette)
{
  static const struct {
    const char *name;
    int colour;
  } names[] = {
    { "window_bg_color", 0 },
    { "window_fg_color", 1 },
    { "view_bg_color", 0 },
    { "view_fg_color", 1 },
    { "dialog_bg_color", 0 },
    { "dialog_fg_color", 1 },
    { "headerbar_bg_color", 2 },
    { "headerbar_fg_color", 1 },
    { "headerbar_border_color", 1 },
    { "headerbar_backdrop_color", 2 },
    { "headerbar_shade_color", 1 },
    { "sidebar_bg_color", 2 },
    { "sidebar_fg_color", 1 },
    { "sidebar_backdrop_color", 2 },
    { "sidebar_border_color", 1 },
    { "sidebar_shade_color", 1 },
    { "secondary_sidebar_bg_color", 2 },
    { "secondary_sidebar_fg_color", 1 },
    { "secondary_sidebar_backdrop_color", 2 },
    { "secondary_sidebar_border_color", 1 },
    { "card_bg_color", 3 },
    { "card_fg_color", 1 },
    { "popover_bg_color", 3 },
    { "popover_fg_color", 1 },
    { "accent_bg_color", 4 },
    { "accent_fg_color", 5 },
    { "success_bg_color", 6 },
    { "success_fg_color", 5 },
    { "warning_bg_color", 7 },
    { "warning_fg_color", 5 },
    { "error_bg_color", 8 },
    { "error_fg_color", 5 },
    { "destructive_bg_color", 8 },
    { "destructive_fg_color", 5 },
  };

  const char *colours[9];
  GString *css;

  if (!palette->bg)
    return g_strdup ("");

  colours[0] = palette->bg;
  colours[1] = palette->fg;
  colours[2] = palette->surface;
  colours[3] = palette->card;
  colours[4] = palette->accent;
  colours[5] = palette->accent_fg;
  colours[6] = palette->success;
  colours[7] = palette->warning;
  colours[8] = palette->error;

  css = g_string_new (NULL);

  for (size_t i = 0; i < G_N_ELEMENTS (names); i++) {
    const char *colour = colours[names[i].colour];

    /* The borders and the shades are the text colour, not quite opaque. */
    if (g_str_has_suffix (names[i].name, "_border_color") ||
        g_str_has_suffix (names[i].name, "_shade_color"))
      g_string_append_printf (css, "@define-color %s alpha(%s, 0.12);\n", names[i].name, colour);
    else
      g_string_append_printf (css, "@define-color %s %s;\n", names[i].name, colour);
  }

  return g_string_free (css, FALSE);
}
