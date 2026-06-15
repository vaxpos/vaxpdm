#include <gtk/gtk.h>
#include <cairo.h>
#include <pango/pangocairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <xkbcommon/xkbcommon.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <poll.h>
#include <glob.h>
#include <linux/input.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/kd.h>
#include "pam_auth.h"
#include "session.h"

#define FOCUS_USERNAME  0
#define FOCUS_PASSWORD  1
#define FOCUS_LOGIN     2
#define FOCUS_SHUTDOWN  3
#define FOCUS_REBOOT    4
#define FOCUS_SUSPEND   5
#define FOCUS_SETTINGS  6
#define FOCUS_COUNT     7

#define MAX_KEYBOARDS 8
#define MAX_MICE      8

// State variables
static int current_focus = FOCUS_USERNAME;
static char username[32] = "";
static char password[32] = "";
static int selected_session_idx = 0;
static int session_count = 0;
static session_t *sessions = NULL;
static bool mock_mode = false;
static bool autologin_enabled = false;
static char status_message[64] = "Welcome to VAXP OS";
static char active_layout[8] = "EN";

// Mouse state
static double mouse_x = 512, mouse_y = 384;
static bool   mouse_shown = false;    // true once mouse has moved
static int    screen_w_g  = 1024;
static int    screen_h_g  = 768;

// Mouse devices (DRM mode)
static int mouse_fds[MAX_MICE];
static int mouse_count = 0;

// Settings panel state
static bool settings_open = false;

// User avatar image
static cairo_surface_t *avatar_surface = NULL;

static void load_avatar() {
    if (avatar_surface) {
        cairo_surface_destroy(avatar_surface);
        avatar_surface = NULL;
    }
    char avatar_path[512] = "";

    // 1. Try user's face icon (~/.face)
    const char *home = getenv("HOME");
    if (home) {
        snprintf(avatar_path, sizeof(avatar_path), "%s/.face", home);
    }
    // 2. Try AccountsService icon
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "/var/lib/AccountsService/icons/%s",
                 getenv("USER") ? getenv("USER") : "user");
    }
    // 3. Fallback to our own logo.png
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "/usr/share/vaxp-dm/logo.png");
    }
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "/usr/local/share/vaxp-dm/logo.png");
    }
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "/etc/vaxp-dm/logo.png");
    }
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "conf/logo.png");
    }
    if (access(avatar_path, F_OK) != 0) {
        snprintf(avatar_path, sizeof(avatar_path), "logo.png");
    }
    if (access(avatar_path, F_OK) != 0) {
        // Try directory of executable
        char exe_path[512] = "";
        ssize_t n = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (n > 0) {
            exe_path[n] = '\0';
            // Replace binary name with logo.png
            char *slash = strrchr(exe_path, '/');
            if (slash) {
                snprintf(slash + 1, sizeof(exe_path) - (slash - exe_path) - 1, "logo.png");
                snprintf(avatar_path, sizeof(avatar_path), "%s", exe_path);
            }
        }
    }

    if (access(avatar_path, F_OK) == 0) {
        GError *error = NULL;
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(avatar_path, 80, 80, TRUE, &error);
        if (pixbuf) {
            int w = gdk_pixbuf_get_width(pixbuf);
            int h = gdk_pixbuf_get_height(pixbuf);
            int has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
            int src_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
            const guchar *src_pixels = gdk_pixbuf_get_pixels(pixbuf);
            avatar_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
            cairo_surface_flush(avatar_surface);
            unsigned char *dest_pixels = cairo_image_surface_get_data(avatar_surface);
            int dest_rowstride = cairo_image_surface_get_stride(avatar_surface);
            for (int y = 0; y < h; y++) {
                const guchar *src_row = src_pixels + y * src_rowstride;
                unsigned char *dest_row = dest_pixels + y * dest_rowstride;
                for (int x = 0; x < w; x++) {
                    if (has_alpha) {
                        const guchar *p = src_row + x * 4;
                        unsigned char *d = dest_row + x * 4;
                        d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = p[3];
                    } else {
                        const guchar *p = src_row + x * 3;
                        unsigned char *d = dest_row + x * 4;
                        d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d[3] = 255;
                    }
                }
            }
            cairo_surface_mark_dirty(avatar_surface);
            g_object_unref(pixbuf);
        } else if (error) { g_error_free(error); }
    }
}

static char system_users[16][32];
static int system_user_count = 0;
static int selected_user_idx = 0;

static void discover_users() {
    struct passwd *pw;
    system_user_count = 0;
    setpwent();
    while ((pw = getpwent()) != NULL) {
        if (pw->pw_uid >= 1000 && pw->pw_uid < 60000) {
            if (pw->pw_shell && strcmp(pw->pw_shell, "/usr/sbin/nologin") != 0 &&
                strcmp(pw->pw_shell, "/sbin/nologin") != 0 &&
                strcmp(pw->pw_shell, "/bin/false") != 0) {
                strncpy(system_users[system_user_count], pw->pw_name, 31);
                system_users[system_user_count][31] = '\0';
                system_user_count++;
                if (system_user_count >= 16) break;
            }
        }
    }
    endpwent();
}

// Theme colors
static double theme_title_r = 0.796, theme_title_g = 0.651, theme_title_b = 0.969; // #cba6f7
static double theme_focus_r = 0.537, theme_focus_g = 0.702, theme_focus_b = 0.980; // #89b4fa
static double theme_inactive_r = 0.498, theme_inactive_g = 0.549, theme_inactive_b = 0.553; // #7f8c8d
static double theme_status_r = 0.976, theme_status_g = 0.886, theme_status_b = 0.686; // #f9e2af

// Event loop flags
static bool running = true;
static bool do_login = false;
static bool do_reboot = false;
static bool do_shutdown = false;
static bool do_suspend = false;
static bool cursor_visible = true;

// Background image surface
static cairo_surface_t *bg_surface = NULL;

// GTK Mock Mode widget
static GtkWidget *drawing_area = NULL;

// DRM/KMS DRM state
static int drm_fd = -1;
static uint32_t connector_id = 0;
static uint32_t fb_id = 0;
static uint32_t dumb_handle = 0;
static uint64_t dumb_size = 0;
static void *map_ptr = NULL;
static drmModeCrtc *orig_crtc = NULL;

// Keyboard input state
static int kbd_fds[MAX_KEYBOARDS];
static int kbd_count = 0;
static struct termios orig_term;

// XKB state
static struct xkb_context *xkb_ctx = NULL;
static struct xkb_keymap *xkb_keymap = NULL;
static struct xkb_state *xkb_state = NULL;

