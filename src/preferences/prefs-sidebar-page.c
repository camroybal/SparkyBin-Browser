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

#include "prefs-sidebar-page.h"

#include <glib/gi18n.h>

#include "ephy-bookmarks-manager.h"
#include "ephy-prefs.h"
#include "ephy-shell.h"
#include "ephy-settings.h"
#include "palettes.h"

struct _EphyPrefsSidebarPage {
  AdwPreferencesPage parent_instance;

  GtkWidget *palette_group;
  GtkWidget *sections_group;
  GtkWidget *sections_list;
  GtkWidget *add_row;

  EphyBookmarksManager *manager;
};

G_DEFINE_FINAL_TYPE (EphyPrefsSidebarPage, ephy_prefs_sidebar_page, ADW_TYPE_PREFERENCES_PAGE)

enum {
  MANAGE_BOOKMARKS_ROW_ACTIVATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void get_sections (EphyPrefsSidebarPage *self, char ***sections);
static void set_sections (EphyPrefsSidebarPage *self, char **sections);
static gboolean section_exists (EphyPrefsSidebarPage *self, const char *name);
static void append_section (EphyPrefsSidebarPage *self, const char *name);

/* Colours */

static void
on_palette_selected (AdwActionRow *row,
                     gpointer    user_data)
{
  const char *slug = g_object_get_data (G_OBJECT (row), "palette");

  g_settings_set_string (EPHY_SETTINGS_UI, EPHY_PREFS_UI_PALETTE, slug);
}

static void
swatch_snapshot (GtkWidget    *widget,
                 GtkSnapshot  *snapshot)
{
  const char *colour = g_object_get_data (G_OBJECT (widget), "colour");
  GtkAllocation allocation;
  GdkRGBA rgba;
  graphene_rect_t bounds;
  GskRoundedRect outline;
  graphene_size_t corner = GRAPHENE_SIZE_INIT (6, 6);
  float widths[4] = { 1.0, 1.0, 1.0, 1.0 };
  GdkRGBA borders[4];

  if (!colour || !gdk_rgba_parse (&rgba, colour))
    return;

  allocation.width = gtk_widget_get_width (widget);
  allocation.height = gtk_widget_get_height (widget);
  graphene_rect_init (&bounds, 0, 0, allocation.width, allocation.height);
  gtk_snapshot_append_color (snapshot, &rgba, &bounds);

  gsk_rounded_rect_init (&outline, &bounds, &corner, &corner, &corner, &corner);
  borders[0] = rgba;
  borders[1] = rgba;
  borders[2] = rgba;
  borders[3] = rgba;
  gtk_snapshot_append_border (snapshot, &outline, widths, borders);
}

static GtkWidget *
create_swatch (const char *colour)
{
  GtkWidget *drawing;

  drawing = g_object_new (GTK_TYPE_DRAWING_AREA, NULL);
  gtk_widget_set_size_request (drawing, 20, 20);
  gtk_widget_add_css_class (drawing, "palette-swatch");
  g_object_set_data_full (G_OBJECT (drawing), "colour", g_strdup (colour), g_free);
  g_signal_connect (drawing, "snapshot", G_CALLBACK (swatch_snapshot), NULL);

  return drawing;
}

static GtkWidget *
create_palette_row (EphyPalette *palette)
{
  GtkWidget *row;
  GtkWidget *box;
  GtkWidget *swatches;
  const char *colours[3];

  row = adw_action_row_new ();
  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), palette->name);

  swatches = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_valign (swatches, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (swatches, "palette-swatches");

  if (palette->bg) {
    colours[0] = palette->bg;
    colours[1] = palette->fg;
    colours[2] = palette->accent;
  } else {
    /* The desktop's own colours, which are not known here. */
    colours[0] = "#fafafb";
    colours[1] = "#2e3436";
    colours[2] = "#3584e4";
  }

  for (int i = 0; i < 3; i++)
    gtk_box_append (GTK_BOX (swatches), create_swatch (colours[i]));

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
  gtk_box_append (GTK_BOX (box), swatches);

  adw_action_row_add_suffix (ADW_ACTION_ROW (row), box);

  g_object_set_data_full (G_OBJECT (row), "palette", g_strdup (palette->slug), g_free);
  g_signal_connect (row, "activated", G_CALLBACK (on_palette_selected), NULL);

  return row;
}

