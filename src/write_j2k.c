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

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

// For usleep.
#include <unistd.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "plugin-intl.h"

#include "main.h"
#include "write_j2k.h"



extern gint32 orig_image_ID_global;
extern gint32 drawable_ID_global;

static GtkWidget *preview_file_size = nullptr;

static bool __preview_update_required = true;


// TODO[OPT]: This is a brute force max expected compressed file size implementation to deliver a working version asap.
// The chunk size could be reduced & the custom read & write functions enhanced to support reading & writing chunks from a memory stream, to replace the single block only current implementation. 
const size_t write_chunk_size = 0x08000000; // 128 MB


typedef struct
{
   uint8 *data;
   size_t len;
} Buffer;


#if ENABLE_OPENJPEG_DIAGNOSTIC

void error_callback (const char *msg, void *client_data)
{
  FILE *stream = (FILE *) client_data;
  fprintf (stream, "[J2K: ERROR] %s", msg);
}


void warning_callback (const char *msg, void *client_data)
{
  FILE *stream = (FILE *) client_data;
  fprintf (stream, "[J2K: WARNING] %s", msg);
}


void info_callback (const char *msg, void *client_data)
{
  FILE *stream = (FILE *) client_data;
  fprintf (stream, "[J2K: INFO] %s", msg);
}

#endif // ENABLE_OPENJPEG_DIAGNOSTIC


static opj_image_t *ToCodestream(const opj_cparameters_t *parameters, uint32 w, uint32 h, uint32 src_bytes_per_pixel, bool mono, bool save_alpha,
                                 const unsigned char *src_line, uint32 src_pitch, bool colour_order_rgb, bool flip_image_vertically)
{
   const uint8 *src_ptr;
   uint32 alpha_channel;
   uint32 x,y;
   int i, numcomps;
   OPJ_COLOR_SPACE color_space;
   int subsampling_dx, subsampling_dy;
   opj_image_t *image;
   uint32 red_channel, blue_channel;

   /* Initialize image components */
   opj_image_cmptparm_t cmptparm[4];	/* Maximum of 4 components */
   memset(&cmptparm[0], 0, 4 * sizeof(opj_image_cmptparm_t));

   if (mono)
   {
	  color_space = OPJ_CLRSPC_GRAY;
      numcomps = save_alpha ? 2 : 1;
   }
   else
   {
      numcomps = save_alpha ? 4 : 3;
	  color_space = OPJ_CLRSPC_SRGB;
   }

   subsampling_dx = parameters->subsampling_dx;
   subsampling_dy = parameters->subsampling_dy;

   for (i = 0; i < numcomps; i++)
   {
		cmptparm[i].prec = 8;
		cmptparm[i].bpp = 8;
		cmptparm[i].sgnd = 0;
		cmptparm[i].dx = subsampling_dx;
		cmptparm[i].dy = subsampling_dy;
		cmptparm[i].w = w;
		cmptparm[i].h = h;
   }

   /* Create the image */
   image = opj_image_create(numcomps, &cmptparm[0], color_space);

   if (!image)
		return nullptr;

	/* Set image offset and reference grid */
	image->x0 = parameters->image_offset_x0;
	image->y0 = parameters->image_offset_y0;
	image->x1 =	!image->x0 ? (w - 1) * subsampling_dx + 1 : image->x0 + (w - 1) * subsampling_dx + 1;
	image->y1 =	!image->y0 ? (h - 1) * subsampling_dy + 1 : image->y0 + (h - 1) * subsampling_dy + 1;

   /* Set image data */

   // Optionally flip image vertically to ensure the texture saves the right way up.
   if (flip_image_vertically)
      src_line += src_pitch * (h-1);

   alpha_channel = mono ? 1 : 3;

   // Select appropriate channels - input could be rgb or bgr
   red_channel  = colour_order_rgb ? 2 : 0;
   blue_channel = colour_order_rgb ? 0 : 2;

   for (y=0;y<h;y++)
   {
		src_ptr = src_line;

		for (x=0;x<w;x++)
		{
			uint32 index = y*w + x;

		 if (mono)
         {
            src_ptr += 2;
            image->comps[0].data[index] = *(src_ptr++);
         }
         else
         {
            image->comps[red_channel].data[index] = *(src_ptr++);
			   image->comps[1].data[index] = *(src_ptr++);
            image->comps[blue_channel].data[index] = *(src_ptr++);
         }

         if (save_alpha)
            image->comps[alpha_channel].data[index] = *(src_ptr++);
         else if (src_bytes_per_pixel==4)
            src_ptr++;
		}

      if (flip_image_vertically)
         src_line -= src_pitch;
      else
         src_line += src_pitch;
	}

   return image;
}


