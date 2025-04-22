/* ----------------------------------------------------------------

Project : GIMP / JPEG-2000 plugin

Copyright (C) 2002-2025 Advance Software Limited.

Licensed under GNU General Public License V3.
License terms are available here : http://www.gnu.org/licenses/gpl.html

This software requires :

GIMP 3, OpenJPEG 2.3.1

Compiles on Windows (mingw64) and Linux.

------------------------------------------------------------------- */


#if ENABLE_J2K_READ_THIS_PLUGIN

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "main.h"


gint32 volatile  preview_image_ID;
gint32           preview_layer_ID;
GimpDrawable    *drawable_global;


#if ENABLE_OPENJPEG_DIAGNOSTIC
void error_callback (const char *msg, void *client_data);
void warning_callback (const char *msg, void *client_data);
void info_callback (const char *msg, void *client_data);
#endif


#ifndef WIN32
#define stricmp strcasecmp
#endif


static gboolean __SupportedFormat(opj_image_t *img)
{
	gint32 i;

   for (i = 0; i < img->numcomps-1; i++)
	{
		if (img->comps[0].dx != img->comps[i+1].dx)
			return 0;

		if (img->comps[0].dy != img->comps[i+1].dy)
			return 0;

		if (img->comps[0].prec != img->comps[i+1].prec)
			return 0;
	}

	return 1;
}


/*
 * Divide an integer by a power of 2 and round upwards.
 *
 * a divided by 2^b
 */


static int ceildiv(int a, int b)
{
    return (a+b-1)/b;
}


static gboolean __Query(opj_image_t *img, uint32 *width, uint32 *height, gboolean *alpha)
{
	if (!__SupportedFormat(img))
		return 0;

   *width  = ceildiv(img->x1-img->x0, img->comps[0].dx);
	*height = ceildiv(img->y1-img->y0, img->comps[0].dy);

	// Mono with alpha, or RGB with alpha.
   *alpha = (img->numcomps==2) || (img->numcomps==4);

   return 1;
}


static void opj_pixel_data_to_gimp(opj_image_t * image, GeglBuffer *buffer)
{
   int i, j, src_comp;
   gint height, width, src_num_components, dest_num_components, offset;
   guchar *buf;

   width = (gint) (image->x1);
   height = (gint) (image->y1);
   src_num_components = (gint) (image->numcomps);

   // Extend grey(a) into rgb(a) for now.
   dest_num_components = src_num_components;

   if (dest_num_components < 3)
      dest_num_components += 2;

   buf = g_new (guchar, dest_num_components * width * height);

   for (i = 0; i < width; ++i)
   {
      for (j = 0; j < height; ++j)
      {
         int dest_comp, dest_base;

         offset = i + (j * width);
         dest_base = dest_num_components * offset;
         dest_comp = 0;

         for (src_comp = 0; src_comp < src_num_components;src_comp++, dest_comp++)
         {
            buf[dest_comp + dest_base] = (guchar) (image->comps[src_comp].data[offset]);

            // Extend greyscale(a) into rgb(a) gimp layer for now.
            if ((src_num_components < 3) && (src_comp==0))
            {
                buf[1 + dest_base] = (guchar) (image->comps[0].data[offset]);
                buf[2 + dest_base] = (guchar) (image->comps[0].data[offset]);
                dest_comp += 2;
            }
         }
      }
   }

   gegl_buffer_set (buffer, GEGL_RECTANGLE (0, 0, width, height), 0, babl_format ("R'G'B' u8"),
                           buf, GEGL_AUTO_ROWSTRIDE);
						   
   g_free(buf);
}