static void
update_palette_selection (EphyPrefsSidebarPage *self)
{
  g_autofree char *current = NULL;
  GtkWidget *child;

  current = g_settings_get_string (EPHY_SETTINGS_UI, EPHY_PREFS_UI_PALETTE);

  for (child = gtk_widget_get_first_child (self->palette_group); child;
       child = gtk_widget_get_next_sibling (child)) {
    const char *slug = g_object_get_data (G_OBJECT (child), "palette");

    if (!slug)
      continue;

    if (g_strcmp0 (slug, current) == 0)
      gtk_widget_add_css_class (child, "palette-selected");
    else
      gtk_widget_remove_css_class (child, "palette-selected");
  }
}

static void
on_palette_changed (GSettings   *settings,
                     const char *key,
                     gpointer   user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);

  update_palette_selection (self);
}

static void
build_palette_group (EphyPrefsSidebarPage *self)
{
  g_autoptr (GPtrArray) palettes = NULL;
  guint i;

  palettes = ephy_palette_get_all ();

  for (i = 0; i < palettes->len; i++) {
    EphyPalette *palette = g_ptr_array_index (palettes, i);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->palette_group),
                               create_palette_row (palette));
  }

  update_palette_selection (self);
}

/* Sections */

static gboolean
section_exists (EphyPrefsSidebarPage *self,
                const char           *name)
{
  g_autofree char **sections = NULL;
  int i;

  get_sections (self, &sections);

  for (i = 0; sections[i] != NULL; i++) {
    if (g_strcmp0 (sections[i], name) == 0)
      return TRUE;
  }

  return FALSE;
}

static void
append_section (EphyPrefsSidebarPage *self,
                const char           *name)
{
  g_autofree char **sections = NULL;
  int count;

  if (section_exists (self, name))
    return;

  get_sections (self, &sections);
  count = g_strv_length (sections);

  sections = g_realloc (sections, sizeof (char *) * (count + 2));
  sections[count] = g_strdup (name);
  sections[count + 1] = NULL;

  set_sections (self, sections);
}

static void
get_sections (EphyPrefsSidebarPage *self,
              char                ***sections)
{
  g_autoptr (GVariant) value = NULL;
  GVariantIter iter;
  const char *name;
  GPtrArray *result;

  result = g_ptr_array_new_with_free_func (g_free);
  value = g_settings_get_value (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_SECTIONS);

  g_variant_iter_init (&iter, value);
  while (g_variant_iter_next (&iter, "&s", &name))
    g_ptr_array_add (result, g_strdup (name));

  *sections = (char **) g_ptr_array_free (result, FALSE);
}

static void
set_sections (EphyPrefsSidebarPage *self,
              char                **sections)
{
  g_autoptr (GVariant) value = NULL;

  value = g_variant_new ("as", sections, g_strv_length (sections));
  g_settings_set_value (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_SECTIONS, value);
}

static void
move_section (EphyPrefsSidebarPage *self,
              int                   index,
              int                   delta)
{
  g_autofree char **sections = NULL;
  int count;
  char *moved;

  get_sections (self, &sections);
  count = g_strv_length (sections);

  if (index < 0 || index >= count || index + delta < 0 || index + delta >= count)
    return;

  moved = sections[index];
  sections[index] = sections[index + delta];
  sections[index + delta] = moved;

  set_sections (self, sections);
}

static void
remove_section (EphyPrefsSidebarPage *self,
                int                   index)
{
  g_autofree char **sections = NULL;
  int count;
  int i;

  get_sections (self, &sections);
  count = g_strv_length (sections);

  if (index < 0 || index >= count)
    return;

  g_free (sections[index]);
  for (i = index; i < count - 1; i++)
    sections[i] = sections[i + 1];
  sections[count - 1] = NULL;

  set_sections (self, sections);
}