bool serialize_file(void *buffer, int buffer_length_bytes, void *user_data)
{
   const char *filename = (const char *) user_data;

   FILE *f = fopen(filename, "wb");

   if (!f)
   {
      fprintf(stderr, "Could not open file for writing : %s", filename);
      return false;
   }
   else
   {
      fwrite(buffer, 1, buffer_length_bytes, f);
      fclose(f);
   }

   return true;
}



// Analyse image to enable us to perform compression optimizations - currently limited to component reduction (rgb->grey).

bool Scan_IsMono(const uint8 *src_line, uint32 src_pitch, uint32 src_bytes_per_pixel, uint32 width, uint32 height)
{
   uint32 x,y;
   const uint8 *src_ptr;
   int r,g,b;

   const uint32 colour_threshold = 3;

	for (y=0;y<height;y++)
	{
      src_ptr = src_line;

		for (x=0;x<width;x++)
		{
			r = *(src_ptr++);
			g = *(src_ptr++);
			b = *(src_ptr++);

         // Step past alpha channel data, if present.
         src_ptr += (src_bytes_per_pixel==4);

         if ( (abs(r-g)>colour_threshold) || (abs(r-b)>colour_threshold) || (abs(g-b)>colour_threshold) )
         {
            return false;
         }
		}

		src_line += src_pitch;
	}

   return true;
}


bool IsChannelRedundant(uint32 channel_offset, const uint8 *src_line, uint32 src_pitch, uint32 src_bytes_per_pixel, uint32 width, uint32 height)
{
   uint32 x,y, next_pixel_offset;
   bool need_channel = false;
   bool done = false;
   uint8 first_value;
	const uint8 *src_ptr;

   // Currently only supports formats which pack one byte per channel.

   if (channel_offset >= src_bytes_per_pixel)
      return true;

   next_pixel_offset = src_bytes_per_pixel-channel_offset;

   // Grab channel value from the first pixel ...
   first_value = *(src_line+channel_offset);

   for (y=0;y<height;y++)
   {
	     if (done)
          break;

        src_ptr = src_line;

		  for (x=0;x<width;x++)
		  {
				src_ptr += channel_offset;

				if (*src_ptr != first_value)
				{
					need_channel = true;
					done = true;
					break;
				}

				src_ptr += next_pixel_offset;
			}

		src_line += src_pitch;
	}

   return !need_channel;
}



OPJ_SIZE_T memory_stream_write(void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data)
{
   Buffer *b = (Buffer*) p_user_data;
 
   assert(b->len == 0);
   
	if (b->len != 0)
	{
		fprintf(stderr, "Output file too big. Current implementation only supports single chunk write, max output file size : %d bytes", write_chunk_size);
		return 0;
	}
  
   b->data = malloc(p_nb_bytes);
   b->len = p_nb_bytes;
   memcpy(b->data, p_buffer, b->len);

   return p_nb_bytes;
}


