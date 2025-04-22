/*
 * GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __OPENJPG_H__
#define __OPENJPG_H__

#define LOAD_PROC      "file-openjpg-load"
#define EXPORT_PROC    "file-openjpg-export"
#define PLUG_IN_BINARY "file-openjpeg"
#define PLUG_IN_ROLE   "gimp-file-openjpg"

// Not used, common\file-jp2-load.c also uses openjpeg & appears sufficient for our needs.
// This one still requires a little debugging to complete its implementation.

#define ENABLE_J2K_READ_THIS_PLUGIN 0

#endif /* __OPENJPG_H__ */
