/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil, c-basic-offset: 2 -*- */
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

#include "ephy-web-extension.h"

G_BEGIN_DECLS

void ephy_web_extension_api_permissions_handler (EphyWebExtensionSender *sender,
                                                 const char             *method_name,
                                                 JsonArray              *args,
                                                 GTask                  *task);

G_END_DECLS
