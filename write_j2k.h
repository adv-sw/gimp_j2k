/* ----------------------------------------------------------------

Project : GIMP / JPEG-2000 plugin

Copyright (C) 2008-2025 Advance Software Limited.

Licensed under GNU General Public License V3.
License terms are available here : http://www.gnu.org/licenses/gpl.html

This software requires :

GIMP 3, OpenJPEG 2.3.1

Compiles on Windows (mingw64) and Linux.

------------------------------------------------------------------- */


#ifndef __GIMP_WRITE_J2K_H__
#define __GIMP_WRITE_J2K_H__


// OpenJPEG codec constants

#define J2K_CFMT 0
#define JP2_CFMT 1
#define JPT_CFMT 2
#define MJ2_CFMT 3
#define BMP_DFMT 12


// Save configuration parameters.
#define NUM_QUALITY_PARAMETERS 1

// Max quality = lossless.
#define QUALITY_MAX      100
#define DEFAULT_QUALITY  (QUALITY_MAX/2)
#define DEFAULT_PREVIEW  TRUE

// Save configuration version & id.
#define J2K_DEFAULTS_PARASITE "j2k-save-defaults"
#define J2K_SAVE_DEFAULTS_VERSION 2

// Save GUI configuration
#define SCALE_WIDTH           125

#define bool gboolean


typedef struct
{
   guint   width;
   guint   height;
   guint   num_components;
   guchar *data;
} Image_Info;


typedef struct
{
  bool dialog_exit_code;

  bool  quit_preview_processing;
  bool  preview_update_available;
  bool  preview_state_change;
  bool  preview_thread_done;
  guint preview_idle_fn_handle;

  opj_image_t *preview_image;

  guint preview_image_file_size;

  GtkTextBuffer *text_buffer;

  GtkWidget *slider_quality[NUM_QUALITY_PARAMETERS];  // Quality slider(s)
  GtkWidget *checkbox_preview;                        // Show preview toggle checkbox

  GThread *background_thread;  // Used for preview image generation. Done in seperate thread to keep UI responsive.

  Image_Info image_info;

} Save_State;


typedef struct
{
   gdouble quality[NUM_QUALITY_PARAMETERS];
   bool    preview_enabled;

} Save_Parameters;

extern Save_Parameters save_params;


typedef bool (*Serialize_CB)(void *buffer, int length, void *user_data);

bool serialize_file(void *buffer, int length, void *user_data);

bool serialize_prepare(Image_Info *si, gint32 image_ID, gint32 drawable_ID, gint32 orig_image_ID, bool preview);
bool serialize_image(Image_Info *image_info, bool format_codestream_only, Serialize_CB callback, void *user_data);

bool interactive_save();
void get_save_defaults();


#endif