static void read_config_string(const char *key, char *out_val, size_t max_len, const char *default_val) {
    FILE *f = NULL;
    if (mock_mode) {
        f = fopen("conf/vaxp-dm.conf", "r");
        if (!f) {
            f = fopen("/etc/vaxp-dm.conf", "r");
        }
    } else {
        f = fopen("/etc/vaxp-dm.conf", "r");
        if (!f) {
            f = fopen("conf/vaxp-dm.conf", "r");
        }

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
        if (strncmp(ptr, search_key, key_len) == 0 || strncmp(ptr, key, strlen(key)) == 0) {
            char *eq = strchr(ptr, '=');
            if (eq) {
                char *val = eq + 1;
                while (*val == ' ' || *val == '\t') val++;
                val[strcspn(val, "\r\n")] = '\0';
                size_t val_len = strlen(val);
                while (val_len > 0 && (val[val_len - 1] == ' ' || val[val_len - 1] == '\t')) {
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

static bool parse_hex_color(const char *hex, double *r, double *g, double *b) {
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

// GdkPixbuf loader to Cairo Image Surface
static cairo_surface_t *load_image(const char *filename) {
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filename, &error);
    if (!pixbuf) {
        if (error) {
            fprintf(stderr, "Failed to load image %s: %s\n", filename, error->message);
            g_error_free(error);
        }
        return NULL;
    }

    int w = gdk_pixbuf_get_width(pixbuf);
    int h = gdk_pixbuf_get_height(pixbuf);
    int has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
    int src_rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    const guchar *src_pixels = gdk_pixbuf_get_pixels(pixbuf);

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_surface_flush(surface);
    unsigned char *dest_pixels = cairo_image_surface_get_data(surface);
    int dest_rowstride = cairo_image_surface_get_stride(surface);

    for (int y = 0; y < h; y++) {
        const guchar *src_row = src_pixels + y * src_rowstride;
        unsigned char *dest_row = dest_pixels + y * dest_rowstride;
        for (int x = 0; x < w; x++) {
            if (has_alpha) {
                const guchar *p = src_row + x * 4;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; // B
                d[1] = p[1]; // G
                d[2] = p[0]; // R
                d[3] = p[3]; // A
            } else {
                const guchar *p = src_row + x * 3;
                unsigned char *d = dest_row + x * 4;
                d[0] = p[2]; // B
                d[1] = p[1]; // G
                d[2] = p[0]; // R
                d[3] = 255;  // A
            }
        }
    }
    cairo_surface_mark_dirty(surface);
    g_object_unref(pixbuf);
    return surface;
}

static void load_background() {
    if (bg_surface) {
        cairo_surface_destroy(bg_surface);
        bg_surface = NULL;
    }
    char bg_path[512] = "";
    read_config_string("background_path", bg_path, sizeof(bg_path), "");
    if (strlen(bg_path) == 0 || access(bg_path, F_OK) != 0) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(bg_path, sizeof(bg_path), "%s/.config/vaxp/background", home);
        }
        if (access(bg_path, F_OK) != 0) {
            snprintf(bg_path, sizeof(bg_path), "/home/x/.config/vaxp/background");
        }
    }
    if (access(bg_path, F_OK) == 0) {
        bg_surface = load_image(bg_path);
    }
}

// Drawing helper functions
static void draw_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI_2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI_2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, -M_PI_2);
    cairo_close_path(cr);
}

static void draw_glow(cairo_t *cr, double x, double y, double w, double h, double r, double max_alpha, double size) {
    cairo_save(cr);
    int steps = 8;
    for (int i = 0; i < steps; i++) {
        double offset = size * ((double)i / steps);
        double alpha = max_alpha * (1.0 - (double)i / steps);
        cairo_set_source_rgba(cr, theme_focus_r, theme_focus_g, theme_focus_b, alpha);
        draw_rounded_rect(cr, x - offset, y - offset, w + offset*2, h + offset*2, r + offset);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

// Draw a circular avatar / user icon
static void draw_avatar(cairo_t *cr, double cx, double cy, double radius) {
    cairo_save(cr);
    // Clip to circle
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_clip(cr);

    if (avatar_surface) {
        int aw = cairo_image_surface_get_width(avatar_surface);
        int ah = cairo_image_surface_get_height(avatar_surface);
        double scale = (2.0 * radius) / (aw < ah ? aw : ah);
        double ox = cx - radius - (aw * scale - 2.0 * radius) / 2.0;
        double oy = cy - radius - (ah * scale - 2.0 * radius) / 2.0;
        cairo_translate(cr, ox, oy);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, avatar_surface, 0, 0);
        cairo_paint(cr);
    } else {
        // Fallback: gradient fill + person silhouette
        cairo_pattern_t *grad = cairo_pattern_create_radial(cx, cy - radius * 0.2, radius * 0.1,
                                                             cx, cy, radius);
        cairo_pattern_add_color_stop_rgb(grad, 0, 0.30, 0.25, 0.45);
        cairo_pattern_add_color_stop_rgb(grad, 1, 0.12, 0.10, 0.22);
        cairo_set_source(cr, grad);
        cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);
        // Head
        double hr = radius * 0.32;
        cairo_arc(cr, cx, cy - radius * 0.18, hr, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.85);
        cairo_fill(cr);
        // Body (ellipse)
        cairo_save(cr);
        cairo_translate(cr, cx, cy + radius * 0.45);
        cairo_scale(cr, 1.0, 0.5);
        cairo_arc(cr, 0, 0, radius * 0.54, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.85);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    // Outer ring
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.55);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
}

// Draw a circular icon button (power/reboot/suspend)
static void draw_icon_button(cairo_t *cr, double cx, double cy, double r, bool focused,
                              double fr, double fg, double fb, const char *symbol) {
    cairo_save(cr);
    // Background circle
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    if (focused) {
        // Glow
        for (int i = 6; i >= 0; i--) {
            double off = (double)i * 1.2;
            cairo_arc(cr, cx, cy, r + off, 0, 2 * M_PI);
            cairo_set_source_rgba(cr, fr, fg, fb, 0.04 * (7 - i));
            cairo_fill(cr);
        }
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, fr, fg, fb, 0.25);
    } else {
        cairo_set_source_rgba(cr, 0.15, 0.13, 0.25, 0.75);
    }
    cairo_fill(cr);
    // Border
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    if (focused) {
        cairo_set_source_rgba(cr, fr, fg, fb, 0.9);
        cairo_set_line_width(cr, 1.8);
    } else {
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.25);
        cairo_set_line_width(cr, 1.2);
    }
    cairo_stroke(cr);

    // Symbol text
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, symbol, -1);
    PangoFontDescription *desc = pango_font_description_from_string("Sans 13");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
    if (focused) {
        cairo_set_source_rgba(cr, fr, fg, fb, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.75);
    }
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    cairo_restore(cr);
}

