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
#include "ephy-settings.h"
#include "ephy-shell.h"

#define SECTION_BOOKMARKS "bookmarks"

enum {
  MANAGE_BOOKMARKS_ROW_ACTIVATED,
  LAST_SIGNAL
};

struct _EphyPrefsSidebarPage {
  AdwPreferencesPage parent_instance;

  GtkWidget *visible_row;
  GtkWidget *width_row;
  GtkWidget *sections_group;
  GtkWidget *add_row;

  GPtrArray *section_rows;

  EphyBookmarksManager *manager;
};

G_DEFINE_FINAL_TYPE (EphyPrefsSidebarPage, ephy_prefs_sidebar_page, ADW_TYPE_PREFERENCES_PAGE)

static guint signals[LAST_SIGNAL];

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

static const char *
section_title (const char *name)
{
  if (g_strcmp0 (name, SECTION_BOOKMARKS) == 0)
    return _("Bookmarks bar");

  return name;
}

static int
count_bookmarks_in_section (EphyPrefsSidebarPage *self,
                            const char           *name)
{
  GSequence *bookmarks;
  int count;

  if (g_strcmp0 (name, SECTION_BOOKMARKS) == 0) {
    GSequence *order = ephy_bookmarks_manager_get_bookmarks_order (self->manager);
    GSequenceIter *iter;
    int bookmarks_count = 0;

    for (iter = g_sequence_get_begin_iter (order);
         !g_sequence_iter_is_end (iter);
         iter = g_sequence_iter_next (iter)) {
      GVariant *variant = g_sequence_get (iter);
      const char *type;
      const char *item;
      int index;

      g_variant_get (variant, "(ssi)", &type, &item, &index);
      if (g_strcmp0 (type, "bookmark") == 0)
        bookmarks_count++;
    }

    return bookmarks_count;
  }

  bookmarks = ephy_bookmarks_manager_get_bookmarks_with_tag (self->manager, name);
  count = (int) g_sequence_get_length (bookmarks);
  g_sequence_free (bookmarks);

  return count;
}

static void
rebuild_sections (EphyPrefsSidebarPage *self)
{
  g_autofree char **sections = NULL;
  guint i;
  int count;
  int index;

  /* The rows are ours, so they are removed by hand. The group's first
   * child is its own layout, not one of ours. */
  for (i = 0; i < self->section_rows->len; i++) {
    adw_preferences_group_remove (ADW_PREFERENCES_GROUP (self->sections_group),
                                  g_ptr_array_index (self->section_rows, i));
  }
  g_ptr_array_set_size (self->section_rows, 0);

  get_sections (self, &sections);
  count = g_strv_length (sections);

  for (index = 0; index < count; index++) {
    GtkWidget *actions;
    GtkWidget *up;
    GtkWidget *down;
    GtkWidget *remove;
    g_autofree char *subtitle = NULL;
    int bookmarks_count;
    GtkWidget *row;

    row = adw_action_row_new ();
    adw_preferences_row_set_title (ADW_PREFERENCES_ROW (row), section_title (sections[index]));

    bookmarks_count = count_bookmarks_in_section (self, sections[index]);
    subtitle = g_strdup_printf (ngettext ("%d bookmark", "%d bookmarks", bookmarks_count),
                                 bookmarks_count);
    adw_action_row_set_subtitle (ADW_ACTION_ROW (row), subtitle);

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
    g_ptr_array_add (self->section_rows, row);
  }
}

static void
on_tag_row_activated (GtkListBoxRow *row,
                       gpointer      user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  const char *name = g_object_get_data (G_OBJECT (row), "section-name");

  append_section (self, name);
  gtk_popover_popdown (GTK_POPOVER (gtk_widget_get_parent (GTK_WIDGET (row))));
}

static void
on_new_section_activated (GtkEntry *entry,
                          gpointer  user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  const char *text = gtk_editable_get_text (GTK_EDITABLE (entry));
  g_autofree char *name = NULL;

  name = g_strstrip (g_strdup (text));

  if (*name == '\0')
    return;

  /* A section that is not a tag yet becomes one, so that it can be given
   * bookmarks. */
  if (!ephy_bookmarks_manager_tag_exists (self->manager, name))
    ephy_bookmarks_manager_create_tag (self->manager, name);

  append_section (self, name);
}