bool serialize_image(Image_Info *src_image_info, bool format_codestream_only, Serialize_CB callback, void *user_data)
{
   int i;
   uint32 src_bytes_per_pixel = src_image_info->num_components;
   uint32 src_pitch = src_bytes_per_pixel * src_image_info->width;
   const bool colour_order_rgb = false;
   const bool flip_image_vertically = false;

   // Check for redundant colour channels ...
   bool mono = Scan_IsMono(src_image_info->data, src_pitch, src_bytes_per_pixel, src_image_info->width, src_image_info->height);

   bool save_alpha = (src_bytes_per_pixel == 2) || (src_bytes_per_pixel == 4);


#if 0

   // TODO: Turn this back on - its useful in ensuring optimal file size - our objective.

   if (save_alpha)
   {
      // Check for redundant alpha channels ...
      if (IsChannelRedundant(3, src_image_info->data, src_pitch, src_bytes_per_pixel, src_image_info->width, src_image_info->height))
      {
          //String s("Warning - '" + filename + "' contains a uniform alpha channel (discarded). Please use layer transparency instead.");
          //fprintf(stderr, s);
         save_alpha = false;
      }
   }
#endif


   // PART 2 : Initialize OpenJPEG.


	/* Set encoding parameters  */
    opj_cparameters_t parameters;
    opj_set_default_encoder_parameters(&parameters);
	parameters.cod_format = format_codestream_only ? J2K_CFMT : JP2_CFMT;

	/* Get a J2K compressor handle */
	opj_codec_t *codec = opj_create_compress(OPJ_CODEC_J2K);
	

#if ENABLE_OPENJPEG_DIAGNOSTIC
	opj_set_info_handler(codec, info_callback, stderr);
	opj_set_warning_handler(codec, warning_callback, stderr);
	opj_set_error_handler(codec, error_callback, stderr);
#endif


   opj_image_t *image = ToCodestream(&parameters, src_image_info->width, src_image_info->height, src_bytes_per_pixel, mono, save_alpha, src_image_info->data, src_pitch,
                                     colour_order_rgb, flip_image_vertically);

   // PART 3 : Encode OpenJPEG raw data into a j2k codestream.

   // Please see image_to_j2k sample code in openjpeg.org j2k for an example of how to use other encoding parameters.

   // Decide if MCT should be used.
   parameters.tcp_mct = image->numcomps >= 3 ? 1 : 0;

   // OpenJPEG quality: 10 = low, 20 = higher, etc. 0 = lossless.
   // Remapped so QUALITY_MAX = lossless.

   for (i=0;i< NUM_QUALITY_PARAMETERS; i++)
   {
      float q = save_params.quality[i];
      parameters.tcp_distoratio[i] = q == QUALITY_MAX ? 0: q;
      parameters.tcp_numlayers++;
   }

   parameters.cp_fixed_quality = 1;

	/* setup the encoder parameters using the current image and user parameters */
	opj_setup_encoder(codec, &parameters, image);

	/* Open a byte stream for writing */
   opj_stream_t *s = opj_stream_create(write_chunk_size, false);
    
   opj_stream_set_write_function(s, memory_stream_write);


   Buffer b;
   b.data = NULL;
   b.len = 0;
   opj_stream_set_user_data(s, (void*) &b, NULL); 


	bool ok = opj_start_compress(codec, image, s);

   if (!ok)
      fprintf(stderr, "Failed: opj_start_compress.\n");
   else
   { 
      ok = opj_encode(codec, s);

	   if (!ok)
		  fprintf(stderr, "Failed : opj_encode.\n");
	    else
		{
			ok = opj_end_compress(codec, s);
  
		    if (!ok)
		       fprintf(stderr, "Failed : opj_end_compress.\n");
		}
   }
  
   // PART 4 : Destroy compressor & custom stream.
   opj_stream_destroy(s);
   opj_destroy_codec(codec);
	
   // PART 5 : Process compressed stream.

   if (ok && callback)
      ok = callback(b.data, b.len, user_data);


	if (b.data)
      free(b.data);

   return ok;
}