// Unified interface renderer
static void draw_interface(cairo_t *cr, int width, int height) {
    // 1. Draw Background
    if (bg_surface) {
        int img_w = cairo_image_surface_get_width(bg_surface);
        int img_h = cairo_image_surface_get_height(bg_surface);
        cairo_save(cr);
        double scale_x = (double)width / img_w;
        double scale_y = (double)height / img_h;
        double scale = (scale_x > scale_y) ? scale_x : scale_y;
        double offset_x = (width - img_w * scale) / 2.0;
        double offset_y = (height - img_h * scale) / 2.0;
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, bg_surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_save(cr);
        cairo_pattern_t *pat = cairo_pattern_create_linear(0, 0, width, height);
        cairo_pattern_add_color_stop_rgb(pat, 0, 0.15, 0.10, 0.25);
        cairo_pattern_add_color_stop_rgb(pat, 1, 0.05, 0.05, 0.14);
        cairo_set_source(cr, pat);
        cairo_paint(cr);
        cairo_pattern_destroy(pat);
        cairo_restore(cr);
    }

    // 2. Glassmorphic login card (narrower, taller — like lock screen)
    double box_w = 300;
    double box_h = 440;
    double box_x = (width - box_w) / 2.0;
    double box_y = (height - box_h) / 2.0;
    double box_r = 22.0;

    // Drop Shadow
    cairo_save(cr);
    for (int i = 12; i >= 0; i--) {
        double offset = 2.0 + (double)i * 2.0;
        double alpha = 0.045 * (1.0 - (double)i / 13.0);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, alpha);
        draw_rounded_rect(cr, box_x - offset, box_y - offset + 4, box_w + offset*2, box_h + offset*2, box_r + offset);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    // Box body — blurred glass effect via layered semitransparent fill
    cairo_save(cr);
    draw_rounded_rect(cr, box_x, box_y, box_w, box_h, box_r);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_fill_preserve(cr);
    // Inner highlight at top
    cairo_pattern_t *top_shine = cairo_pattern_create_linear(box_x, box_y, box_x, box_y + box_h * 0.4);
    cairo_pattern_add_color_stop_rgba(top_shine, 0, 1, 1, 1, 0.06);
    cairo_pattern_add_color_stop_rgba(top_shine, 1, 1, 1, 1, 0.0);
    cairo_set_source(cr, top_shine);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(top_shine);
    // Border
    cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.22);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    cairo_restore(cr);

    // 3. Date / Time at top of card
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char day_str[32], date_str[32], time_str[16];
        // Day name
        const char *days[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        snprintf(day_str, sizeof(day_str), "%s", days[tm_info->tm_wday]);
        // Date
        const char *months[] = {"January","February","March","April","May","June",
                                  "July","August","September","October","November","December"};
        snprintf(date_str, sizeof(date_str), "%d %s", tm_info->tm_mday, months[tm_info->tm_mon]);
        // Time
        snprintf(time_str, sizeof(time_str), "- %02d:%02d -", tm_info->tm_hour, tm_info->tm_min);

        cairo_save(cr);
        // Day name — large bold
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, day_str, -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 22");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, box_y + 20);
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.97);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        // Date
        layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, date_str, -1);
        desc = pango_font_description_from_string("Sans 13");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, box_y + 48);
        cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 0.85);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        // Time
        layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, time_str, -1);
        desc = pango_font_description_from_string("Sans 11");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, box_y + 68);
        cairo_set_source_rgba(cr, 0.75, 0.75, 0.85, 0.70);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    // 4. User avatar circle
    double av_cx = box_x + box_w / 2.0;
    double av_cy = box_y + 155;
    double av_r = 38.0;
    draw_avatar(cr, av_cx, av_cy, av_r);

    // Username below avatar
    {
        cairo_save(cr);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        char user_display[64];
        if (current_focus == FOCUS_USERNAME && system_user_count > 1) {
            snprintf(user_display, sizeof(user_display), "< %s >", username);
        } else if (strlen(username) > 0) {
            snprintf(user_display, sizeof(user_display), "%s", username);
        } else {
            snprintf(user_display, sizeof(user_display), "user");
        }
        pango_layout_set_text(layout, user_display, -1);
        PangoFontDescription *desc;
        if (current_focus == FOCUS_USERNAME) {
            desc = pango_font_description_from_string("Sans Bold 13");
            cairo_set_source_rgb(cr, theme_focus_r, theme_focus_g, theme_focus_b);
        } else {
            desc = pango_font_description_from_string("Sans Bold 13");
            cairo_set_source_rgba(cr, 0.85, 0.85, 0.92, 0.90);
        }
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, box_y + 203);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    // 5. Password field  (slim, centered, like lock screen)
    double field_w = 240;
    double field_h = 38;
    double field_x = box_x + (box_w - field_w) / 2.0;
    double pass_y = box_y + 233;

    cairo_save(cr);
    if (current_focus == FOCUS_PASSWORD) {
        draw_glow(cr, field_x, pass_y, field_w, field_h, 10.0, 0.18, 7.0);
    }
    draw_rounded_rect(cr, field_x, pass_y, field_w, field_h, 10.0);
    cairo_set_source_rgba(cr, 0.05, 0.04, 0.10, 0.65);
    cairo_fill_preserve(cr);
    if (current_focus == FOCUS_PASSWORD) {
        cairo_set_source_rgba(cr, theme_focus_r, theme_focus_g, theme_focus_b, 0.85);
        cairo_set_line_width(cr, 1.8);
    } else {
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.20);
        cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    {
        // Password icon (lock symbol placeholder: ●)
        PangoLayout *layout = pango_cairo_create_layout(cr);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.45);
        pango_layout_set_text(layout, "●", -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, field_x + 12, pass_y + (field_h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        // Password text / placeholder
        char masked[128] = "";
        int pw_len = strlen(password);
        for (int i = 0; i < pw_len && i < 32; i++) strcat(masked, "•");

        layout = pango_cairo_create_layout(cr);
        int text_w, text_h;
        if (pw_len == 0) {
            pango_layout_set_text(layout, "Enter password", -1);
            desc = pango_font_description_from_string("Sans Italic 11");
            cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.35);
        } else {
            pango_layout_set_text(layout, masked, -1);
            desc = pango_font_description_from_string("Sans 12");
            cairo_set_source_rgba(cr, 0.93, 0.93, 0.97, 0.95);
        }
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        pango_layout_get_pixel_size(layout, &text_w, &text_h);
        cairo_move_to(cr, field_x + 30, pass_y + (field_h - text_h) / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        if (current_focus == FOCUS_PASSWORD && cursor_visible) {
            cairo_set_source_rgba(cr, theme_focus_r, theme_focus_g, theme_focus_b, 0.9);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, field_x + 30 + text_w + 2, pass_y + 8);
            cairo_line_to(cr, field_x + 30 + text_w + 2, pass_y + field_h - 8);
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);

    // 6. Bottom row: Shutdown | Reboot | Suspend as circular icon buttons
    double icon_r = 18.0;
    double icons_y = box_y + box_h - icon_r - 22;
    double icon_spacing = 58.0;
    double icon_cx_center = box_x + box_w / 2.0;

    // Shutdown (power)
    draw_icon_button(cr, icon_cx_center - icon_spacing, icons_y, icon_r,
                     current_focus == FOCUS_SHUTDOWN,
                     0.953, 0.545, 0.659, "⏻");
    // Reboot
    draw_icon_button(cr, icon_cx_center, icons_y, icon_r,
                     current_focus == FOCUS_REBOOT,
                     0.976, 0.886, 0.686, "↺");
    // Suspend
    draw_icon_button(cr, icon_cx_center + icon_spacing, icons_y, icon_r,
                     current_focus == FOCUS_SUSPEND,
                     0.537, 0.702, 0.980, "☽");

    // 7. Status messages: selected session + welcome text
    {
        cairo_save(cr);
        double text_base_y = icons_y - icon_r - 8;

        // Welcome / status line
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, status_message, -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, text_base_y - th);
        cairo_set_source_rgba(cr, theme_status_r, theme_status_g, theme_status_b, 0.85);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        // Selected session name (above welcome line)
        if (session_count > 0) {
            char sess_label[128];
            snprintf(sess_label, sizeof(sess_label), "%s", sessions[selected_session_idx].name);
            layout = pango_cairo_create_layout(cr);
            pango_layout_set_text(layout, sess_label, -1);
            desc = pango_font_description_from_string("Sans Bold 9");
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr, box_x + (box_w - tw) / 2.0, text_base_y - th - 18);
            cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.80);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }
        cairo_restore(cr);
    }

    // 8. Keyboard layout tag (top-left of card)
    {
        cairo_save(cr);
        double tag_w = 30, tag_h = 18;
        double tag_x = box_x + 12, tag_y = box_y + 12;
        draw_rounded_rect(cr, tag_x, tag_y, tag_w, tag_h, 4.0);
        cairo_set_source_rgba(cr, 0.15, 0.13, 0.26, 0.80);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, theme_focus_r, theme_focus_g, theme_focus_b, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, active_layout, -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 9");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, tag_x + (tag_w - tw) / 2.0, tag_y + (tag_h - th) / 2.0);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.9);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    // 9. Settings gear button — fixed at bottom-right corner of screen
    double gear_r  = 22.0;
    double gear_cx = width  - gear_r - 20;
    double gear_cy = height - gear_r - 20;
    {
        cairo_save(cr);
        // Glow when active
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            for (int i = 6; i >= 0; i--) {
                double off = (double)i * 1.5;
                cairo_arc(cr, gear_cx, gear_cy, gear_r + off, 0, 2 * M_PI);
                cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.03 * (7 - i));
                cairo_fill(cr);
            }
        }
        // Button background
        cairo_arc(cr, gear_cx, gear_cy, gear_r, 0, 2 * M_PI);
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.28);
        } else {
            cairo_set_source_rgba(cr, 0.08, 0.07, 0.16, 0.80);
        }
        cairo_fill(cr);
        cairo_arc(cr, gear_cx, gear_cy, gear_r, 0, 2 * M_PI);
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.85);
            cairo_set_line_width(cr, 1.8);
        } else {
            cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.22);
            cairo_set_line_width(cr, 1.2);
        }
        cairo_stroke(cr);
        // Gear symbol
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, "⚙", -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans 15");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, gear_cx - tw / 2.0, gear_cy - th / 2.0);
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 1.0);
        } else {
            cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.55);
        }
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    // 10. Session popup panel — opens upward from gear button, auto-width
    if (settings_open) {
        int visible_sessions = session_count > 0 ? session_count : 1;

        // ── Compute minimum panel width from longest session name ──
        double panel_w = 140; // minimum width
        {
            cairo_save(cr);
            PangoLayout *tmp = pango_cairo_create_layout(cr);
            PangoFontDescription *fdesc = pango_font_description_from_string("Sans Bold 11");
            pango_layout_set_font_description(tmp, fdesc);
            pango_font_description_free(fdesc);
            for (int i = 0; i < visible_sessions; i++) {
                const char *sn = (session_count > 0) ? sessions[i].name : "No Sessions";
                pango_layout_set_text(tmp, sn, -1);
                int sw, sh; (void)sh;
                pango_layout_get_pixel_size(tmp, &sw, &sh);
                double needed = sw + 36.0; // 18px padding each side
                if (needed > panel_w) panel_w = needed;
            }
            g_object_unref(tmp);
            cairo_restore(cr);
        }

        double item_h  = 34;
        double header_h_approx = 22; // rough height of "Session" label + padding
        double panel_h = item_h * visible_sessions + header_h_approx + 8;

        // Open upward from gear button, right-aligned with it
        double panel_x = gear_cx + gear_r - panel_w; // right-align with gear
        double panel_y = gear_cy - gear_r - panel_h - 8;

        // Clamp to screen bounds
        if (panel_x < 10)               panel_x = 10;
        if (panel_x + panel_w > width - 10) panel_x = width - panel_w - 10;
        if (panel_y < 10)               panel_y = 10;
        if (panel_y + panel_h > height - 10) panel_y = height - panel_h - 10;

        cairo_save(cr);
        // Panel shadow
        for (int i = 10; i >= 0; i--) {
            double off = (double)i * 1.5;
            cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
            draw_rounded_rect(cr, panel_x - off, panel_y - off,
                              panel_w + off*2, panel_h + off*2, 14 + off);
            cairo_fill(cr);
        }
        // Panel body
        draw_rounded_rect(cr, panel_x, panel_y, panel_w, panel_h, 14);
        cairo_set_source_rgba(cr, 0.09, 0.08, 0.18, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.28);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);

        // Header label "Session"
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, "Session", -1);
        PangoFontDescription *desc = pango_font_description_from_string("Sans Bold 9");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, panel_x + (panel_w - tw) / 2.0, panel_y + 6);
        cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.60);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        // Thin separator under header
        double sep_y = panel_y + 6 + th + 4;
        cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, panel_x + 10, sep_y);
        cairo_line_to(cr, panel_x + panel_w - 10, sep_y);
        cairo_stroke(cr);

        // Session items
        double items_start_y = sep_y + 4;
        for (int i = 0; i < visible_sessions; i++) {
            double iy = items_start_y + i * item_h;
            bool is_selected = (i == selected_session_idx);

            if (is_selected) {
                draw_rounded_rect(cr, panel_x + 6, iy + 3, panel_w - 12, item_h - 6, 8);
                cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 0.20);
                cairo_fill(cr);
            }

            layout = pango_cairo_create_layout(cr);
            const char *sname = (session_count > 0) ? sessions[i].name : "No Sessions";
            pango_layout_set_text(layout, sname, -1);
            desc = pango_font_description_from_string(is_selected ? "Sans Bold 11" : "Sans 11");
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr, panel_x + (panel_w - tw) / 2.0, iy + (item_h - th) / 2.0);
            if (is_selected) {
                cairo_set_source_rgba(cr, theme_title_r, theme_title_g, theme_title_b, 1.0);
            } else {
                cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.75);
            }
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }
        cairo_restore(cr);
    }
}

