/* j2k-export.c                                                        */

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

#include "config.h"

#include <errno.h>
#include <string.h>

#include <glib/gstdio.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "j2k.h"
#include "j2k-export.h"

#include "libgimp/stdplugins-intl.h"


#include "main.h"
#include "write_j2k.h"


void Export_SetQuality(float q);
bool serialize_image(Image_Info *src_image_info, bool format_codestream_only, Serialize_CB callback, void *user_data);

GError **__error = NULL;

static  gboolean  save_dialog (GimpProcedure       *procedure,
             GimpProcedureConfig *config,
             GimpDrawable        *drawable,
             GimpImage           *image,
			 gint           channels);

static gboolean
warning_dialog (const gchar *primary,
                const gchar *secondary)
{
  GtkWidget *dialog;
  gboolean   ok;

  dialog = gtk_message_dialog_new (NULL, 0,
                                   GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
                                   "%s", primary);

  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            "%s", secondary);

  gtk_widget_show (dialog);
  gimp_window_set_transient (GTK_WINDOW (dialog));

  ok = (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return ok;
}


int serialize_save(void *buffer, int buffer_length_bytes, void *user_data)
{
	GFile *file = (GFile*) user_data;
	
	FILE *outfile = g_fopen (g_file_peek_path (file), "wb");

	if (! outfile)
    {
      g_set_error (__error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   _("Could not open '%s' for writing: %s"),
                   gimp_file_get_utf8_name (file), g_strerror (errno));
      return false;
    }
	
	fwrite(buffer, buffer_length_bytes, 1, outfile);
	 
	fclose (outfile);
}


GimpPDBStatusType
export_image (GFile         *file,
              GimpImage     *image,
              GimpDrawable  *drawable,
              GimpRunMode    run_mode,
              GimpProcedure *procedure,
              GObject       *config,
              GError       **error)
{

  gint            channels;
  gint            colors;
  guchar         *pixels = NULL;
  GeglBuffer     *buffer;
  const Babl     *format;
  GimpImageType   drawable_type;
  gint            drawable_width;
  gint            drawable_height;
  gint            i;
  double          dquality;

  buffer = gimp_drawable_get_buffer (drawable);

  drawable_type   = gimp_drawable_type   (drawable);
  drawable_width  = gimp_drawable_get_width  (drawable);
  drawable_height = gimp_drawable_get_height (drawable);
  
  Image_Info image_info;

  image_info.width = drawable_width;
  image_info.height = drawable_height;

  __error = error;

  switch (drawable_type)
    {
    case GIMP_RGBA_IMAGE:
      format       = babl_format ("R'G'B'A u8");
      channels     = 4; 
      break;

    case GIMP_RGB_IMAGE:
      format       = babl_format ("R'G'B' u8");
      channels     = 3;
      break;

    case GIMP_GRAYA_IMAGE:
	  format   = babl_format ("Y'A u8");
	  channels = 2;
      break;
		
    case GIMP_GRAY_IMAGE:
	  format   = babl_format ("Y' u8");
	  channels = 1;
      break;

    case GIMP_INDEXEDA_IMAGE:
	case GIMP_INDEXED_IMAGE:
        if (run_mode == GIMP_RUN_INTERACTIVE) 
           warning_dialog (_("Indexed image formats not yet supported & not ideal for j2k anyway."), "Suggest use of PNG instead.");
  
        return GIMP_PDB_CANCEL;

    default:
      g_assert_not_reached ();
    }

  image_info.num_components = channels;

  if (run_mode == GIMP_RUN_INTERACTIVE)
  {
	if (! save_dialog (procedure, (GimpProcedureConfig*) config, drawable, image, channels))
		return GIMP_PDB_CANCEL;
  }
  
  g_object_get (config, "quality", &dquality, NULL);

  gimp_progress_init_printf (_("Exporting '%s'"),
                             gimp_file_get_utf8_name (file));

  /* fetch the image */
  pixels = g_new (guchar, (gsize) drawable_width * drawable_height * channels);

  gegl_buffer_get (buffer,
                   GEGL_RECTANGLE (0, 0, drawable_width, drawable_height), 1.0,
                   format, pixels,
                   GEGL_AUTO_ROWSTRIDE, GEGL_ABYSS_NONE);
				   
  image_info.data = pixels;
    
  serialize_image(&image_info, true, serialize_save, file);

  /* ... and exit normally */

  g_object_unref (buffer); 
  g_free (pixels);

  return GIMP_PDB_SUCCESS;

abort:

  g_free(pixels);

  g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
               _("Error writing to file."));

  return GIMP_PDB_EXECUTION_ERROR;
}



static void
quality_changed (GimpProcedureConfig *config)
{
  double quality;
  
  g_object_get (config,
                "quality",               &quality,
                NULL);
				
  Export_SetQuality(quality);
}


