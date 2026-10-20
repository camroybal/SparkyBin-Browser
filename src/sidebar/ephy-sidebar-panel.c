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

#include "ephy-sidebar-panel.h"

#include <string.h>

#include <glib/gi18n.h>

#include "ephy-bookmark.h"
#include "ephy-bookmark-row.h"
#include "ephy-bookmarks-manager.h"
#include "ephy-prefs.h"
#include "ephy-settings.h"
#include "ephy-shell.h"

/* The name of the section that holds the bookmarks bar. Every other
 * section is named after a tag. */
#define SECTION_BOOKMARKS "bookmarks"

typedef struct
{
  EphySidebarPanel *panel;
  char              *name;
  GtkWidget         *box;
  GtkWidget         *header;
  GtkWidget         *list;
  gboolean           expanded;
} SidebarSection;

struct _EphySidebarPanel {
  AdwBin parent_instance;

  GtkWidget *search_entry;
  GtkWidget *sections_box;
  GtkWidget *results_box;

  GPtrArray *sections; /* SidebarSection */

  EphyBookmarksManager *manager;
};

G_DEFINE_FINAL_TYPE (EphySidebarPanel, ephy_sidebar_panel, ADW_TYPE_BIN)

static void sidebar_section_free (SidebarSection *section);

static GtkWidget *
create_bookmark_row (EphySidebarPanel *self,
                     EphyBookmark      *bookmark)
{
  /* The row opens the bookmark itself when it is activated. */
  return ephy_bookmark_row_new (bookmark);
}

static void
on_header_clicked (GtkButton *button,
                   gpointer  user_data)
{
  SidebarSection *section = user_data;

  section->expanded = !section->expanded;
  gtk_widget_set_visible (section->list, section->expanded);
}

static GtkWidget *
create_section (EphySidebarPanel *self,
                const char        *name)
{
  SidebarSection *section;
  GtkWidget *header;
  GtkWidget *label;
  GtkWidget *box;
  GtkWidget *list;
  const char *title;

  section = g_new0 (SidebarSection, 1);
  section->panel = self;
  section->name = g_strdup (name);
  section->expanded = TRUE;

  title = g_strcmp0 (name, SECTION_BOOKMARKS) == 0
            ? _("Bookmarks bar")
            : name;

  header = gtk_button_new ();
  gtk_button_set_has_frame (GTK_BUTTON (header), FALSE);
  label = gtk_label_new (title);
  gtk_label_set_xalign (GTK_LABEL (label), 0.0);
  gtk_widget_add_css_class (label, "heading");
  gtk_button_set_child (GTK_BUTTON (header), label);
  g_signal_connect (header, "clicked", G_CALLBACK (on_header_clicked), section);

  list = gtk_list_box_new ();
  gtk_list_box_set_selection_mode (GTK_LIST_BOX (list), GTK_SELECTION_NONE);
  gtk_widget_add_css_class (list, "navigation-sidebar");

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_append (GTK_BOX (box), header);
  gtk_box_append (GTK_BOX (box), list);

  section->box = box;
  section->header = header;
  section->list = list;

  g_ptr_array_add (self->sections, section);

  return box;
}

static void
populate_bookmarks_section (EphySidebarPanel *self,
                            SidebarSection   *section)
{
  EphyBookmarksManager *manager = self->manager;
  GSequence *order;
  GSequenceIter *iter;

  order = ephy_bookmarks_manager_get_bookmarks_order (manager);

  for (iter = g_sequence_get_begin_iter (order);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    GVariant *variant = g_sequence_get (iter);
    const char *type;
    const char *item;
    int index;
    EphyBookmark *bookmark;

    g_variant_get (variant, "(ssi)", &type, &item, &index);

    if (g_strcmp0 (type, "bookmark") != 0)
      continue;

    bookmark = ephy_bookmarks_manager_get_bookmark_by_url (manager, item);
    if (!bookmark)
      continue;

    gtk_list_box_append (GTK_LIST_BOX (section->list),
                         create_bookmark_row (self, bookmark));
  }
}

static void
populate_tag_section (EphySidebarPanel *self,
                      SidebarSection   *section)
{
  GSequence *bookmarks;
  GSequenceIter *iter;

  bookmarks = ephy_bookmarks_manager_get_bookmarks_with_tag (self->manager, section->name);

  for (iter = g_sequence_get_begin_iter (bookmarks);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    gtk_list_box_append (GTK_LIST_BOX (section->list),
                         create_bookmark_row (self, bookmark));
  }

  g_sequence_free (bookmarks);
}