// Transfers openjpeg format image to gimp equivalent - loaded as regular image or constructed in preview window as appropriate.
GimpImage * image_to_gimp(opj_image_t *image, const char *filename, gboolean preview)
{
   uint32 image_width, image_height;

   GimpImageBaseType type;
   GimpImageType  layer_type;
   gint32 layer_ID,image_ID;
   gint x1, y1, x2, y2, width, height;
   
   GimpImage         *gimp_image;
   GimpLayer         *layer;
   GeglBuffer        *buffer;

   gboolean contains_alpha = 0;

   if (!__Query(image, &image_width, &image_height, &contains_alpha))
   {
      fprintf(stderr,"The encoding method is not currently supported.");
      return 0;
   }

  /* create output image */

  width = (gint) (image->x1);
  height = (gint) (image->y1);
  x1 = (gint) (image->x0);
  y1 = (gint) (image->y0);
  x2 = x1 + width;
  y2 = y1 + height;

  // TODO: Get GIMP grey layers working. Grey duplicated across RGB for now.
  //if (image->numcomps < 3)
  //   type = GIMP_GRAY;
  //else
     type = GIMP_RGB;

  switch (image->numcomps)
  {
     case 1: layer_type = GIMP_RGB_IMAGE /*GIMP_GRAY_IMAGE*/;  break;
     case 2: layer_type = GIMP_RGBA_IMAGE /*GIMP_GRAYA_IMAGE*/; break;
     case 3: layer_type = GIMP_RGB_IMAGE;   break;
     case 4: layer_type = GIMP_RGBA_IMAGE;  break;

     default:
      fprintf(stderr,"Unsupported number of channels : %d.", image->numcomps);
      return 0;
  }

#if 0
  if (preview)
  {
      image_ID = preview_image_ID;

      // TODO: Preview layer format should match image format, not saved file format.
      preview_layer_ID = gimp_layer_new(preview_image_ID, "J2K preview", width, height, layer_type, 100, gimp_image_get_default_new_layer_mode (gimp_image));
      layer_ID = preview_layer_ID;
  }
  else
  {
     gimp_image = gimp_image_new(width, height, type);

     gimp_image_set_filename(image_ID, filename);
     layer_ID = gimp_layer_new (image_ID, "Background", width, height, layer_type, 100, gimp_image_get_default_new_layer_mode (gimp_image));
  }

  gimp_image_add_layer(image_ID, layer_ID, 0);
  drawable_global = drawable = gimp_drawable_get(layer_ID);
  gimp_drawable_mask_bounds(drawable->drawable_id, &x1, &y1, &x2, &y2);
  gimp_pixel_rgn_init(&rgn_in, drawable, x1, y1,x2 - x1, y2 - y1, TRUE, FALSE);
#endif

  gimp_image = gimp_image_new_with_precision (width, height, GIMP_RGB, GIMP_PRECISION_U32_NON_LINEAR);
  
  layer = gimp_layer_new (gimp_image, "Background",
                          width, height,
                          GIMP_RGBA_IMAGE, 100,
                          gimp_image_get_default_new_layer_mode (gimp_image));
						  
  gimp_image_insert_layer (gimp_image, layer, NULL, 0);
  buffer = gimp_drawable_get_buffer (GIMP_DRAWABLE (layer));


  // Convert the pixel data ...
  opj_pixel_data_to_gimp(image, buffer);

  return gimp_image;
}


typedef struct
{
   uint8 *data;
   uint8 *start;
   size_t len;
   size_t left;
} Buffer;



OPJ_SIZE_T memory_stream_read(void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data)
{
  Buffer *b = (Buffer*) p_user_data;

  if (b->left < p_nb_bytes)
  {
#if ENABLE_OPENJPEG_DIAGNOSTIC 
     fprintf(stderr, "memory_stream_read: requested %ld bytes. %ld available.\n", p_nb_bytes, b->left);
#endif

 		p_nb_bytes = b->left;
  }

  memcpy(p_buffer, b->data, p_nb_bytes);
  b->left -= p_nb_bytes;
  b->data += p_nb_bytes;

#if ENABLE_OPENJPEG_DIAGNOSTIC
  fprintf(stderr, "memory_stream_read: read %ld bytes. %ld remaining.\n", p_nb_bytes, b->left);
#endif

  return p_nb_bytes;
};



bool memory_stream_seek(OPJ_OFF_T bytes, void *p_user_data)
{
  Buffer *b = (Buffer*) p_user_data;
  
  assert(b->len >= bytes);

  if (b->len < bytes)
  {
#if ENABLE_OPENJPEG_DIAGNOSTIC
	  fprintf(stderr, "memory_stream_seek: bad.");
#endif

     return false;
  }

  b->data = b->start + bytes;
  b->left = b->len - bytes;
  
#if ENABLE_OPENJPEG_DIAGNOSTIC
  fprintf(stderr, "memory_stream_seek: offset %ld bytes. %ld remaining.\n", bytes, b->left);
#endif

  return true;
};