bool serialize_prepare(Image_Info *image, gint32 image_ID, gint32 drawable_ID, gint32 orig_image_ID, bool preview)
{
  GimpPixelRgn pixel_rgn;
  GimpDrawable *drawable;
  GimpImageType drawable_type;
  gint x1, y1, x2, y2;

  // Setup GIMP drawables
  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  gimp_drawable_mask_bounds (drawable->drawable_id, &x1, &y1, &x2, &y2);

  image->width = x2 - x1;
  image->height = y2 - y1;

  gimp_pixel_rgn_init(&pixel_rgn, drawable, x1, y1, image->width, image->height, FALSE, FALSE);

  /* Convert drawable image into opj_image_t image object
   * Use pixel_rgn for that
   * First initialize image_info with necessary parameters
   * then call opj_image_create
   */

  switch (drawable_type)
  {
    case GIMP_GRAY_IMAGE:
    {
	    image->num_components = 1;
	    break;
    }

    case GIMP_GRAYA_IMAGE:
    {
    	 image->num_components = 2;
	    break;
    }

    case GIMP_RGB_IMAGE:
    {
	    image->num_components = 3;
	    break;
    }

    case GIMP_RGBA_IMAGE:
    {
   	 image->num_components = 4;
	    break;
    }

    case GIMP_INDEXED_IMAGE:
    {
     	 g_message (_("Cannot currently save indexed images."));
	    return GIMP_PDB_EXECUTION_ERROR;
    }

    case GIMP_INDEXEDA_IMAGE:
    {
	    g_message (_("Cannot currently save indexed images."));
	    return GIMP_PDB_EXECUTION_ERROR;
    }
  }

  image->data = g_new (guchar, image->num_components * image->width * image->height);
  gimp_pixel_rgn_get_rect(&pixel_rgn, image->data, 0, 0, image->width, image->height);

  if (preview && drawable)
     gimp_drawable_detach(drawable);

  return GIMP_PDB_SUCCESS;
}


// -------------------------------------------------------------------------------------------------------
//   Preview
// -------------------------------------------------------------------------------------------------------


void destroy_preview()
{
  if (drawable_global)
  {
      gimp_drawable_detach (drawable_global);
      drawable_global = nullptr;
  }

  if (gimp_image_is_valid(preview_image_ID) && gimp_drawable_is_valid(preview_layer_ID))
  {
      /*  assuming that reference counting is working correctly,
          we do not need to delete the layer, removing it from
          the image should be sufficient  */
      gimp_image_remove_layer(preview_image_ID, preview_layer_ID);

      preview_layer_ID = -1;
  }
}


// -------------------------------------------------------------------------------------------------------
//   Save dialog GUI configuration & functionality.
// -------------------------------------------------------------------------------------------------------


static void register_preview_parameter_changed()
{
   __preview_update_required = true;
}


static void save_dialog_response(GtkWidget *widget, gint response_id, gpointer data)
{
  Save_State *state = (Save_State *) data;
  GtkTextIter  start_iter;
  GtkTextIter  end_iter;

  switch (response_id)
  {
    case GTK_RESPONSE_OK:
      gtk_text_buffer_get_bounds (state->text_buffer, &start_iter, &end_iter);
      image_comment = gtk_text_buffer_get_text (state->text_buffer, &start_iter, &end_iter, FALSE);
      state->dialog_exit_code = TRUE;

      /* fall through */

    default:
      gtk_widget_destroy (widget);
      break;
  }
}


void get_save_defaults()
{
  GimpParasite *parasite;
  gchar        *def_str, *param;
  Save_Parameters tmp;
  int i, version, num_quality_params;

  for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
  {
     save_params.quality[i] = DEFAULT_QUALITY;
     tmp.quality[i] = DEFAULT_QUALITY;
  }

  save_params.preview_enabled = DEFAULT_PREVIEW;

  parasite = gimp_get_parasite(J2K_DEFAULTS_PARASITE);

  if (!parasite)
    return;

  def_str = g_strndup((const gchar *) gimp_parasite_data (parasite), gimp_parasite_data_size(parasite));

  gimp_parasite_free(parasite);

  param = strtok(def_str, " ");

  version = param ? atoi(param) : 0;

  if (version == J2K_SAVE_DEFAULTS_VERSION)
  {
      param = strtok(nullptr, " ");
      tmp.preview_enabled = param ? atoi(param) : 0;

      param = strtok(nullptr, " ");
      num_quality_params = param ? atoi(param) : 0;

      if (num_quality_params > NUM_QUALITY_PARAMETERS)
         num_quality_params = NUM_QUALITY_PARAMETERS;

      for (i=0;i<num_quality_params;i++)
      {
         param = strtok(nullptr, " ");
         tmp.quality[i] = param ? atof(param) : DEFAULT_QUALITY;
      }

      memcpy(&save_params, &tmp, sizeof(tmp));
   }
}


