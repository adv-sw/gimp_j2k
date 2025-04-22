/* ----------------------------------------------------------------

Project : GIMP / JPEG-2000 plugin

Copyright (C) 2008-2025 Advance Software Limited.

Licensed under GNU General Public License V3.
License terms are available here : http://www.gnu.org/licenses/gpl.html

This software requires :

GIMP 3, OpenJPEG 2.3.1

Compiles on Windows (mingw64) and Linux.

------------------------------------------------------------------- */

#ifndef __GIMP_J2K_MAIN_H__
#define __GIMP_J2K_MAIN_H__


#define ENABLE_OPENJPEG_DIAGNOSTIC 0


#ifndef MSVC
#define stricmp strcasecmp
#define strnicmp strncasecmp
typedef int bool;
#define true   1
#define false  0
#define nullptr NULL
#endif

#include <openjpeg.h>



#define LOAD_THUMB_PROC "j2k-load-thumb"
#define SAVE_PROC       "j2k-save"


#define DATA_KEY_VALS    "plug_in_j2k"
#define DATA_KEY_UI_VALS "plug_in_j2k_ui"
#define PARASITE_KEY     "plug-in-j2k-options"

#define __ENABLE_PREVIEW_BACKGROUND_THREAD_EXIT_TIMEOUT 0

extern gint32 volatile  preview_image_ID;
extern gint32           preview_layer_ID;
extern gchar           *image_comment;
extern GimpDrawable    *drawable_global;
extern gboolean         undo_touched;
extern gint32           display_ID;

typedef unsigned long int uint32;
typedef unsigned char     uint8;


opj_image_t *image_load(const gchar *filename);
opj_image_t *decode_image(guint8 *src, uint32 file_length, bool format_codestream);
GimpImage *image_to_gimp(opj_image_t *image, const char *filename, gboolean preview);


#endif /* __GIMP_J2K_MAIN_H__ */
