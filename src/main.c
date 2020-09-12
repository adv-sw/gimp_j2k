/* ----------------------------------------------------------------

Project : GIMP / JPEG-2000 plugin

Copyright (C) 2008-2020 Advance Software Limited.

Licensed under GNU General Public License V3.
License terms are available here : http://www.gnu.org/licenses/gpl.html

This software requires :

GIMP 2.10.20 SDK, OpenJPEG 2.3.1

Compiles on Windows (msys2) and Linux.

------------------------------------------------------------------- */



#include "config.h"

#include <string.h>
#include <malloc.h>
#include <libgimp/gimp.h>

#include <gtk/gtk.h>

#include <libgimp/gimpui.h>

#include "plugin-intl.h"

#include "main.h"
#include "write_j2k.h"

#include <gtk/gtkprivate.h>


static void query();
static void run(const gchar * name, gint nparams, const GimpParam * param, gint * nreturn_vals, GimpParam ** return_vals);
static void INIT_Il8N();


gboolean         undo_touched;
gboolean         load_interactive;
gchar           *image_comment;
gint32           display_ID;
Save_Parameters  save_params;
gint32           orig_image_ID_global;
gint32           drawable_ID_global;
gboolean         has_metadata;


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,				/* init_proc  */
  NULL,				/* quit_proc  */
  query,			/* query_proc */
  run,				/* run_proc   */
};


MAIN()