static void init_xkb() {
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = {
        .rules = NULL,
        .model = NULL,
        .layout = "us,ara",
        .variant = NULL,
        .options = "grp:alt_shift_toggle"
    };
    xkb_keymap = xkb_keymap_new_from_names(xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkb_keymap) {
        fprintf(stderr, "Failed to compile XKB keymap. Trying default.\n");
        xkb_keymap = xkb_keymap_new_from_names(xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (xkb_keymap) {
        xkb_state = xkb_state_new(xkb_keymap);
    }
    strcpy(active_layout, "EN");
}

static void cleanup_xkb() {
    if (xkb_state) { xkb_state_unref(xkb_state); xkb_state = NULL; }
    if (xkb_keymap) { xkb_keymap_unref(xkb_keymap); xkb_keymap = NULL; }
    if (xkb_ctx) { xkb_context_unref(xkb_ctx); xkb_ctx = NULL; }
}

static void update_layout_name() {
    if (!xkb_keymap || !xkb_state) {
        strcpy(active_layout, "EN");
        return;
    }
    xkb_layout_index_t num = xkb_keymap_num_layouts(xkb_keymap);
    for (xkb_layout_index_t i = 0; i < num; i++) {
        if (xkb_state_layout_index_is_active(xkb_state, i, XKB_STATE_LAYOUT_EFFECTIVE)) {
            const char *name = xkb_keymap_layout_get_name(xkb_keymap, i);
            if (name && strcasestr(name, "arabic")) {
                strcpy(active_layout, "AR");
            } else {
                strcpy(active_layout, "EN");
            }
            return;
        }
    }
    strcpy(active_layout, "EN");
}

static void init_keyboards() {
    glob_t g;
    kbd_count = 0;
    if (glob("/dev/input/by-path/*-event-kbd", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && kbd_count < MAX_KEYBOARDS; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                if (ioctl(fd, EVIOCGRAB, 1) < 0) {
                    perror("EVIOCGRAB failed");
                }
                kbd_fds[kbd_count++] = fd;
            }
        }
        globfree(&g);
    }
    if (kbd_count == 0) {
        // Fallback to event0 if no keyboard found by path
        int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ioctl(fd, EVIOCGRAB, 1);
            kbd_fds[kbd_count++] = fd;
        }
    }
}

