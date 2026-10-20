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

#include <adwaita.h>

G_BEGIN_DECLS

#define EPHY_TYPE_SIDEBAR_PANEL (ephy_sidebar_panel_get_type ())

G_DECLARE_FINAL_TYPE (EphySidebarPanel, ephy_sidebar_panel, EPHY, SIDEBAR_PANEL, AdwBin)

GtkWidget *ephy_sidebar_panel_new (void);

G_END_DECLS