static void
on_up_clicked (GtkButton *button,
               gpointer  user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));

  move_section (self, index, -1);
}

static void
on_down_clicked (GtkButton *button,
                 gpointer  user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));

  move_section (self, index, 1);
}

static void
on_remove_clicked (GtkButton *button,
                   gpointer  user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  int index = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button), "index"));

  remove_section (self, index);
}

static GtkWidget *
create_icon_button (const char           *icon_name,
                    const char           *tooltip,
                    GCallback             callback,
                    EphyPrefsSidebarPage *self,
                    int                   index)
{
  GtkWidget *button;
  GtkWidget *image;

  button = gtk_button_new ();
  gtk_widget_set_valign (button, GTK_ALIGN_CENTER);
  gtk_widget_add_css_class (button, "flat");
  gtk_widget_set_tooltip_text (button, tooltip);
  g_object_set_data (G_OBJECT (button), "index", GINT_TO_POINTER (index));

  image = gtk_image_new_from_icon_name (icon_name);
  gtk_button_set_child (GTK_BUTTON (button), image);

  g_signal_connect (button, "clicked", callback, self);

  return button;
}

static void
rebuild_sections (EphyPrefsSidebarPage *self)
{
  g_autofree char **sections = NULL;
  GtkWidget *child;
  int count;
  int index;

  while ((child = gtk_widget_get_first_child (self->sections_group)) != NULL) {
    if (child == self->add_row)
      break;
    adw_preferences_group_remove (ADW_PREFERENCES_GROUP (self->sections_group), child);
  }

  get_sections (self, &sections);
  count = g_strv_length (sections);

  for (index = 0; index < count; index++) {
    GtkWidget *actions;
    GtkWidget *up;
    GtkWidget *down;
    GtkWidget *remove;
    GtkWidget *row;

    row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), sections[index]);

    actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class (actions, "linked");

    up = create_icon_button ("go-up-symbolic", _("Move up"), G_CALLBACK (on_up_clicked), self, index);
    down = create_icon_button ("go-down-symbolic", _("Move down"), G_CALLBACK (on_down_clicked), self, index);
    remove = create_icon_button ("user-trash-symbolic", _("Remove"), G_CALLBACK (on_remove_clicked), self, index);

    gtk_widget_set_sensitive (up, index > 0);
    gtk_widget_set_sensitive (down, index < count - 1);

    gtk_box_append (GTK_BOX (actions), up);
    gtk_box_append (GTK_BOX (actions), down);
    gtk_box_append (GTK_BOX (actions), remove);

    adw_action_row_add_suffix (ADW_ACTION_ROW (row), actions);

    adw_preferences_group_add (ADW_PREFERENCES_GROUP (self->sections_group), row);
  }
}

static void
on_tag_row_activated (GtkListBoxRow *row,
                       gpointer      user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (row), "section-name");

  append_section (self, name);
}

static void
on_new_section_activated (GtkEntry *entry,
                           gpointer  user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  g_autofree char *name = NULL;

  name = g_strstrip (g_strdup (gtk_editable_get_text (GTK_EDITABLE (entry))));

  if (*name == '\0')
    return;

  /* A section that is not a tag yet becomes one, so that it has somewhere
   * to put bookmarks. */
  if (!ephy_bookmarks_manager_tag_exists (self->manager, name))
    ephy_bookmarks_manager_create_tag (self->manager, name);

  append_section (self, name);

  gtk_editable_set_text (GTK_EDITABLE (entry), "");
}

static void
on_manage_row_activated (AdwActionRow *row,
                          gpointer     user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);

  g_signal_emit (self, signals[MANAGE_BOOKMARKS_ROW_ACTIVATED], 0);
}

