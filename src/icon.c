#include "icon.h"

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdbool.h>
#include <string.h>

#include "log.h"
#include "notification.h"
#include "settings.h"
#include "utils.h"

static bool is_readable_file(const char *filename)
{
        return (access(filename, R_OK) != -1);
}

const char *get_filename_ext(const char *filename)
{
        const char *dot = strrchr(filename, '.');
        if (!dot || dot == filename) return "";
        return dot + 1;
}

static cairo_status_t read_from_buf(void *closure, unsigned char *data, unsigned int size)
{
        GByteArray *buf = (GByteArray *)closure;

        unsigned int cpy = MIN(size, buf->len);
        memcpy(data, buf->data, cpy);
        g_byte_array_remove_range(buf, 0, cpy);

        return CAIRO_STATUS_SUCCESS;
}


cairo_surface_t *gdk_pixbuf_to_cairo_surface(GdkPixbuf *pixbuf)
{
        /*
         * Export the gdk pixbuf into buffer as a png and import the png buffer
         * via cairo again as a cairo_surface_t.
         * It looks counterintuitive, as there is gdk_cairo_set_source_pixbuf,
         * which does the job faster. But this would require gtk3 as a dependency
         * for a single function call. See discussion in #334 and #376.
         */
        cairo_surface_t *icon_surface = NULL;
        GByteArray *buffer;
        char *bufstr;
        gsize buflen;

        gdk_pixbuf_save_to_buffer(pixbuf, &bufstr, &buflen, "png", NULL, NULL);

        buffer = g_byte_array_new_take((guint8*)bufstr, buflen);
        icon_surface = cairo_image_surface_create_from_png_stream(read_from_buf, buffer);

        g_byte_array_free(buffer, TRUE);

        return icon_surface;
}

GdkPixbuf *get_pixbuf_from_file(const char *filename)
{
        char *path = string_to_path(g_strdup(filename));
        GError *error = NULL;

        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);

        if (error) {
                LOG_W("%s", error->message);
                g_error_free(error);
        }

        g_free(path);
        return pixbuf;
}

GdkPixbuf *get_pixbuf_from_icon(const char *iconname)
{
        if (STR_EMPTY(iconname))
                return NULL;

        const char *suffixes[] = { ".svg", ".png", ".xpm", NULL };
        GdkPixbuf *pixbuf = NULL;
        gchar *uri_path = NULL;

        if (g_str_has_prefix(iconname, "file://")) {
                uri_path = g_filename_from_uri(iconname, NULL, NULL);
                if (uri_path)
                        iconname = uri_path;
        }

        /* absolute path? */
        if (iconname[0] == '/' || iconname[0] == '~') {
                pixbuf = get_pixbuf_from_file(iconname);
        } else {
        /* search in icon_path */
                char *start = settings.icon_path,
                     *end, *current_folder, *maybe_icon_path;
                do {
                        end = strchr(start, ':');
                        if (!end) end = strchr(settings.icon_path, '\0'); /* end = end of string */

                        current_folder = g_strndup(start, end - start);

                        for (const char **suf = suffixes; *suf; suf++) {
                                maybe_icon_path = g_strconcat(current_folder, "/", iconname, *suf, NULL);
                                if (is_readable_file(maybe_icon_path))
                                        pixbuf = get_pixbuf_from_file(maybe_icon_path);
                                g_free(maybe_icon_path);

                                if (pixbuf)
                                        break;
                        }

                        g_free(current_folder);
                        if (pixbuf)
                                break;

                        start = end + 1;
                } while (STR_FULL(end));
                if (!pixbuf)
                        LOG_W("No icon found in path: '%s'", iconname);
        }

        g_free(uri_path);
        return pixbuf;
}

GdkPixbuf *get_pixbuf_from_raw_image(const struct raw_image *raw_image)
{
        GdkPixbuf *pixbuf = NULL;

        pixbuf = gdk_pixbuf_new_from_data(raw_image->data,
                                          GDK_COLORSPACE_RGB,
                                          raw_image->has_alpha,
                                          raw_image->bits_per_sample,
                                          raw_image->width,
                                          raw_image->height,
                                          raw_image->rowstride,
                                          NULL,
                                          NULL);

        return pixbuf;
}

cairo_surface_t *icon_get_for_notification(const struct notification *n)
{
        GdkPixbuf *pixbuf;

        if (n->raw_icon)
                pixbuf = get_pixbuf_from_raw_image(n->raw_icon);
        else if (n->icon)
                pixbuf = get_pixbuf_from_icon(n->icon);
        else
                return NULL;

        if (!pixbuf)
                return NULL;

        int w = gdk_pixbuf_get_width(pixbuf);
        int h = gdk_pixbuf_get_height(pixbuf);
        int larger = w > h ? w : h;
        if (settings.max_icon_size && larger > settings.max_icon_size) {
                int scaled_w = settings.max_icon_size;
                int scaled_h = settings.max_icon_size;
                if (w >= h)
                        scaled_h = (settings.max_icon_size * h) / w;
                else
                        scaled_w = (settings.max_icon_size * w) / h;

                GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
                                pixbuf,
                                scaled_w,
                                scaled_h,
                                GDK_INTERP_BILINEAR);
                g_object_unref(pixbuf);
                pixbuf = scaled;
        }

        cairo_surface_t *ret = gdk_pixbuf_to_cairo_surface(pixbuf);
        g_object_unref(pixbuf);
        return ret;
}

/* vim: set tabstop=8 shiftwidth=8 expandtab textwidth=0: */
