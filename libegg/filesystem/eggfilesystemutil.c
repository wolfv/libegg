#include <config.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <libgnomeui/gnome-icon-theme.h>
#include <libgnomeui/gnome-icon-lookup.h>
#ifdef HAVE_RSVG
#include <librsvg/rsvg.h>
#endif
#include <math.h>
#include "eggfilesystemutil.h"

#define ICON_SIZE_STANDARD 20
#define ICON_SIZE_MAX 24

static gboolean
path_represents_svg_image (const char *path)
{
        /* Synchronous mime sniffing is a really bad idea here
         * since it's only useful for people adding custom icons,
         * and if they're doing that, they can behave themselves
         * and use a .svg extension.
         */
        return path != NULL && strstr (path, ".svg") != NULL;
}

/* This loads an SVG image, scaling it to the appropriate size. */
#ifdef HAVE_RSVG
static GdkPixbuf *
load_pixbuf_svg (const char *path,
		 guint size_in_pixels,
		 guint base_size)
{
	double zoom;
	GdkPixbuf *pixbuf;

	if (base_size != 0) {
		zoom = (double)size_in_pixels / base_size;

		pixbuf = rsvg_pixbuf_from_file_at_zoom_with_max (path, zoom, zoom, ICON_SIZE_STANDARD, ICON_SIZE_STANDARD, NULL);
	} else {
		pixbuf = rsvg_pixbuf_from_file_at_max_size (path,
							    size_in_pixels,
							    size_in_pixels,
							    NULL);
	}

	if (pixbuf == NULL) {
		return NULL;
	}
	
	return pixbuf;
}
#endif

static GdkPixbuf *
scale_icon (GdkPixbuf *pixbuf,
	    double *scale)
{
	guint width, height;

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	width = floor (width * *scale + 0.5);
	height = floor (height * *scale + 0.5);
	
	return gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
}

static GdkPixbuf *
load_icon_file (char          *filename,
		guint          base_size,
		guint          nominal_size)
{
	GdkPixbuf *pixbuf, *scaled_pixbuf;
	int width, height, size;
	double scale;

	if (path_represents_svg_image (filename)) {
#ifdef HAVE_RSVG
		pixbuf = load_pixbuf_svg (filename,
					  nominal_size,
					  base_size);
#else
		/* svg file, and no svg support... */
		return NULL;
#endif
	} else {
		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);

		if (pixbuf == NULL) {
			return NULL;
		}
		
		if (base_size == 0) {
			width = gdk_pixbuf_get_width (pixbuf); 
			height = gdk_pixbuf_get_height (pixbuf);
			size = MAX (width, height);
			if (size > ICON_SIZE_MAX) {
				base_size = size;
			} else {
				/* Don't scale up small icons */
				base_size = ICON_SIZE_STANDARD;
			}
		}
		
		if (base_size != nominal_size) {
			scale = (double)nominal_size/base_size;
			scaled_pixbuf = scale_icon (pixbuf, &scale);
			g_object_unref (pixbuf);
			pixbuf = scaled_pixbuf;
		}
	}

	return pixbuf;
}

GdkPixbuf *
egg_file_system_util_get_icon (GnomeIconTheme *theme, const gchar *uri,
			       const gchar *mime_type)
{
	gchar *icon;
	gchar *filename;
	const GnomeIconData *icon_data;
	int base_size;
	GdkPixbuf *pixbuf;
	
	icon = gnome_icon_lookup (theme, NULL, uri, NULL, NULL,
				  mime_type, 0, NULL);
	

	g_return_val_if_fail (icon != NULL, NULL);

	filename = gnome_icon_theme_lookup_icon (theme, icon,
						 ICON_SIZE_STANDARD,
						 &icon_data,
						 &base_size);
	g_free (icon);

	pixbuf = load_icon_file (filename, base_size, ICON_SIZE_STANDARD);
	g_free (filename);
	
	
	return pixbuf;
}
