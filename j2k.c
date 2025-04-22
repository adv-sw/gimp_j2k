
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

#include <stdlib.h>
#include <string.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "j2k.h"
#include "j2k-load.h"
#include "j2k-export.h"

#include "libgimp/stdplugins-intl.h"


typedef struct _J2K      J2K;
typedef struct _J2KClass J2KClass;

struct _J2K
{
  GimpPlugIn      parent_instance;
};

struct _J2KClass
{
  GimpPlugInClass parent_class;
};


#define J2K_TYPE  (j2k_get_type ())
#define J2K(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), J2K_TYPE, J2K))

GType                   j2k_get_type         (void) G_GNUC_CONST;

static GList          * j2k_query_procedures (GimpPlugIn            *plug_in);
static GimpProcedure  * j2k_create_procedure (GimpPlugIn            *plug_in,
                                              const gchar           *name);

static GimpValueArray * j2k_load             (GimpProcedure         *procedure,
                                              GimpRunMode            run_mode,
                                              GFile                 *file,
                                              GimpMetadata          *metadata,
                                              GimpMetadataLoadFlags *flags,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);
static GimpValueArray * j2k_export           (GimpProcedure         *procedure,
                                              GimpRunMode            run_mode,
                                              GimpImage             *image,
                                              GFile                 *file,
                                              GimpExportOptions     *options,
                                              GimpMetadata          *metadata,
                                              GimpProcedureConfig   *config,
                                              gpointer               run_data);



G_DEFINE_TYPE (J2K, j2k, GIMP_TYPE_PLUG_IN)

GIMP_MAIN (J2K_TYPE)
DEFINE_STD_SET_I18N


static void
j2k_class_init (J2KClass *klass)
{
  GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS (klass);

  plug_in_class->query_procedures = j2k_query_procedures;
  plug_in_class->create_procedure = j2k_create_procedure;
  plug_in_class->set_i18n         = STD_SET_I18N;
}

static void
j2k_init (J2K *j2k)
{
}

static GList *
j2k_query_procedures (GimpPlugIn *plug_in)
{
  GList *list = NULL;

#if ENABLE_J2K_READ_THIS_PLUGIN
  list = g_list_append (list, g_strdup (LOAD_PROC));
#endif

  list = g_list_append (list, g_strdup (EXPORT_PROC));

  return list;
}

static GimpProcedure *
j2k_create_procedure (GimpPlugIn  *plug_in,
                      const gchar *name)
{
  GimpProcedure *procedure = NULL;

#if ENABLE_J2K_READ_THIS_PLUGIN
  if (! strcmp (name, LOAD_PROC))
    {
      procedure = gimp_load_procedure_new (plug_in, name,
                                           GIMP_PDB_PROC_TYPE_PLUGIN,
                                           j2k_load, NULL, NULL);

      gimp_procedure_set_menu_label (procedure, _("j2k image"));

      gimp_procedure_set_documentation (procedure,
                                        _("Loads files of jpeg-2000 file format"),
                                        _("Loads files of jpeg-2000 file format"),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Advance Software",
                                      "Advance Software",
                                      "2025");

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/j2k");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "j2k");
      gimp_file_procedure_set_magics (GIMP_FILE_PROCEDURE (procedure),   // TODO: Review this.
                                      "0,string,BM");
    }
  else
	  
#endif
  
  if (! strcmp (name, EXPORT_PROC))
    {
      procedure = gimp_export_procedure_new (plug_in, name,
                                             GIMP_PDB_PROC_TYPE_PLUGIN,
                                             FALSE, j2k_export, NULL, NULL);

      gimp_procedure_set_image_types (procedure, "GRAY, RGB*");

      gimp_procedure_set_menu_label (procedure, _("jpeg-2000 image"));
      gimp_file_procedure_set_format_name (GIMP_FILE_PROCEDURE (procedure),
                                           _("J2K"));

      gimp_procedure_set_documentation (procedure,
                                        _("Saves files in J2K file format"),
                                        _("Saves files in J2K file format"),
                                        name);
      gimp_procedure_set_attribution (procedure,
                                      "Advance Software",
                                      "Advance Software",
                                      "2025");

      gimp_file_procedure_set_mime_types (GIMP_FILE_PROCEDURE (procedure),
                                          "image/j2k");
      gimp_file_procedure_set_extensions (GIMP_FILE_PROCEDURE (procedure),
                                          "j2k");

      gimp_export_procedure_set_capabilities (GIMP_EXPORT_PROCEDURE (procedure),
                                              GIMP_EXPORT_CAN_HANDLE_RGB   |
                                              GIMP_EXPORT_CAN_HANDLE_GRAY  |
                                              GIMP_EXPORT_CAN_HANDLE_ALPHA |
                                              GIMP_EXPORT_CAN_HANDLE_INDEXED,
                                              NULL, NULL, NULL);

      gimp_procedure_add_double_argument (procedure, "quality",
                                          _("_Quality"),
                                          _("Quality of exported image"),
                                          0.0, 1.0, 0.9,
                                          G_PARAM_READWRITE);
   
      gimp_procedure_add_boolean_aux_argument (procedure, "show-preview",
                                               _("Sho_w preview in image window"),
                                               _("Creates a temporary layer with an export preview"),
                                               FALSE,
                                               G_PARAM_READWRITE);

    }

  return procedure;
}