// -------------------------------------------------------------------------------------------------------
//   Preview
// -------------------------------------------------------------------------------------------------------

// TODO: Implement referencing pattern in jpeg-export.c
void make_preview(GimpProcedureConfig *config) { /*TODO*/ }

void destroy_preview()
{
	// TODO
}


static GtkWidget         *preview_size  = NULL;
 
// TODO: Complete stripping of jpg-export reference code to essentials only
// TODO2: Fix up interactive preview. Reference code from 2.x version at the end of write_j2k.c

gboolean
save_dialog (GimpProcedure       *procedure,
             GimpProcedureConfig *config,
             GimpDrawable        *drawable,
             GimpImage           *image,
			 gint           channels)
{
  GtkWidget        *dialog;
  GtkWidget        *widget;
  GtkWidget        *box;
  GtkWidget        *profile_label;
  gint              restart;
  gboolean          run;

  g_object_get (config,
                "restart",          &restart,
                NULL);

  dialog = gimp_export_procedure_dialog_new (GIMP_EXPORT_PROCEDURE (procedure),
                                             GIMP_PROCEDURE_CONFIG (config),
                                             gimp_item_get_image (GIMP_ITEM (drawable)));

  gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog),
                                   "option-title", _("Options"),
                                   FALSE, FALSE);


  /* Quality as a GimpScaleEntry. */
  gimp_procedure_dialog_get_spin_scale (GIMP_PROCEDURE_DIALOG (dialog), "quality", 100.0);

  /* changing quality disables custom quantization tables, and vice-versa */
  g_signal_connect (config, "notify::quality",
                    G_CALLBACK (quality_changed),
                    NULL);

  /* File size label. */
  preview_size = gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog),
                                                  "preview-size", _("File size: unknown"),
                                                  FALSE, FALSE);
  gtk_label_set_xalign (GTK_LABEL (preview_size), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (preview_size), PANGO_ELLIPSIZE_END);
  gimp_label_set_attributes (GTK_LABEL (preview_size),
                             PANGO_ATTR_STYLE, PANGO_STYLE_ITALIC,
                             -1);
  gimp_help_set_help_data (preview_size,
                           _("Enable preview to obtain the file size."), NULL);


  /* Profile label. */
  profile_label = gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog),
                                                   "profile-label", _("No soft-proofing profile"),
                                                   FALSE, FALSE);
  gtk_label_set_xalign (GTK_LABEL (profile_label), 0.0);
  gtk_label_set_ellipsize (GTK_LABEL (profile_label), PANGO_ELLIPSIZE_END);
  gimp_label_set_attributes (GTK_LABEL (profile_label),
                             PANGO_ATTR_STYLE, PANGO_STYLE_ITALIC,
                             -1);

  /* Restart marker. */
  /* TODO: apparently when toggle is unchecked, we want to show the
   * scale as 0.
   */
  gimp_procedure_dialog_fill_frame (GIMP_PROCEDURE_DIALOG (dialog),
                                    "restart-frame", "use-restart", FALSE,
                                    "restart");
  if (restart == 0)
    g_object_set (config,
                  "use-restart", FALSE,
                  NULL);



  gimp_procedure_dialog_get_label (GIMP_PROCEDURE_DIALOG (dialog),
                                   "advanced-title", _("Advanced Options"),
                                   FALSE, FALSE);

  /* Put options in two column form so the dialog fits on
   * smaller screens. */
  gimp_procedure_dialog_fill_box (GIMP_PROCEDURE_DIALOG (dialog),
                                  "options",
                                  "quality",

                                  NULL);
  gimp_procedure_dialog_fill_frame (GIMP_PROCEDURE_DIALOG (dialog),
                                    "option-frame", "option-title", FALSE,
                                    "options");

  gimp_procedure_dialog_fill_box (GIMP_PROCEDURE_DIALOG (dialog),
                                  "advanced-options",

                                  NULL);
  gimp_procedure_dialog_fill_frame (GIMP_PROCEDURE_DIALOG (dialog),
                                    "advanced-frame", "advanced-title", FALSE,
                                    "advanced-options");

  box = gimp_procedure_dialog_fill_box (GIMP_PROCEDURE_DIALOG (dialog),
                                        "jpeg-hbox", "option-frame",
                                        "advanced-frame", NULL);
  gtk_box_set_spacing (GTK_BOX (box), 12);
  gtk_orientable_set_orientation (GTK_ORIENTABLE (box),
                                  GTK_ORIENTATION_HORIZONTAL);

  gimp_procedure_dialog_fill (GIMP_PROCEDURE_DIALOG (dialog),
                              "jpeg-hbox", NULL);

  /* Run make_preview() when various config are changed. */
  g_signal_connect (config, "notify",
                    G_CALLBACK (make_preview),
                    NULL);

  make_preview (config);

  run = gimp_procedure_dialog_run (GIMP_PROCEDURE_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  destroy_preview ();

  return run;
}