static void cleanup_keyboards() {
    for (int i = 0; i < kbd_count; i++) {
        ioctl(kbd_fds[i], EVIOCGRAB, 0);
        close(kbd_fds[i]);
    }
    kbd_count = 0;
}

static void cleanup_graphics() {
    if (mock_mode) {
        if (gtk_main_level() > 0) {
            gtk_main_quit();
        }
    } else {
        if (orig_crtc && drm_fd >= 0) {
            drmModeSetCrtc(drm_fd, orig_crtc->crtc_id, orig_crtc->buffer_id, orig_crtc->x, orig_crtc->y,
                           &connector_id, 1, &orig_crtc->mode);
            drmModeFreeCrtc(orig_crtc);
            orig_crtc = NULL;
        }
        
        if (map_ptr && map_ptr != MAP_FAILED) {
            munmap(map_ptr, dumb_size);
            map_ptr = NULL;
        }
        
        if (fb_id && drm_fd >= 0) {
            drmModeRmFB(drm_fd, fb_id);
            fb_id = 0;
        }
        
        if (dumb_handle && drm_fd >= 0) {
            struct drm_mode_destroy_dumb dest = { .handle = dumb_handle };
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dest);
            dumb_handle = 0;
        }
        
        if (drm_fd >= 0) {
            close(drm_fd);
            drm_fd = -1;
        }
        
        cleanup_keyboards();
        ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
        printf("\033[?25h");
        fflush(stdout);
    }
    
    cleanup_xkb();
    if (bg_surface) {
        cairo_surface_destroy(bg_surface);
        bg_surface = NULL;
    }
}

static bool attempt_login(bool is_autologin) {
    if (strlen(username) == 0) {
        strcpy(status_message, "Username cannot be empty");
        theme_status_r = 0.953; theme_status_g = 0.545; theme_status_b = 0.659; // Red
        return false;
    }

    strcpy(status_message, "Authenticating...");
    theme_status_r = 0.537; theme_status_g = 0.702; theme_status_b = 0.980; // Blue

    pam_session_t *pam_sess = pam_session_init(mock_mode);
    if (!pam_sess) {
        strcpy(status_message, "PAM init failed");
        theme_status_r = 0.953; theme_status_g = 0.545; theme_status_b = 0.659;
        return false;
    }

    if (!pam_session_authenticate(pam_sess, username, password, is_autologin)) {
        strcpy(status_message, "Incorrect username or password");
        theme_status_r = 0.953; theme_status_g = 0.545; theme_status_b = 0.659;
        memset(password, 0, sizeof(password));
        current_focus = FOCUS_PASSWORD;
        pam_session_free(pam_sess);
        return false;
    }

    const char *tty = ttyname(STDIN_FILENO);
    if (!pam_session_start(pam_sess, tty, sessions[selected_session_idx].type)) {
        strcpy(status_message, "Failed to start PAM session");
        theme_status_r = 0.953; theme_status_g = 0.545; theme_status_b = 0.659;
        pam_session_free(pam_sess);
        return false;
    }

    cleanup_graphics();
    printf("Authentication successful. Launching %s...\n", sessions[selected_session_idx].name);
    launch_session(&sessions[selected_session_idx], username, tty, mock_mode);

    pam_session_close(pam_sess);
    pam_session_free(pam_sess);
    free_sessions(sessions, session_count);
    
    exit(EXIT_SUCCESS);
}

static void handle_keysym(xkb_keysym_t sym, const char *utf8) {
    // If settings panel is open, handle navigation within it
    if (settings_open) {
        if (sym == XKB_KEY_Escape || (sym == XKB_KEY_Return && current_focus == FOCUS_SETTINGS)) {
            // Enter on settings = toggle close; Escape = close
            if (sym == XKB_KEY_Escape) {
                settings_open = false;
            } else {
                settings_open = false;
            }
            return;
        }
        if (sym == XKB_KEY_Up) {
            if (session_count > 0)
                selected_session_idx = (selected_session_idx - 1 + session_count) % session_count;
            return;
        }
        if (sym == XKB_KEY_Down) {
            if (session_count > 0)
                selected_session_idx = (selected_session_idx + 1) % session_count;
            return;
        }
        if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            settings_open = false;
            return;
        }
        return; // Swallow other keys while panel is open
    }

    if (sym == XKB_KEY_Tab || sym == XKB_KEY_Down) {
        current_focus = (current_focus + 1) % FOCUS_COUNT;
    } else if (sym == XKB_KEY_ISO_Left_Tab || sym == XKB_KEY_Up) {
        current_focus = (current_focus - 1 + FOCUS_COUNT) % FOCUS_COUNT;
    } else if (sym == XKB_KEY_Left) {
        if (current_focus == FOCUS_USERNAME) {
            if (system_user_count > 1) {
                selected_user_idx = (selected_user_idx - 1 + system_user_count) % system_user_count;
                strncpy(username, system_users[selected_user_idx], sizeof(username) - 1);
                load_avatar();
            }
        }
    } else if (sym == XKB_KEY_Right) {
        if (current_focus == FOCUS_USERNAME) {
            if (system_user_count > 1) {
                selected_user_idx = (selected_user_idx + 1) % system_user_count;
                strncpy(username, system_users[selected_user_idx], sizeof(username) - 1);
                load_avatar();
            }
        }
    } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        if (current_focus == FOCUS_USERNAME) {
            current_focus = FOCUS_PASSWORD;
        } else if (current_focus == FOCUS_PASSWORD) {
            do_login = true;
            running = false;
        } else if (current_focus == FOCUS_LOGIN) {
            do_login = true;
            running = false;
        } else if (current_focus == FOCUS_REBOOT) {
            do_reboot = true;
            running = false;
        } else if (current_focus == FOCUS_SHUTDOWN) {
            do_shutdown = true;
            running = false;
        } else if (current_focus == FOCUS_SUSPEND) {
            do_suspend = true;
            running = false;
        } else if (current_focus == FOCUS_SETTINGS) {
            settings_open = !settings_open;
        }
    } else if (sym == XKB_KEY_BackSpace) {
        if (current_focus == FOCUS_USERNAME) {
            int len = strlen(username);
            if (len > 0) {
                while (len > 0) {
                    len--;
                    if ((username[len] & 0xC0) != 0x80) {
                        username[len] = '\0';
                        break;
                    }
                }
            }
        } else if (current_focus == FOCUS_PASSWORD) {
            int len = strlen(password);
            if (len > 0) {
                while (len > 0) {
                    len--;
                    if ((password[len] & 0xC0) != 0x80) {
                        password[len] = '\0';
                        break;
                    }
                }
            }
        }
    } else if (sym == XKB_KEY_Escape) {
        if (settings_open) {
            settings_open = false;
        } else if (mock_mode) {
            running = false;
        }
    } else {
        if (utf8 && strlen(utf8) > 0) {
            if (current_focus == FOCUS_USERNAME) {
                size_t len = strlen(username);
                if (len + strlen(utf8) < sizeof(username) - 1) {
                    strcat(username, utf8);
                }
            } else if (current_focus == FOCUS_PASSWORD) {
                size_t len = strlen(password);
                if (len + strlen(utf8) < sizeof(password) - 1) {
                    strcat(password, utf8);
                }
            }
        }
    }
}