static void
clear_sections (EphySidebarPanel *self)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->sections_box)) != NULL)
    gtk_widget_unparent (child);

  g_ptr_array_set_size (self->sections, 0);
}

static void
populate_sections (EphySidebarPanel *self)
{
  g_autoptr (GVariant) sections = NULL;
  GVariantIter iter;
  const char *name;

  clear_sections (self);

  sections = g_settings_get_value (EPHY_SETTINGS_UI, EPHY_PREFS_UI_SIDEBAR_SECTIONS);
  g_variant_iter_init (&iter, sections);

  while (g_variant_iter_loop (&iter, "s", &name)) {
    SidebarSection *section;
    GtkWidget *box;

    box = create_section (self, name);
    section = g_ptr_array_index (self->sections, self->sections->len - 1);

    if (g_strcmp0 (name, SECTION_BOOKMARKS) == 0)
      populate_bookmarks_section (self, section);
    else
      populate_tag_section (self, section);

    gtk_box_append (GTK_BOX (self->sections_box), box);
  }
}

static gboolean
bookmark_matches (EphyBookmark  *bookmark,
                  const char    *needle)
{
  g_autofree char *title = NULL;
  g_autofree char *url = NULL;
  g_autofree char *title_folded = NULL;
  g_autofree char *url_folded = NULL;

  title = g_utf8_casefold (ephy_bookmark_get_title (bookmark), -1);
  url = g_utf8_casefold (ephy_bookmark_get_url (bookmark), -1);

  title_folded = g_utf8_casefold (title, -1);
  url_folded = g_utf8_casefold (url, -1);

  return strstr (title_folded, needle) != NULL || strstr (url_folded, needle) != NULL;
}

static void
populate_results (EphySidebarPanel *self)
{
  const char *text;
  g_autofree char *needle = NULL;
  GSequence *bookmarks;
  GSequenceIter *iter;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->results_box)) != NULL)
    gtk_widget_unparent (child);

  text = gtk_editable_get_text (GTK_EDITABLE (self->search_entry));
  needle = g_utf8_casefold (text, -1);

  if (*needle == '\0')
    return;

  bookmarks = ephy_bookmarks_manager_get_bookmarks (self->manager);

  for (iter = g_sequence_get_begin_iter (bookmarks);
       !g_sequence_iter_is_end (iter);
       iter = g_sequence_iter_next (iter)) {
    EphyBookmark *bookmark = g_sequence_get (iter);

    if (!bookmark_matches (bookmark, needle))
      continue;

    gtk_box_append (GTK_BOX (self->results_box),
                    create_bookmark_row (self, bookmark));
  }

  g_sequence_free (bookmarks);
}

static void
on_search_changed (GtkEditable *editable,
                   gpointer     user_data)
{
  EphySidebarPanel *self = EPHY_SIDEBAR_PANEL (user_data);
  const char *text;
  gboolean searching;

  text = gtk_editable_get_text (editable);
  searching = *text != '\0';

  gtk_widget_set_visible (self->sections_box, !searching);
  gtk_widget_set_visible (self->results_box, searching);

  if (searching)
    populate_results (self);
}

static void
on_bookmarks_changed (EphyBookmarksManager *manager,
                      gpointer             user_data)
{
  EphySidebarPanel *self = EPHY_SIDEBAR_PANEL (user_data);

  populate_sections (self);

  if (gtk_widget_get_visible (self->results_box))
    populate_results (self);
}

static void
on_bookmark_added (EphyBookmarksManager *manager,
                   EphyBookmark         *bookmark,
                   gpointer             user_data)
{
  on_bookmarks_changed (manager, user_data);
}

static void
on_bookmark_removed (EphyBookmarksManager *manager,
                     EphyBookmark         *bookmark,
                     gpointer             user_data)
{
  on_bookmarks_changed (manager, user_data);
}

static void
on_bookmark_title_changed (EphyBookmarksManager *manager,
                           EphyBookmark         *bookmark,
                           gpointer             user_data)
{
  on_bookmarks_changed (manager, user_data);
}

