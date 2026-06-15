/* config.c — قراءة الإعدادات، تحميل الخلفية والأفاتار، اكتشاف المستخدمين */

#include "config.h"
#include "globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* ─── read_config_string ────────────────────────────────────────────────── */
void read_config_string(const char *key, char *out_val, size_t max_len,
                        const char *default_val)
{
    FILE *f = NULL;

    if (mock_mode) {
        f = fopen("conf/vaxp-dm.conf", "r");
        if (!f)
            f = fopen("/etc/vaxp-dm.conf", "r");
    } else {
        f = fopen("/etc/vaxp-dm.conf", "r");
        if (!f)
            f = fopen("conf/vaxp-dm.conf", "r");
    }

    if (!f) {
        strncpy(out_val, default_val, max_len);
        out_val[max_len - 1] = '\0';
        return;
    }

    char line[256];
    char search_key[128];
    snprintf(search_key, sizeof(search_key), "%s =", key);
    size_t key_len = strlen(search_key);

    while (fgets(line, sizeof(line), f)) {
        char *ptr = line;
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        if (*ptr == '#' || *ptr == ';') continue;
        if (strncmp(ptr, search_key, key_len) == 0 ||
            strncmp(ptr, key, strlen(key)) == 0) {
            char *eq = strchr(ptr, '=');
            if (eq) {
                char *val = eq + 1;
                while (*val == ' ' || *val == '\t') val++;
                val[strcspn(val, "\r\n")] = '\0';
                size_t val_len = strlen(val);
                while (val_len > 0 &&
                       (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) {
                    val[val_len - 1] = '\0';
                    val_len--;
                }
                strncpy(out_val, val, max_len);
                out_val[max_len - 1] = '\0';
                fclose(f);
                return;
            }
        }
    }

    fclose(f);
    strncpy(out_val, default_val, max_len);
    out_val[max_len - 1] = '\0';
}

/* ─── parse_hex_color ───────────────────────────────────────────────────── */
bool parse_hex_color(const char *hex, double *r, double *g, double *b)
{
    if (!hex) return false;
    if (hex[0] == '#') hex++;
    if (strlen(hex) != 6) return false;
    unsigned int rc, gc, bc;
    if (sscanf(hex, "%02x%02x%02x", &rc, &gc, &bc) == 3) {
        *r = rc / 255.0;
        *g = gc / 255.0;
        *b = bc / 255.0;
        return true;
    }
    return false;
}

/* ─── load_image ────────────────────────────────────────────────────────── */
cairo_surface_t *load_image(const char *filename)
{
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (!pixbuf) {
        if (error) {
            fprintf(stderr, "Failed to load image %s: %s\n",
                    filename, error->message);
            g_error_free(error);
        }
        return NULL;
    }

    int w             = gdk_pixbuf_get_width(pixbuf);
    int h             = gdk_pixbuf_get_height(pixbuf);
    int has_alpha     = gdk_pixbuf_get_has_alpha(pixbuf);
    int src_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *src_pixels = gdk_pixbuf_get_pixels(pixbuf);

    cairo_surface_t *surface =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(surface);
    unsigned char *dest_pixels = cairo_image_surface_get_data(surface);
    int dest_rowstride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < h; y++) {
        const guchar  *src_row  = src_pixels  + y * src_rowstride;
        unsigned char *dest_row = dest_pixels + y * dest_rowstride;
        for (int x = 0; x < w; x++) {
            if (has_alpha) {
                const guchar  *p = src_row  + x * 4;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3];
            } else {
                const guchar  *p = src_row  + x * 3;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = 255;
            }
        }
    }
    cairo_surface_mark_dirty(surface);
    g_object_unref(pixbuf);
    return surface;
}

/* ─── load_background ───────────────────────────────────────────────────── */
void load_background(void)
{
    if (bg_surface) {
        cairo_surface_destroy(bg_surface);
        bg_surface = NULL;
    }

    char bg_path[512] = "";
    read_config_string("background_path", bg_path, sizeof(bg_path), "");

    if (strlen(bg_path) == 0 || access(bg_path, F_OK) != 0) {
        const char *home = getenv("HOME");
        if (home)
            snprintf(bg_path, sizeof(bg_path),
                     "%s/.config/vaxp/background", home);
        if (access(bg_path, F_OK) != 0)
            snprintf(bg_path, sizeof(bg_path),
                     "/home/x/.config/vaxp/background");
    }

    if (access(bg_path, F_OK) == 0)
        bg_surface = load_image(bg_path);
}