// ── Mouse hit-testing ──────────────────────────────────────────────────────
// Returns the FOCUS_* value for whatever UI element is under (mx, my),
// or -1 if nothing interactive is under the cursor.
// Also fills *sess_idx with a session index when clicking inside the popup.
static int hit_test_element(double mx, double my, int width, int height,
                             int *sess_idx) {
    if (sess_idx) *sess_idx = -1;

    double box_w = 300, box_h = 440;
    double box_x = (width  - box_w) / 2.0;
    double box_y = (height - box_h) / 2.0;

    // Avatar / username area — clicking cycles users
    double av_cx = box_x + box_w / 2.0;
    double av_cy = box_y + 155;
    double av_r  = 48.0;   // slightly larger hit area
    double dx = mx - av_cx, dy = my - av_cy;
    if (dx*dx + dy*dy <= av_r*av_r) return FOCUS_USERNAME;

    // Password field
    double field_w = 240, field_h = 38;
    double field_x = box_x + (box_w - field_w) / 2.0;
    double pass_y  = box_y + 233;
    if (mx >= field_x && mx <= field_x + field_w &&
        my >= pass_y  && my <= pass_y  + field_h)
        return FOCUS_PASSWORD;

    // Bottom icon buttons
    double icon_r       = 24.0;   // slightly larger hit area
    double icons_y      = box_y + box_h - 18.0 - 22;
    double icon_spacing = 58.0;
    double icon_center  = box_x + box_w / 2.0;

    // Shutdown
    dx = mx - (icon_center - icon_spacing); dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_SHUTDOWN;
    // Reboot
    dx = mx - icon_center; dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_REBOOT;
    // Suspend
    dx = mx - (icon_center + icon_spacing); dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_SUSPEND;

    // Settings gear button — bottom-right corner of screen
    double gear_r  = 28.0;  // slightly larger hit area
    double gear_cx = (double)width  - 22.0 - 22.0;
    double gear_cy = (double)height - 22.0 - 22.0;
    dx = mx - gear_cx; dy = my - gear_cy;
    if (dx*dx + dy*dy <= gear_r*gear_r) return FOCUS_SETTINGS;

    // Session popup items (only when open) — panel opens upward from gear
    if (settings_open && session_count > 0) {
        // Recompute panel_w using the same 36px padding minimum logic
        double panel_w = 140;
        for (int i = 0; i < session_count; i++) {
            // Rough estimate: 7px per char * name length + 36
            int name_len = (int)strlen(sessions[i].name);
            double needed = name_len * 7.5 + 36.0;
            if (needed > panel_w) panel_w = needed;
        }
        double item_h    = 34;
        double header_h  = 22;
        double panel_h   = item_h * session_count + header_h + 8;
        double panel_x   = gear_cx + 22.0 - panel_w;
        double panel_y   = gear_cy - 22.0 - panel_h - 8;

        if (panel_x < 10)               panel_x = 10;
        if (panel_x + panel_w > width - 10) panel_x = width - panel_w - 10;
        if (panel_y < 10)               panel_y = 10;
        if (panel_y + panel_h > height - 10) panel_y = height - panel_h - 10;

        double sep_y       = panel_y + 6 + 12 + 4; // approx header label height ~12
        double items_start = sep_y + 4;
        for (int i = 0; i < session_count; i++) {
            double iy = items_start + i * item_h;
            if (mx >= panel_x && mx <= panel_x + panel_w &&
                my >= iy       && my <= iy + item_h) {
                if (sess_idx) *sess_idx = i;
                return FOCUS_SETTINGS;
            }
        }
    }

    return -1; // nothing hit
}

// Handle a mouse click at (mx, my)
static void handle_mouse_click(double mx, double my, int width, int height) {
    int sess_idx = -1;
    int elem = hit_test_element(mx, my, width, height, &sess_idx);

    if (elem < 0) return;

    switch (elem) {
    case FOCUS_USERNAME:
        if (settings_open) { settings_open = false; return; }
        // Cycle to next user on click
        if (system_user_count > 1) {
            selected_user_idx = (selected_user_idx + 1) % system_user_count;
            strncpy(username, system_users[selected_user_idx], sizeof(username) - 1);
            load_avatar();
        }
        current_focus = FOCUS_USERNAME;
        break;

    case FOCUS_PASSWORD:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_PASSWORD;
        cursor_visible = true;
        break;

    case FOCUS_SETTINGS:
        if (sess_idx >= 0 && sess_idx < session_count) {
            // Clicked a specific session item
            selected_session_idx = sess_idx;
            settings_open = false;
        } else {
            settings_open = !settings_open;
            current_focus = FOCUS_SETTINGS;
        }
        break;

    case FOCUS_SHUTDOWN:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_SHUTDOWN;
        do_shutdown = true;
        running = false;
        break;

    case FOCUS_REBOOT:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_REBOOT;
        do_reboot = true;
        running = false;
        break;

    case FOCUS_SUSPEND:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_SUSPEND;
        do_suspend = true;
        running = false;
        break;

    default:
        if (settings_open) settings_open = false;
        break;
    }
}

// Handle mouse motion — update hover focus
static void handle_mouse_motion(double mx, double my, int width, int height) {
    mouse_x = mx;
    mouse_y = my;
    mouse_shown = true;
    int elem = hit_test_element(mx, my, width, height, NULL);
    if (elem >= 0 && !settings_open) {
        current_focus = elem;
    }
}

// Draw a software cursor (small arrow) for DRM mode
static void draw_cursor(cairo_t *cr, double x, double y) {
    if (!mouse_shown) return;
    cairo_save(cr);
    // Arrow shape
    cairo_move_to(cr, x,      y);
    cairo_line_to(cr, x,      y + 14);
    cairo_line_to(cr, x + 4,  y + 10);
    cairo_line_to(cr, x + 7,  y + 16);
    cairo_line_to(cr, x + 9,  y + 15);
    cairo_line_to(cr, x + 6,  y + 9);
    cairo_line_to(cr, x + 11, y + 9);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    cairo_restore(cr);
}

// Mouse device init/cleanup for DRM mode
static void init_mice() {
    mouse_count = 0;
    glob_t g;
    if (glob("/dev/input/by-path/*-event-mouse", 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && mouse_count < MAX_MICE; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0) mouse_fds[mouse_count++] = fd;
        }
        globfree(&g);
    }
    // Fallback: scan event devices for relative axis capability
    if (mouse_count == 0) {
        for (int i = 0; i < 16 && mouse_count < MAX_MICE; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/input/event%d", i);
            int fd = open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            unsigned long evbits = 0;
            if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) >= 0 &&
                (evbits & (1ul << EV_REL))) {
                mouse_fds[mouse_count++] = fd;
            } else {
                close(fd);
            }
        }
    }
}

static void cleanup_mice() {
    for (int i = 0; i < mouse_count; i++) close(mouse_fds[i]);
    mouse_count = 0;
}