static void
on_add_row_activated (AdwActionRow *row,
                       gpointer     user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *entry;
  GtkWidget *list_box;
  GSequence *tags;
  GSequenceIter *iter;

  popover = g_object_get_data (G_OBJECT (self), "popover");
  list_box = g_object_get_data (G_OBJECT (self), "popover-list");
  entry = g_object_get_data (G_OBJECT (self), "popover-entry");

  while ((box = gtk_widget_get_first_child (list_box)) != NULL)
    gtk_widget_unparent (box);

  tags = ephy_bookmarks_manager_get_tags (self->manager);

  for (iter = g_sequence_get_begin_iter (tags);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    const char *tag = g_sequence_get (iter);
    GtkWidget *tag_row;

    if (section_exists (self, tag))
      continue;

    tag_row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (tag_row), tag);
    g_object_set_data_full (G_OBJECT (tag_row), "section-name", g_strdup (tag), g_free);
    g_signal_connect (tag_row, "activated", G_CALLBACK (on_tag_row_activated), self);
    gtk_list_box_append (GTK_LIST_BOX (list_box), tag_row);
  }

  gtk_editable_set_text (GTK_EDITABLE (entry), "");
  gtk_popover_popup (GTK_POPOVER (popover));
}

static void
ephy_prefs_sidebar_page_dispose (GObject *object)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (object);

  g_clear_object (&self->manager);

  G_OBJECT_CLASS (ephy_prefs_sidebar_page_parent_class)->dispose (object);
}

static void
ephy_prefs_sidebar_page_class_init (EphyPrefsSidebarPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = ephy_prefs_sidebar_page_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/com/sparkybin/browser/gtk/prefs-sidebar-page.ui");

  gtk_widget_class_bind_template_child (widget_class, EphyPrefsSidebarPage, palette_group);
  gtk_widget_class_bind_template_child (widget_class, EphyPrefsSidebarPage, sections_group);
  gtk_widget_class_bind_template_child (widget_class, EphyPrefsSidebarPage, add_row);

  gtk_widget_class_bind_template_callback (widget_class, on_add_row_activated);
  gtk_widget_class_bind_template_callback (widget_class, on_manage_row_activated);

  signals[MANAGE_BOOKMARKS_ROW_ACTIVATED] =
    g_signal_new ("manage-bookmarks-row-activated",
                  EPHY_TYPE_PREFS_SIDEBAR_PAGE,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
ephy_prefs_sidebar_page_init (EphyPrefsSidebarPage *self)
{
  GtkWidget *popover;
  GtkWidget *box;
  GtkWidget *entry;
  GtkWidget *list_box;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = ephy_shell_get_bookmarks_manager (ephy_shell_get_default ());

  build_palette_group (self);
  rebuild_sections (self);

  g_signal_connect (EPHY_SETTINGS_UI, "changed::" EPHY_PREFS_UI_PALETTE,
                    G_CALLBACK (on_palette_changed), self);

  /* The popover for adding a section. */
  popover = gtk_popover_new ();
  gtk_popover_set_has_arrow (GTK_POPOVER (popover), FALSE);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (box, 6);
  gtk_widget_set_margin_bottom (box, 6);
  gtk_widget_set_margin_start (box, 6);
  gtk_widget_set_margin_end (box, 6);

  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("New section"));
  g_signal_connect (entry, "activate", G_CALLBACK (on_new_section_activated), self);
  gtk_box_append (GTK_BOX (box), entry);

  list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box), GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (box), list_box);

  gtk_popover_set_child (GTK_POPOVER (popover), box);
  gtk_widget_set_parent (popover, self->add_row);

  g_object_set_data (G_OBJECT (self), "popover", popover);
  g_object_set_data (G_OBJECT (self), "popover-list", list_box);
  g_object_set_data (G_OBJECT (self), "popover-entry", entry);
}

/**
 * ephy_prefs_sidebar_page_new:
 *
 * Creates the preferences page for the sidebar and the colours.
 *
 * Returns: (transfer full): the new page
 */
GtkWidget *
ephy_prefs_sidebar_page_new (void)
{
  return g_object_new (EPHY_TYPE_PREFS_SIDEBAR_PAGE, NULL);
}