static void save_defaults()
{
  GimpParasite *parasite;
  gchar *prev;
  int i;

  gchar *def_str = g_strdup_printf("%d %d %d", J2K_SAVE_DEFAULTS_VERSION, save_params.preview_enabled, NUM_QUALITY_PARAMETERS);

  for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
  {
     prev = def_str;
     def_str = g_strdup_printf ("%s %lf ", prev, save_params.quality[i]);
     g_free(prev);
  }

  parasite = gimp_parasite_new(J2K_DEFAULTS_PARASITE, GIMP_PARASITE_PERSISTENT, strlen(def_str), def_str);

  gimp_attach_parasite(parasite);
  gimp_parasite_free(parasite);
  g_free(def_str);
}


static void save_dialog_set_defaults(Save_State *state)
{
  int i;

  get_save_defaults();

#define SET_ACTIVE_BTTN(x,y) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (state->x), save_params.y)

  SET_ACTIVE_BTTN(checkbox_preview, preview_enabled);

#undef SET_ACTIVE_BTTN

  for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
     gtk_adjustment_set_value(GTK_ADJUSTMENT(state->slider_quality[i]), save_params.quality[i]);
}


static void save_dialog_initialize(Save_State *state)
{
  GtkWidget     *dialog;
  GtkWidget     *vbox;
  GtkObject     *entry;
  GtkWidget     *table;
  GtkWidget     *table2;
  GtkWidget     *tabledefaults;
  GtkWidget     *expander;
  GtkWidget     *frame;
  GtkWidget     *toggle;
  GtkWidget     *text_view;
  GtkTextBuffer *text_buffer;
  GtkWidget     *scrolled_window;
  GtkWidget     *button;
  gchar         *text;
  int i;

  dialog = gimp_dialog_new (_("Save as JPEG-2000"), PLUG_IN_BINARY,
                            NULL, (GtkDialogFlags) 0,
                            gimp_standard_help_func, SAVE_PROC,

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_SAVE,   GTK_RESPONSE_OK,

                            NULL);

  gtk_dialog_set_alternative_button_order(GTK_DIALOG (dialog), GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL, -1);

  g_signal_connect(dialog, "response", G_CALLBACK (save_dialog_response), state);

  g_signal_connect(dialog, "destroy", G_CALLBACK (gtk_main_quit), NULL);

  gtk_window_set_resizable(GTK_WINDOW (dialog), FALSE);
  gimp_window_set_transient(GTK_WINDOW (dialog));


  for (i=0;i<NUM_QUALITY_PARAMETERS;i++)
  {
     vbox = gtk_vbox_new(FALSE, 12);
     gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
     gtk_box_pack_start(GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);
     gtk_widget_show(vbox);

     table = gtk_table_new (1, 3, FALSE);
     gtk_table_set_col_spacings (GTK_TABLE (table), 6);
     gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
     gtk_widget_show (table);

     state->slider_quality[i] = entry = gimp_scale_entry_new(GTK_TABLE(table), 0, 0,
                                                _("_Quality:"), SCALE_WIDTH, 0, save_params.quality[i],
                                                1.0, 100.0, 1.0, 10.0, 0, TRUE, 0.0, 0.0,
                                                _("JPEG-2000 image quality"), "j2k-save-quality");

     g_signal_connect(entry, "value-changed", G_CALLBACK(gimp_double_adjustment_update), &(save_params.quality[i]));
     g_signal_connect(entry, "value-changed", G_CALLBACK(register_preview_parameter_changed), nullptr);
  }


  preview_file_size = gtk_label_new (_("File size: unknown"));
  gtk_misc_set_alignment (GTK_MISC (preview_file_size), 0.0, 0.5);
  gimp_label_set_attributes (GTK_LABEL (preview_file_size), PANGO_ATTR_STYLE, PANGO_STYLE_ITALIC,-1);
  gtk_box_pack_start (GTK_BOX (vbox), preview_file_size, FALSE, FALSE, 0);
  gtk_widget_show (preview_file_size);
  gimp_help_set_help_data (preview_file_size, _("Enable preview to obtain the file size."), nullptr);

  state->checkbox_preview = toggle = gtk_check_button_new_with_mnemonic (_("Sho_w preview in image window"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), save_params.preview_enabled);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 0);
  gtk_widget_show(toggle);

  g_signal_connect(toggle, "toggled", G_CALLBACK (gimp_toggle_button_update), &save_params.preview_enabled);
  g_signal_connect(toggle, "toggled", G_CALLBACK(register_preview_parameter_changed), nullptr);

  // Advanced settings :

  // TODO: Add some !  See openjpeg image_to_j2k.c sample code for available parameters.

  text = g_strdup_printf ("<b>%s</b>", _("_Advanced Options"));
  expander = gtk_expander_new_with_mnemonic (text);
  gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);
  g_free(text);

  gtk_box_pack_start (GTK_BOX (vbox), expander, TRUE, TRUE, 0);
  gtk_widget_show (expander);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_add (GTK_CONTAINER (expander), vbox);
  gtk_widget_show (vbox);

  frame = gimp_frame_new ("<expander>");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  table = gtk_table_new (4, 7, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacing (GTK_TABLE (table), 1, 12);
  gtk_container_add (GTK_CONTAINER (frame), table);

  table2 = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table2), 6);
  gtk_table_attach (GTK_TABLE (table), table2,
                    2, 6, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (table2);



  // Comment

  frame = gimp_frame_new (_("Comment"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                       GTK_SHADOW_IN);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);

  gtk_widget_set_size_request (scrolled_window, 250, 50);
  gtk_container_add (GTK_CONTAINER (frame), scrolled_window);
  gtk_widget_show (scrolled_window);

  state->text_buffer = text_buffer = gtk_text_buffer_new (NULL);

  if (image_comment)
    gtk_text_buffer_set_text (text_buffer, image_comment, -1);

  text_view = gtk_text_view_new_with_buffer (text_buffer);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);

  gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);
  gtk_widget_show (text_view);

  g_object_unref (text_buffer);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  tabledefaults = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (tabledefaults), 6);
  gtk_box_pack_start (GTK_BOX (vbox), tabledefaults, FALSE, FALSE, 0);
  gtk_widget_show (tabledefaults);

  button = gtk_button_new_with_mnemonic (_("Load Defaults"));
  gtk_table_attach (GTK_TABLE (tabledefaults), button, 0, 1, 1, 2, GTK_FILL, (GtkAttachOptions) 0, 0, 0);
  gtk_widget_show (button);

  g_signal_connect_swapped (button, "clicked", G_CALLBACK (save_dialog_set_defaults), state);

  button = gtk_button_new_with_mnemonic (_("Sa_ve Defaults"));
  gtk_table_attach (GTK_TABLE (tabledefaults), button, 1, 2, 1, 2, GTK_FILL, (GtkAttachOptions) 0, 0, 0);
  gtk_widget_show(button);

  g_signal_connect_swapped (button, "clicked", G_CALLBACK (save_defaults), state);
  gtk_widget_show(frame);

  gtk_widget_show(table);
  gtk_widget_show(dialog);
}