static GimpValueArray *
j2k_load (GimpProcedure         *procedure,
          GimpRunMode            run_mode,
          GFile                 *file,
          GimpMetadata          *metadata,
          GimpMetadataLoadFlags *flags,
          GimpProcedureConfig   *config,
          gpointer               run_data)
{
  GimpValueArray *return_vals;
  GimpImage      *image;
  GError         *error = NULL;

  gegl_init (NULL, NULL);

  image = load_image (file, &error);

  if (! image)
    return gimp_procedure_new_return_values (procedure,
                                             GIMP_PDB_EXECUTION_ERROR,
                                             error);

  return_vals = gimp_procedure_new_return_values (procedure,
                                                  GIMP_PDB_SUCCESS,
                                                  NULL);

  GIMP_VALUES_SET_IMAGE (return_vals, 1, image);

  return return_vals;
}


static GimpValueArray *
j2k_export (GimpProcedure        *procedure,
            GimpRunMode           run_mode,
            GimpImage            *image,
            GFile                *file,
            GimpExportOptions    *options,
            GimpMetadata         *metadata,
            GimpProcedureConfig  *config,
            gpointer              run_data)
{
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GimpExportReturn   export = GIMP_EXPORT_IGNORE;
  GList             *drawables;
  GError            *error  = NULL;
  gdouble dquality;
  gint    quality;
  gboolean show_preview = FALSE;
  gboolean undo_touched = FALSE;

  gegl_init (NULL, NULL);
  
   switch (run_mode)
    {
		case GIMP_RUN_NONINTERACTIVE:
		  g_object_set (config, "show-preview", FALSE, NULL);
		  break;
		
		case GIMP_RUN_INTERACTIVE:
		case GIMP_RUN_WITH_LAST_VALS:	
			{
			  g_object_get (config, "quality", &dquality, NULL);
			  quality = (gint) (dquality * 100.0);
			}
	}
  

  export    = gimp_export_options_get_image (options, &image);
  drawables = gimp_image_list_layers (image);
  
   if (run_mode == GIMP_RUN_INTERACTIVE)
   {
      gimp_ui_init (PLUG_IN_BINARY);

      g_object_get (config, "show-preview", &show_preview, NULL);
      if (show_preview)
	  {
		  /* we freeze undo saving so that we can avoid sucking up
           * tile cache with our unneeded preview steps. */
          gimp_image_undo_freeze (image);

          undo_touched = TRUE;
	  }
   }

  status = export_image (file, image, drawables->data, run_mode,
                         procedure, G_OBJECT (config),
                         &error);

  if (export == GIMP_EXPORT_EXPORT)
    gimp_image_delete (image);

  g_list_free (drawables);
  return gimp_procedure_new_return_values (procedure, status, error);
}
