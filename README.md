# gimp_j2k
GIMP3 J2K plugin

Reworks our previous GIMP2 OpenJPEG based J2K plugin. Implements only save/write - read is provided but disabled as there's already common/file-jp2-load.c which implements that in the GIMP3 master tree.

Add 

subdir('file-openjpeg')

to gimp\plug-ins\meson.build & ensure source is located in gimp\plug-ins\file-openjpeg

Build GIMP3 as normal. You should now have j2k write super powers with quality slider working but interactive preview of quality not at this time.

Further work: 

Fix up interactive preview.
openjpeg supports a large number of tweakable parameters to refine write size/quality. Explore those.
