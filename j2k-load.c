
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
 * ----------------------------------------------------------------------------
 */


#if ENABLE_J2K_READ_THIS_PLUGIN

#include "config.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>

#include "j2k.h"
#include "j2k-load.h"

#include "libgimp/stdplugins-intl.h"

#include "main.h"


GimpImage *image_to_gimp(opj_image_t *image, const char *filename, gboolean preview);

GimpImage *
load_image (GFile *gfile, GError **error)
{
	gimp_progress_init_printf (_("Opening '%s'"), gimp_file_get_utf8_name (gfile));

	const gchar *path = g_file_get_path(gfile);

	opj_image_t *img = image_load(path);
	
	return image_to_gimp(img, path, false);
}

#endif