OPJ_OFF_T memory_stream_skip(OPJ_OFF_T bytes, void *p_user_data)
{
  Buffer *b = (Buffer*) p_user_data;
  
  assert(b->left >= bytes);

  if (b->left < bytes)
     bytes = b->left;

  b->left -= bytes;
  b->data += bytes;

#if ENABLE_OPENJPEG_DIAGNOSTIC
  fprintf(stderr, "memory_stream_skip: skip %ld bytes. %ld remaining.\n", bytes, b->left);
#endif

  return bytes;
};



// Loads jpeg-2000 image from memory & decodes it returning decoded image.
opj_image_t *decode_image(guint8 *src, uint32 buffer_length, bool format_codestream)
{
   if (!src || (buffer_length == 0))
      return 0;


   // Set decoding parameters to default decompression values
   opj_dparameters_t parameters;
   memset(&parameters, 0, sizeof(opj_dparameters_t));
   opj_set_default_decoder_parameters(&parameters);

 	
	// Get a decoder handle ...
	opj_codec_t *codec = opj_create_decompress(format_codestream ? OPJ_CODEC_J2K : OPJ_CODEC_JP2);


#if ENABLE_OPENJPEG_DIAGNOSTIC
	opj_set_info_handler(codec, info_callback, stderr);
	opj_set_warning_handler(codec, warning_callback, stderr);
	opj_set_error_handler(codec, error_callback, stderr);
#endif


	// Setup the decoder decoding parameters ...
   if (!opj_setup_decoder(codec, &parameters))
	{
       opj_destroy_codec(codec);
       return nullptr;
	}

   opj_stream_t *s = opj_stream_create(buffer_length, true);

   if (!s)
	{
       opj_destroy_codec(codec);
       return nullptr;
	}

   opj_stream_set_read_function(s, memory_stream_read);
   opj_stream_set_seek_function(s, memory_stream_seek);
   opj_stream_set_skip_function(s, memory_stream_skip);

	Buffer b;
	b.start = b.data = (uint8*) src;
	b.left = b.len = buffer_length;
 
   // TODO: This function is badly named, it provides the length of the custom stream, not the user data :)
   opj_stream_set_user_data_length(s, buffer_length);

   opj_stream_set_user_data(s, (void*) &b, NULL); 

 
#if ENABLE_OPENJPEG_DIAGNOSTIC
   if (format_codestream)
		fprintf(stderr, "decode_image[j2k]\n");
	else
		fprintf(stderr, "decode_image[jp2]\n");
#endif


   opj_image_t *image = nullptr;
   bool ok = opj_read_header(s, codec, &image);


   if (!ok)
	{
#if ENABLE_OPENJPEG_DIAGNOSTIC
	    fprintf(stderr, "decode_image[hdr-fail]\n");
#endif

	    opj_stream_destroy(s);
  	    opj_destroy_codec(codec);
       return nullptr;
	}

   ok = opj_decode(codec, s, image);

   if (codec)
   {
  		opj_end_decompress(codec, s);
		opj_stream_destroy(s);
	    opj_destroy_codec(codec);
   }

   return image;
}


opj_image_t *image_load(const gchar *filename)
{
  FILE *fsrc;
  size_t file_length;
  unsigned char *src;

  // Read the file into memory.

  fsrc = fopen (filename, "rb");

  if (!fsrc)
  {
      fprintf (stderr, "ERROR -> failed to open %s for reading\n", filename);
      return NULL;
  }


  fseek (fsrc, 0, SEEK_END);
  file_length = ftell (fsrc);
  fseek (fsrc, 0, SEEK_SET);
  src = (unsigned char *) malloc (file_length);
  size_t read_bytes = fread (src, 1, file_length, fsrc);
  fclose (fsrc);

  if (read_bytes != file_length)
  {
		fprintf (stderr, "ERROR -> failed to read all from %s. Got %ld of %ld\n", filename, read_bytes, file_length);
      return NULL;
  }


  // Guess format from extension.
  const char *ext = strrchr(filename, '.');
  gboolean format_codestream = ext && !stricmp(ext, ".j2k");

  opj_image_t *opj_image = decode_image(src, file_length, format_codestream);

  // If load failed, try other format - file could be named incorrectly.
  if (!opj_image)
     opj_image = decode_image(src, file_length, !format_codestream);

  return opj_image;
}

#endif