static void
on_add_row_activated (AdwActionRow *row,
                      gpointer     user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);
  GSequence *tags;
  GSequenceIter *iter;

  /* The popover is created once and filled in each time, so that it does
   * not have to be built again. */
  GtkWidget *popover = g_object_get_data (G_OBJECT (self), "popover");
  GtkWidget *list_box = g_object_get_data (G_OBJECT (self), "popover-list");
  GtkWidget *entry = g_object_get_data (G_OBJECT (self), "popover-entry");
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (list_box)) != NULL)
    gtk_widget_unparent (child);

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
on_sections_changed (GSettings   *settings,
                     const char *key,
                     gpointer   user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);

  rebuild_sections (self);
}

static void
on_manage_row_activated (AdwActionRow *row,
                         gpointer     user_data)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (user_data);

  g_signal_emit (self, signals[MANAGE_BOOKMARKS_ROW_ACTIVATED], 0);
}

static void
on_width_changed (AdwSpinRow *row,
                  gpointer   user_data)
{
  g_settings_set_int (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_WIDTH,
                      (int) adw_spin_row_get_value (row));
}

static void
ephy_prefs_sidebar_page_dispose (GObject *object)
{
  EphyPrefsSidebarPage *self = EPHY_PREFS_SIDEBAR_PAGE (object);

  g_clear_pointer (&self->section_rows, g_ptr_array_unref);
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

  gtk_widget_class_bind_template_child (widget_class, EphyPrefsSidebarPage, visible_row);
  gtk_widget_class_bind_template_child (widget_class, EphyPrefsSidebarPage, width_row);
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
  g_autoptr (GVariant) width = NULL;
  GtkWidget *popover;
  GtkWidget *popover_box;
  GtkWidget *entry;
  GtkWidget *list_box;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->manager = ephy_shell_get_bookmarks_manager (ephy_shell_get_default ());
  self->section_rows = g_ptr_array_new ();

  g_settings_bind (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_VISIBLE,
                   self->visible_row, "active", G_SETTINGS_BIND_DEFAULT);

  width = g_settings_get_value (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_WIDTH);
  adw_spin_row_set_value (ADW_SPIN_ROW (self->width_row), g_variant_get_int32 (width));
  g_signal_connect (self->width_row, "notify::value", G_CALLBACK (on_width_changed), self);

  /* The popover for adding a section. */
  popover = gtk_popover_new ();
  popover_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (popover_box, 6);
  gtk_widget_set_margin_bottom (popover_box, 6);
  gtk_widget_set_margin_start (popover_box, 6);
  gtk_widget_set_margin_end (popover_box, 6);

  entry = gtk_entry_new ();
  gtk_entry_set_placeholder_text (GTK_ENTRY (entry), _("New section"));
  g_signal_connect (entry, "activate", G_CALLBACK (on_new_section_activated), self);
  gtk_box_append (GTK_BOX (popover_box), entry);

  list_box = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list_box), GTK_SELECTION_NONE);
  gtk_box_append (GTK_BOX (popover_box), list_box);

  gtk_popover_set_child (GTK_POPOVER (popover), popover_box);
  gtk_widget_set_parent (popover, self->add_row);

  g_object_set_data (G_OBJECT (self), "popover", popover);
  g_object_set_data (G_OBJECT (self), "popover-list", list_box);
  g_object_set_data (G_OBJECT (self), "popover-entry", entry);

  g_signal_connect (EPHY_SETTINGS_UI, "changed::" EPHY_PREFS_UI_SIDEBAR_SECTIONS,
                    G_CALLBACK (on_sections_changed), self);

  rebuild_sections (self);
}

/**
 * ephy_prefs_sidebar_page_new:
 *
 * Creates the preferences page for the sidebar.
 *
 * Returns: (transfer full): the new page
 */
GtkWidget *
ephy_prefs_sidebar_page_new (void)
{
  return g_object_new (EPHY_TYPE_PREFS_SIDEBAR_PAGE, NULL);
}