static void query()
{
  gchar *help_path;
  gchar *help_uri;

  static GimpParamDef load_args[] = {
    {GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive"},
    {GIMP_PDB_STRING, "filename", "The name of the file to load"},
    {GIMP_PDB_STRING, "raw_filename", "The name of the file to load"},
  };

  static GimpParamDef load_return_vals[] = {
    {GIMP_PDB_IMAGE, "image", "Output image"}
  };

  static GimpParamDef save_args[] = {
    {GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive"},
    {GIMP_PDB_IMAGE, "image", "Input image"},
    {GIMP_PDB_DRAWABLE, "drawable", "Drawable to save"},
    {GIMP_PDB_STRING, "filename", "The name of the file to save the image in"},
    {GIMP_PDB_STRING, "raw_filename", "The name entered"},
  };

  gimp_plugin_domain_register(PLUGIN_NAME, LOCALEDIR);

  help_path = g_build_filename(DATADIR, "help", NULL);
  help_uri = g_filename_to_uri(help_path, NULL, NULL);
  g_free(help_path);

  gimp_plugin_help_register
    ("http://developer.gimp.org/plug-in-j2k/help", help_uri);

  gimp_install_procedure (PROCEDURE_NAME,
			  "Loads files in the JPEG2000 format",
			  "Loads files in the JPEG2000 format",
			  "Advance Software Limited",
			  "Advance Software Limited",
			  "Copyright 2008-2010",
			  N_("J2K image"),
			  NULL,
			  GIMP_PLUGIN,
			  G_N_ELEMENTS (load_args),
			  G_N_ELEMENTS (load_return_vals),
			  load_args, load_return_vals);

  gimp_register_file_handler_mime (PROCEDURE_NAME, "image/j2k");
  gimp_register_magic_load_handler (PROCEDURE_NAME, "jp2,j2k", "", "");

  gimp_install_procedure (PROCEDURE_NAME_SAVE,
			  "Saves files in the JPEG-2000 format",
			  "Saves files in the JPEG-2000 format",
			  "Advance Software Limited",
			  "Advance Software Limited",
			  "Copyright 2008-2010",
			  N_("JPEG-2000 image"),
			  "GRAY, RGB",
			  GIMP_PLUGIN,
			  G_N_ELEMENTS(save_args), 0, save_args, NULL);

  gimp_register_file_handler_mime(PROCEDURE_NAME_SAVE, "image/j2k");
  gimp_register_save_handler(PROCEDURE_NAME_SAVE, "jp2,j2k", "");
}



bool serialize_file(void *buffer, int buffer_length_bytes, void *user_data);


static void run(const gchar * name, gint nparams, const GimpParam * param, gint * nreturn_vals, GimpParam ** return_vals)
{
  // Multi-threading initialize.
  // TODO: Is this required ? Sufficient ? In the right place ?!
  g_thread_init(NULL);

  static GimpParam values[2];
  GimpRunMode run_mode;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  GimpExportReturn export_rc = GIMP_EXPORT_CANCEL;
  gint32 image_ID;
  gint32 drawable_ID;
  gint32 display_ID;
  gint32 orig_image_ID;
  GimpParasite *parasite;
  GError *error  = NULL;

  run_mode = (GimpRunMode) param[0].data.d_int32;

  *nreturn_vals = 1;
  *return_vals = values;

  values[0].type = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  if (!strcmp(name, PROCEDURE_NAME))
  {
      INIT_Il8N();
      opj_image_t *opj_image = image_load(param[1].data.d_string);

      if (opj_image)
      {
         image_ID = image_to_gimp(opj_image, param[1].data.d_string, FALSE);
         opj_image_destroy(opj_image);
      }

      if (image_ID != -1)
   	{
	      *nreturn_vals = 2;
	      values[1].type = GIMP_PDB_IMAGE;
	      values[1].data.d_image = image_ID;
	   }
      else
   	{
	      printf ("Could not open image.\n");
	      status = GIMP_PDB_EXECUTION_ERROR;
	   }
  }
  else if (strcmp (name, PROCEDURE_NAME_SAVE) == 0)
  {
      image_ID = orig_image_ID = param[1].data.d_int32;
      drawable_ID = param[2].data.d_int32;

      // Export image with optional preview ...

      switch (run_mode)
      {
        case GIMP_RUN_INTERACTIVE:
        case GIMP_RUN_WITH_LAST_VALS:
          gimp_ui_init(PLUG_IN_BINARY, FALSE);

          export_rc = gimp_export_image(&image_ID, &drawable_ID, "J2K",
                                      (GimpExportCapabilities) (GIMP_EXPORT_CAN_HANDLE_RGB|GIMP_EXPORT_CAN_HANDLE_GRAY|GIMP_EXPORT_CAN_HANDLE_ALPHA));
          switch (export_rc)
          {
              case GIMP_EXPORT_EXPORT:
              {
                gchar *tmp = g_filename_from_utf8(_("Export Preview"), -1,  NULL, NULL, NULL);

                if (tmp)
                {
                    gimp_image_set_filename(image_ID, tmp);
                    g_free (tmp);
                }

                display_ID = -1;
              }
              break;

            case GIMP_EXPORT_IGNORE:
              break;

            case GIMP_EXPORT_CANCEL:
              values[0].data.d_status = GIMP_PDB_CANCEL;
              return;
            }
          break;

        default:
          break;
      }

      get_save_defaults();

      switch (run_mode)
      {
        case GIMP_RUN_NONINTERACTIVE:

          if (nparams != 14)
          {
              status = GIMP_PDB_CALLING_ERROR;
          }
          else
          {
              int i;

              save_params.preview_enabled = FALSE;

              for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
              {
                  if (save_params.quality[i] < 0.0)
                     status = GIMP_PDB_CALLING_ERROR;
              }
          }
          break;

        case GIMP_RUN_INTERACTIVE:
        case GIMP_RUN_WITH_LAST_VALS:

          // Restore the values found when loading the file (if available)
          //j2k_restore_original_settings (orig_image_ID, &orig_quality);

          // Load up the previous configuration (stored if file has been saved previously)
          parasite = gimp_image_parasite_find (orig_image_ID,"j2k-save-options");

          if (parasite)
          {
              int i;
              const Save_Parameters *config = (const Save_Parameters *) gimp_parasite_data(parasite);

              save_params.preview_enabled = config->preview_enabled;

              // TODO: This is insufficient - should serialize # params in Save_Parameters for when # changes.
              for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
              {
                  save_params.quality[i] = config->quality[i];
              }

              gimp_parasite_free (parasite);
          }
          else
          {
              /* We are called with GIMP_RUN_WITH_LAST_VALS but this image
               * doesn't have a "j2k-save-options" parasite so prompt the user with a dialog.
               */
              run_mode = GIMP_RUN_INTERACTIVE;

              /* If this image was loaded from a J2K file and has not been
               * saved yet, try to use some of the settings from the
               * original file if they are better than the default values.
               */
              /*if (orig_quality > save_params.quality)
              {
                  save_params.quality = orig_quality;
                  save_params.use_orig_quality = TRUE;
              }*/
            }

          break;

        }

      if (run_mode == GIMP_RUN_INTERACTIVE)
      {
          if (save_params.preview_enabled)
          {
              // Freeze undo saving to avoid sucking up tile cache with unnecessary preview steps.
              gimp_image_undo_freeze(image_ID);
              undo_touched = TRUE;
          }

          // Prepare for the preview
          preview_image_ID = image_ID;
          orig_image_ID_global = orig_image_ID;
          drawable_ID_global = drawable_ID;

          //  First aquire information with a dialog.
          status = interactive_save() ? GIMP_PDB_SUCCESS : GIMP_PDB_CANCEL;

          if (undo_touched)
          {
              // Thaw undo saving and flush the displays to have them reflect the current shortcuts.
              gimp_image_undo_thaw(image_ID);
              gimp_displays_flush();
          }
        }

      if (status == GIMP_PDB_SUCCESS)
      {
          Image_Info image_info;

          if (!serialize_prepare(&image_info, image_ID, drawable_ID, orig_image_ID, FALSE))
          {
              status = GIMP_PDB_EXECUTION_ERROR;
          }
          else
          {
             if (image_info.data)
             {
                // Determine format to save to from extension (jp2 wrapped or j2k codestream only).
                const char *filename = param[3].data.d_string;
                const char *ext = strrchr(filename, '.');
                bool format_codestream_only = ext && !stricmp(ext, ".j2k");

                serialize_image(&image_info, format_codestream_only, serialize_file, filename);

                g_free(image_info.data);
             }
          }
      }

      if (export_rc == GIMP_EXPORT_EXPORT)
      {
          /* If the image was exported, delete the new display. */
          /* This also deletes the image.                       */

          if (display_ID != -1)
            gimp_display_delete (display_ID);
          else
            gimp_image_delete (image_ID);
      }

      if (status == GIMP_PDB_SUCCESS)
      {
          // Change the defaults to be whatever was used to save this image.
          // Dump the old parasites and add new ones.

          gimp_image_parasite_detach (orig_image_ID, "gimp-comment");

          if (image_comment && strlen(image_comment))
          {
              parasite = gimp_parasite_new ("gimp-comment", GIMP_PARASITE_PERSISTENT,
                                            strlen (image_comment) + 1, image_comment);

				  gimp_image_parasite_attach (orig_image_ID, parasite);
              gimp_parasite_free (parasite);
          }

          gimp_image_parasite_detach (orig_image_ID, "j2k-save-options");

          parasite = gimp_parasite_new ("j2k-save-options",  0, sizeof (save_params), &save_params);
          gimp_image_parasite_attach (orig_image_ID, parasite);
          gimp_parasite_free (parasite);
      }
    }
    else
    {
       status = GIMP_PDB_CALLING_ERROR;
    }

    if (status != GIMP_PDB_SUCCESS && error)
    {
       *nreturn_vals = 2;
       values[1].type          = GIMP_PDB_STRING;
       values[1].data.d_string = error->message;
    }

    values[0].data.d_status = status;
}


static void INIT_Il8N()
{
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);

#ifdef HAVE_BIND_TEXTDOMAIN_CODESET
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  textdomain (GETTEXT_PACKAGE);
}