// GTK Callbacks
static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    draw_interface(cr, alloc.width, alloc.height);
    return FALSE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (xkb_state) {
        xkb_state_update_key(xkb_state, event->hardware_keycode, XKB_KEY_DOWN);
        update_layout_name();
    }

    char utf8[32] = "";
    uint32_t unicode = gdk_keyval_to_unicode(event->keyval);
    if (unicode >= 32 && unicode != 0x7F) {
        if (unicode < 0x80) {
            utf8[0] = unicode;
            utf8[1] = '\0';
        } else if (unicode < 0x800) {
            utf8[0] = 0xC0 | (unicode >> 6);
            utf8[1] = 0x80 | (unicode & 0x3F);
            utf8[2] = '\0';
        } else if (unicode < 0x10000) {
            utf8[0] = 0xE0 | (unicode >> 12);
            utf8[1] = 0x80 | ((unicode >> 6) & 0x3F);
            utf8[2] = 0x80 | (unicode & 0x3F);
            utf8[3] = '\0';
        }
    }

    handle_keysym(event->keyval, utf8);
    cursor_visible = true;

    gtk_widget_queue_draw(drawing_area);

    if (do_login) {
        do_login = false;
        if (!attempt_login(false)) {
            gtk_widget_queue_draw(drawing_area);
        }
    }
    if (do_reboot) {
        cleanup_graphics();
        printf("Rebooting system...\n");
        exit(EXIT_SUCCESS);
    }
    if (do_shutdown) {
        cleanup_graphics();
        printf("Powering off system...\n");
        exit(EXIT_SUCCESS);
    }
    if (do_suspend) {
        cleanup_graphics();
        printf("Suspending system...\n");
        int rc = system("systemctl suspend");
        (void)rc;
        exit(EXIT_SUCCESS);
    }

    return TRUE;
}

static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;
    if (xkb_state) {
        xkb_state_update_key(xkb_state, event->hardware_keycode, XKB_KEY_UP);
        update_layout_name();
    }
    gtk_widget_queue_draw(drawing_area);
    return TRUE;
}

static gboolean blink_cursor(gpointer user_data) {
    (void)user_data;
    cursor_visible = !cursor_visible;
    if (drawing_area) {
        gtk_widget_queue_draw(drawing_area);
    }
    return TRUE;
}

// GTK mouse callbacks
static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    if (event->button == 1) { // left click
        handle_mouse_click(event->x, event->y, alloc.width, alloc.height);
        cursor_visible = true;
        gtk_widget_queue_draw(drawing_area);

        if (do_login) {
            do_login = false;
            if (!attempt_login(false)) gtk_widget_queue_draw(drawing_area);
        }
        if (do_reboot)   { cleanup_graphics(); printf("Rebooting...\n");    exit(EXIT_SUCCESS); }
        if (do_shutdown) { cleanup_graphics(); printf("Powering off...\n"); exit(EXIT_SUCCESS); }
        if (do_suspend)  {
            cleanup_graphics();
            printf("Suspending...\n");
            int rc = system("systemctl suspend");
            (void)rc;
            exit(EXIT_SUCCESS);
        }
        if (!running)    { cleanup_graphics(); exit(EXIT_SUCCESS); }
    }
    return TRUE;
}

static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    handle_mouse_motion(event->x, event->y, alloc.width, alloc.height);
    gtk_widget_queue_draw(drawing_area);
    return TRUE;
}

static void run_gtk_mock(int argc, char **argv) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "vaxp-dm Mock Screen");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    drawing_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window), drawing_area);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(drawing_area, "draw", G_CALLBACK(on_draw_event), NULL);
    g_signal_connect(window, "key-press-event",   G_CALLBACK(on_key_press),    NULL);
    g_signal_connect(window, "key-release-event", G_CALLBACK(on_key_release),  NULL);
    g_signal_connect(drawing_area, "button-press-event",  G_CALLBACK(on_button_press),  NULL);
    g_signal_connect(drawing_area, "motion-notify-event", G_CALLBACK(on_motion_notify), NULL);

    // Enable mouse events on the drawing area
    gtk_widget_add_events(drawing_area,
        GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);

    g_timeout_add(500, blink_cursor, NULL);

    load_background();
    load_avatar();
    init_xkb();

    gtk_widget_show_all(window);

    if (autologin_enabled) {
        if (!attempt_login(true)) {
            autologin_enabled = false;
        }
    }

    gtk_main();

    cleanup_graphics();
}