/* ─── load_avatar ───────────────────────────────────────────────────────── */
void load_avatar(void)
{
    if (avatar_surface) {
        cairo_surface_destroy(avatar_surface);
        avatar_surface = NULL;
    }

    char avatar_path[512] = "";

    /* 1. ~/.face */
    const char *home = getenv("HOME");
    if (home)
        snprintf(avatar_path, sizeof(avatar_path), "%s/.face", home);

    /* 2. AccountsService icon */
    if (access(avatar_path, F_OK) != 0)
        snprintf(avatar_path, sizeof(avatar_path),
                 "/var/lib/AccountsService/icons/%s",
                 getenv("USER") ? getenv("USER") : "user");

    /* 3. Fallback to logo.png in known locations */
    const char *fallbacks[] = {
        "/usr/share/vaxp-dm/logo.png",
        "/usr/local/share/vaxp-dm/logo.png",
        "/etc/vaxp-dm/logo.png",
        "conf/logo.png",
        "logo.png",
        NULL
    };
    for (int i = 0; fallbacks[i] && access(avatar_path, F_OK) != 0; i++)
        snprintf(avatar_path, sizeof(avatar_path), "%s", fallbacks[i]);

    /* 4. Directory of the executable */
    if (access(avatar_path, F_OK) != 0) {
        char exe_path[512] = "";
        ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (n > 0) {
            exe_path[n] = '\0';
            char *slash = strrchr(exe_path, '/');
            if (slash) {
                snprintf(slash + 1,
                         sizeof(exe_path) - (slash - exe_path) - 1,
                         "logo.png");
                snprintf(avatar_path, sizeof(avatar_path), "%s", exe_path);
            }
        }
    }

    if (access(avatar_path, F_OK) != 0)
        return; /* No image found — draw_avatar() will use the fallback silhouette */

    GError *error = NULL;
    GdkPixbuf *pixbuf =
        gdk_pixbuf_new_from_file_at_scale(avatar_path, 80, 80, TRUE, &error);
    if (!pixbuf) {
        if (error) g_error_free(error);
        return;
    }

    int w             = gdk_pixbuf_get_width(pixbuf);
    int h             = gdk_pixbuf_get_height(pixbuf);
    int has_alpha     = gdk_pixbuf_get_has_alpha(pixbuf);
    int src_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *src_pixels = gdk_pixbuf_get_pixels(pixbuf);

    avatar_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(avatar_surface);
    unsigned char *dest_pixels = cairo_image_surface_get_data(avatar_surface);
    int dest_rowstride = cairo_image_surface_get_stride(avatar_surface);

    for (int y = 0; y < h; y++) {
        const guchar  *src_row  = src_pixels  + y * src_rowstride;
        unsigned char *dest_row = dest_pixels + y * dest_rowstride;
        for (int x = 0; x < w; x++) {
            if (has_alpha) {
                const guchar  *p = src_row  + x * 4;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3];
            } else {
                const guchar  *p = src_row  + x * 3;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = 255;
            }
        }
    }
    cairo_surface_mark_dirty(avatar_surface);
    g_object_unref(pixbuf);
}

/* ─── discover_users ────────────────────────────────────────────────────── */
void discover_users(void)
{
    struct passwd *pw;
    system_user_count = 0;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid >= 1000 && pw->pw_uid < 60000) {
            if (pw->pw_shell &&
                strcmp(pw->pw_shell, "/usr/sbin/nologin") != 0 &&
                strcmp(pw->pw_shell, "/sbin/nologin")     != 0 &&
                strcmp(pw->pw_shell, "/bin/false")        != 0) {
                strncpy(system_users[system_user_count],
                        pw->pw_name, 31);
                system_users[system_user_count][31] = '\0';
                system_user_count++;
                if (system_user_count >= 16) break;
            }
        }
    }
    endpwent();
}