static void
on_bookmark_tag_changed (EphyBookmarksManager *manager,
                         EphyBookmark         *bookmark,
                         const char           *tag,
                         gpointer             user_data)
{
  on_bookmarks_changed (manager, user_data);
}

static void
on_tag_changed (EphyBookmarksManager *manager,
                const char           *tag,
                gpointer             user_data)
{
  on_bookmarks_changed (manager, user_data);
}

static void
on_sections_changed (GSettings *settings,
                     const char *key,
                     gpointer   user_data)
{
  EphySidebarPanel *self = EPHY_SIDEBAR_PANEL (user_data);

  populate_sections (self);
}

static void
ephy_sidebar_panel_dispose (GObject *object)
{
  EphySidebarPanel *self = EPHY_SIDEBAR_PANEL (object);

  g_clear_pointer (&self->sections, g_ptr_array_unref);
  g_clear_object (&self->manager);

  G_OBJECT_CLASS (ephy_sidebar_panel_parent_class)->dispose (object);
}

static void
ephy_sidebar_panel_class_init (EphySidebarPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = ephy_sidebar_panel_dispose;
}

static void
ephy_sidebar_panel_init (EphySidebarPanel *self)
{
  GtkWidget *scrolled;
  GtkWidget *box;
  GtkWidget *content;

  self->manager = ephy_shell_get_bookmarks_manager (ephy_shell_get_default ());
  self->sections = g_ptr_array_new_with_free_func ((GDestroyNotify) sidebar_section_free);

  box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->search_entry = gtk_search_entry_new ();
  gtk_search_entry_set_placeholder_text (GTK_SEARCH_ENTRY (self->search_entry),
                                         _("Search bookmarks"));
  gtk_widget_set_margin_top (self->search_entry, 6);
  gtk_widget_set_margin_bottom (self->search_entry, 6);
  gtk_widget_set_margin_start (self->search_entry, 6);
  gtk_widget_set_margin_end (self->search_entry, 6);
  g_signal_connect (self->search_entry, "changed", G_CALLBACK (on_search_changed), self);
  gtk_box_append (GTK_BOX (box), self->search_entry);

  scrolled = gtk_scrolled_window_new ();
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand (scrolled, TRUE);

  /* A scrolled window holds one child, and the sections and the search
   * results take turns, so they go in a box together. */
  content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);

  self->sections_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top (self->sections_box, 6);
  gtk_widget_set_margin_bottom (self->sections_box, 6);
  gtk_box_append (GTK_BOX (content), self->sections_box);

  self->results_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_visible (self->results_box, FALSE);
  gtk_box_append (GTK_BOX (content), self->results_box);

  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled), content);

  gtk_box_append (GTK_BOX (box), scrolled);

  adw_bin_set_child (ADW_BIN (self), box);

  g_signal_connect (self->manager, "bookmark-added", G_CALLBACK (on_bookmark_added), self);
  g_signal_connect (self->manager, "bookmark-removed", G_CALLBACK (on_bookmark_removed), self);
  g_signal_connect (self->manager, "bookmark-title-changed", G_CALLBACK (on_bookmark_title_changed), self);
  g_signal_connect (self->manager, "bookmark-url-changed", G_CALLBACK (on_bookmark_title_changed), self);
  g_signal_connect (self->manager, "bookmark-tag-added", G_CALLBACK (on_bookmark_tag_changed), self);
  g_signal_connect (self->manager, "bookmark-tag-removed", G_CALLBACK (on_bookmark_tag_changed), self);
  g_signal_connect (self->manager, "tag-created", G_CALLBACK (on_tag_changed), self);
  g_signal_connect (self->manager, "tag-deleted", G_CALLBACK (on_tag_changed), self);
  g_signal_connect (EPHY_SETTINGS_UI, "changed::" EPHY_PREFS_UI_SIDEBAR_SECTIONS,
                    G_CALLBACK (on_sections_changed), self);

  populate_sections (self);
}

static void
sidebar_section_free (SidebarSection *section)
{
  g_free (section->name);
  g_free (section);
}

/**
 * ephy_sidebar_panel_new:
 *
 * Creates the panel that goes in the browser's sidebar.
 *
 * Returns: (transfer full): the new panel
 */
GtkWidget *
ephy_sidebar_panel_new (void)
{
  return g_object_new (EPHY_TYPE_SIDEBAR_PANEL, NULL);
}