bool serialize_decompress(void *buffer, int buffer_length_bytes, void *user_data)
{
   Save_State *state = (Save_State *) user_data;

   state->preview_image_file_size = buffer_length_bytes;

   // Decompress for use by preview display (actual update occurs in idle processing loop) unless another update is immediately required.
   if (!__preview_update_required)
	{
		state->preview_image = decode_image(buffer, buffer_length_bytes, true);
		return !!state->preview_image; // All ok if we get a decoded image.
	}

   return true;
}


// Keep GIMP SDK access to minimum from background thread as the API is not guarenteed thread safe.
static void background_thread(void *user_data)
{
   Save_State *state = (Save_State *) user_data;

   while (!state->quit_preview_processing)
   {
      if (__preview_update_required && !state->preview_update_available)  // Don't create a new preview whilst previous one is still being processed.
      {
         __preview_update_required = FALSE;

         if (save_params.preview_enabled)
         {
            // Serialize with current settings through decompressor to generate new preview image.
            serialize_image(&(state->image_info), true, serialize_decompress, (void*) state);

            state->preview_update_available = TRUE;
         }

         state->preview_state_change = TRUE;
      }

		if (!__preview_update_required)
			usleep(1000);  // TODO: Determine optimal value.
   }

   state->preview_thread_done = true;

   // Terminate thread.
   g_thread_exit(nullptr);
}