static void run_drm_kms() {
    tcgetattr(STDIN_FILENO, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    printf("\033[?25l");
    fflush(stdout);

    drmModeRes *res = NULL;
    drmModeConnector *conn = NULL;

    // Retry opening the DRM device and finding a connected connector (up to 50 times, 10 seconds total)
    // in case the graphics driver is loading asynchronously during boot
    for (int retry = 0; retry < 50; retry++) {
        for (int i = 0; i < 5; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/card%d", i);
            drm_fd = open(path, O_RDWR | O_CLOEXEC);
            if (drm_fd >= 0) {
                res = drmModeGetResources(drm_fd);
                if (res) {
                    for (int j = 0; j < res->count_connectors; j++) {
                        conn = drmModeGetConnector(drm_fd, res->connectors[j]);
                        if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
                            break;
                        }
                        if (conn) {
                            drmModeFreeConnector(conn);
                            conn = NULL;
                        }
                    }
                    if (conn) {
                        break; // Found a working DRM device and connector
                    }
                    drmModeFreeResources(res);
                    res = NULL;
                }
                close(drm_fd);
                drm_fd = -1;
            }
        }
        if (drm_fd >= 0) {
            break;
        }
        usleep(200000); // Wait 200ms
    }

    if (drm_fd < 0 || !res || !conn) {
        fprintf(stderr, "Failed to open DRM device or no connected connector found.\n");
        if (res) drmModeFreeResources(res);
        if (conn) drmModeFreeConnector(conn);
        if (drm_fd >= 0) close(drm_fd);
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
        printf("\033[?25h");
        exit(EXIT_FAILURE);
    }

    connector_id = conn->connector_id;
    drmModeModeInfo mode = conn->modes[0];
    int screen_w = mode.hdisplay;
    int screen_h = mode.vdisplay;

    screen_w_g = screen_w;
    screen_h_g = screen_h;

    uint32_t crtc_id = 0;
    drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoder_id);
    if (enc) {
        crtc_id = enc->crtc_id;
        drmModeFreeEncoder(enc);
    }
    if (crtc_id == 0) {
        for (int i = 0; i < conn->count_encoders; i++) {
            drmModeEncoder *e = drmModeGetEncoder(drm_fd, conn->encoders[i]);
            if (e) {
                for (int j = 0; j < res->count_crtcs; j++) {
                    if (e->possible_crtcs & (1 << j)) {
                        crtc_id = res->crtcs[j];
                        break;
                    }
                }
                drmModeFreeEncoder(e);
            }
            if (crtc_id != 0) break;
        }
    }

    if (crtc_id == 0) {
        fprintf(stderr, "No suitable CRTC found.\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        close(drm_fd);
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
        printf("\033[?25h");
        exit(EXIT_FAILURE);
    }

    struct drm_mode_create_dumb creq = {
        .width = screen_w,
        .height = screen_h,
        .bpp = 32,
        .flags = 0
    };
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB failed");
        exit(EXIT_FAILURE);
    }
    dumb_handle = creq.handle;
    dumb_size = creq.size;

    if (drmModeAddFB(drm_fd, screen_w, screen_h, 24, 32, creq.pitch, creq.handle, &fb_id) < 0) {
        perror("drmModeAddFB failed");
        exit(EXIT_FAILURE);
    }

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB failed");
        exit(EXIT_FAILURE);
    }

    map_ptr = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mreq.offset);
    if (map_ptr == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    orig_crtc = drmModeGetCrtc(drm_fd, crtc_id);

    if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &connector_id, 1, &mode) < 0) {
        perror("drmModeSetCrtc failed");
        exit(EXIT_FAILURE);
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    cairo_surface_t *dumb_surf = cairo_image_surface_create_for_data(
        map_ptr, CAIRO_FORMAT_ARGB32, screen_w, screen_h, creq.pitch
    );
    cairo_surface_t *offscreen = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, screen_w, screen_h);

    load_background();
    load_avatar();
    init_xkb();
    init_keyboards();
    init_mice();
    mouse_x = screen_w_g / 2.0;
    mouse_y = screen_h_g / 2.0;
    ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS);

    if (autologin_enabled) {
        if (!attempt_login(true)) {
            autologin_enabled = false;
        }
    }

    struct pollfd fds[MAX_KEYBOARDS + MAX_MICE];
    int timeout = 500;

    while (running) {
        // Draw UI
        cairo_t *cr_off = cairo_create(offscreen);
        draw_interface(cr_off, screen_w_g, screen_h_g);
        // Draw software cursor on top
        draw_cursor(cr_off, mouse_x, mouse_y);
        cairo_destroy(cr_off);

        cairo_t *cr_dumb = cairo_create(dumb_surf);
        cairo_set_source_surface(cr_dumb, offscreen, 0, 0);
        cairo_paint(cr_dumb);
        cairo_destroy(cr_dumb);

        drmModeDirtyFB(drm_fd, fb_id, NULL, 0);

        // Build poll fd list: keyboards + mice
        int total_fds = 0;
        for (int i = 0; i < kbd_count;   i++) { fds[total_fds].fd = kbd_fds[i];   fds[total_fds++].events = POLLIN; }
        for (int i = 0; i < mouse_count; i++) { fds[total_fds].fd = mouse_fds[i]; fds[total_fds++].events = POLLIN; }

        int ret = poll(fds, total_fds, timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (ret == 0) {
            cursor_visible = !cursor_visible;
            continue;
        }

        // Keyboard events
        for (int i = 0; i < kbd_count; i++) {
            if (fds[i].revents & POLLIN) {
                struct input_event ev;
                while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                    if (ev.type == EV_KEY && (ev.value == 1 || ev.value == 2)) {
                        if (xkb_state) {
                            xkb_state_update_key(xkb_state, ev.code + 8, XKB_KEY_DOWN);
                            update_layout_name();
                            
                            xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, ev.code + 8);
                            char utf8_buf[32] = "";
                            xkb_state_key_get_utf8(xkb_state, ev.code + 8, utf8_buf, sizeof(utf8_buf));
                            
                            handle_keysym(sym, utf8_buf);
                            cursor_visible = true;
                        }
                    } else if (ev.type == EV_KEY && ev.value == 0) {
                        if (xkb_state) {
                            xkb_state_update_key(xkb_state, ev.code + 8, XKB_KEY_UP);
                            update_layout_name();
                        }
                    }
                }
            }
        }

        // Mouse events
        for (int i = 0; i < mouse_count; i++) {
            int fi = kbd_count + i;
            if (fds[fi].revents & POLLIN) {
                struct input_event ev;
                int dx = 0, dy = 0;
                bool btn_left = false;
                while (read(mouse_fds[i], &ev, sizeof(ev)) > 0) {
                    if (ev.type == EV_REL) {
                        if (ev.code == REL_X) dx += ev.value;
                        if (ev.code == REL_Y) dy += ev.value;
                    } else if (ev.type == EV_KEY && ev.code == BTN_LEFT && ev.value == 1) {
                        btn_left = true;
                    }
                }
                if (dx || dy) {
                    mouse_x = mouse_x + dx;
                    mouse_y = mouse_y + dy;
                    if (mouse_x < 0)           mouse_x = 0;
                    if (mouse_x > screen_w_g - 1) mouse_x = screen_w_g - 1;
                    if (mouse_y < 0)           mouse_y = 0;
                    if (mouse_y > screen_h_g - 1) mouse_y = screen_h_g - 1;
                    mouse_shown = true;
                    handle_mouse_motion(mouse_x, mouse_y, screen_w_g, screen_h_g);
                }
                if (btn_left) {
                    handle_mouse_click(mouse_x, mouse_y, screen_w_g, screen_h_g);
                }
            }
        }
        if (do_login) {
            do_login = false;
            attempt_login(false);
        }
        if (do_reboot) {
            cleanup_graphics();
            printf("Rebooting system...\n");
            int rc = system("reboot");
            (void)rc;
            exit(EXIT_SUCCESS);
        }
        if (do_shutdown) {
            cleanup_graphics();
            printf("Powering off system...\n");
            int rc = system("poweroff");
            (void)rc;
            exit(EXIT_SUCCESS);
        }
        if (do_suspend) {
            cleanup_graphics();
            printf("Suspending system...\n");
            int rc = system("systemctl suspend");
            (void)rc;
            exit(EXIT_SUCCESS);
        }
    }

    cairo_surface_destroy(dumb_surf);
    cairo_surface_destroy(offscreen);
    cleanup_mice();
    cleanup_graphics();
}

int main(int argc, char **argv) {
    mock_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mock") == 0) {
            mock_mode = true;
        }
    }

    if (!mock_mode && getuid() != 0) {
        fprintf(stderr, "Warning: Running as non-root. DRM and sessions won't work correctly.\n");
        fprintf(stderr, "Automatically switching to --mock mode.\n\n");
        sleep(2);
        mock_mode = true;
    }

    sessions = discover_sessions(&session_count);

    char def_session[64] = "";
    read_config_string("default_session", def_session, sizeof(def_session), "");
    if (strlen(def_session) > 0 && session_count > 0) {
        for (int i = 0; i < session_count; i++) {
            if (strcasecmp(sessions[i].name, def_session) == 0 ||
                strcasecmp(sessions[i].exec, def_session) == 0) {
                selected_session_idx = i;
                break;
            }
        }
    }

    // Load Theme Colors
    char hex_title[32] = "", hex_focus[32] = "", hex_inactive[32] = "", hex_status[32] = "";
    read_config_string("color_title", hex_title, sizeof(hex_title), "#cba6f7");
    read_config_string("color_focus", hex_focus, sizeof(hex_focus), "#89b4fa");
    read_config_string("color_inactive", hex_inactive, sizeof(hex_inactive), "#7f8c8d");
    read_config_string("color_status", hex_status, sizeof(hex_status), "#f9e2af");

    parse_hex_color(hex_title, &theme_title_r, &theme_title_g, &theme_title_b);
    parse_hex_color(hex_focus, &theme_focus_r, &theme_focus_g, &theme_focus_b);
    parse_hex_color(hex_inactive, &theme_inactive_r, &theme_inactive_g, &theme_inactive_b);
    parse_hex_color(hex_status, &theme_status_r, &theme_status_g, &theme_status_b);

    discover_users();
    
    char autologin_val[32] = "";
    read_config_string("autologin", autologin_val, sizeof(autologin_val), "false");
    autologin_enabled = (strcmp(autologin_val, "true") == 0);
    
    // Auto-select user matching config or first discovered
    char conf_user[32] = "";
    read_config_string("autologin_user", conf_user, sizeof(conf_user), "");
    if (strlen(conf_user) > 0) {
        strncpy(username, conf_user, sizeof(username) - 1);
        for (int i = 0; i < system_user_count; i++) {
            if (strcmp(system_users[i], conf_user) == 0) {
                selected_user_idx = i;
                break;
            }
        }
    } else if (system_user_count > 0) {
        strncpy(username, system_users[0], sizeof(username) - 1);
        selected_user_idx = 0;
    }
    
    if (strlen(username) > 0) {
        current_focus = FOCUS_PASSWORD;
    } else {
        current_focus = FOCUS_USERNAME;
    }

    if (mock_mode) {
        run_gtk_mock(argc, argv);
    } else {
        run_drm_kms();
    }

    free_sessions(sessions, session_count);
    return EXIT_SUCCESS;
}