static void preview_prepare()
{
  destroy_preview();

  if (save_params.preview_enabled)
  {
      if (!undo_touched)
      {
          // Freeze undo saving to avoid sucking up tile cache with unnecessary preview steps.
          gimp_image_undo_freeze(preview_image_ID);
          undo_touched = TRUE;
      }

      if (display_ID == -1)
        display_ID = gimp_display_new(preview_image_ID);
  }
  else
  {
      gtk_label_set_text (GTK_LABEL (preview_file_size), _("File size: unknown"));
      gimp_displays_flush();
   }
}


// Idle processing occurs in main thread so we can safely use all GIMP SDK functionality here.
static bool idle_save_preview_processing(void *user_data)
{
   Save_State *state = (Save_State *) user_data;

   // Some item of state has changed so react accordingly.
   if (state->preview_state_change)
   {
      state->preview_state_change = FALSE;
      
		// Prepare for preview window update unless another update is coming though ...
		if (!__preview_update_required)
			preview_prepare();
   }

   if (state->preview_update_available)
   {
        // Update status.
        gchar temp[128];
		
	    if (!__preview_update_required)
			g_snprintf(temp, sizeof(temp), _("File size: %02.01f kB"), (gdouble) (state->preview_image_file_size) / 1024.0);
		else
			g_snprintf(temp, sizeof(temp), _("File size:")); // If another update is coming through, wait for its size to avoid glitches (changes to discarded intermediate update values).
		
		gtk_label_set_text(GTK_LABEL(preview_file_size), temp);

		// Copy new image into preview window ...
		if (state->preview_image)
		{
			// ... unless a new preview has been requested.
			if (!__preview_update_required)
			{
				if (save_params.preview_enabled)
					image_to_gimp(state->preview_image, nullptr, TRUE);
			}

			opj_image_destroy(state->preview_image);
			state->preview_image = nullptr;
		}

		// Skip flush if another update is being prepared.
		if (!__preview_update_required)
        {
           gimp_displays_flush();
           gdk_flush();
		}

      // Mark current preview update as consumed - background thread can now generate new preview if required.
      state->preview_update_available = FALSE;
   }

   return TRUE;
}


bool interactive_save()
{
   Save_State state;
   memset(&state, 0, sizeof(Save_State));

   save_dialog_initialize(&state);

   // TODO: We need to prepare the image data for access by preview code once if preview is enabled.
   // OPT: Do this in the idle loop - create on demand. Configure preview thread to process only when src data available.
   serialize_prepare(&(state.image_info), preview_image_ID, drawable_ID_global, orig_image_ID_global, TRUE);

   state.background_thread      = g_thread_create( (GThreadFunc) background_thread, (gpointer) &state , FALSE , nullptr);
   state.preview_idle_fn_handle = g_idle_add( (GSourceFunc) idle_save_preview_processing, &state);

   state.dialog_exit_code = FALSE;

   // Main processing loop executed continuously whilst dialog active.
   gtk_main();

   state.quit_preview_processing = TRUE;   // signal the background preview generation to stop.

   int counter = 0;

   // Wait for thread exit.
   while (!state.preview_thread_done)
   {
      usleep(1000); // TODO: Figure out optimal setting. This is a wild guess.

      // Diagnostics to console if thread exit takes longer than expected.

      if (++counter > 1000)  // Wild guess - determine optimal setting.
      {
          printf("%s plugin : Waiting for background thread to terminate.\n", PLUG_IN_BINARY);
          counter = 0;

#if __ENABLE_PREVIEW_BACKGROUND_THREAD_EXIT_TIMEOUT
          break; // Timeout & kill thread as it refuses to exit gracefully.
#endif
      }

   }


   // TODO: Optionally kill thread if it refuses to exit gracefully after some timeout.
   // Docs state that glib currently has no thread kill wrapper so use platform specific interfaces to implement.


   // Background thread terminated, so we're idle processor can be terminated.
   g_source_remove(state.preview_idle_fn_handle);


   // Preview window processing complete so remove it.
   destroy_preview();

   // Done with source image data copy.
   if (state.image_info.data)
      g_free(state.image_info.data);

   return state.dialog_exit_code;
}
