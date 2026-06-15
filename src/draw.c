/* draw.c — جميع دوال الرسم بـ Cairo/Pango */

#include "draw.h"
#include "globals.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ─── draw_rounded_rect ─────────────────────────────────────────────────── */
void draw_rounded_rect(cairo_t *cr, double x, double y,
                       double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI_2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r,  0,      M_PI_2);
    cairo_arc(cr, x + r,     y + h - r, r,  M_PI_2, M_PI);
    cairo_arc(cr, x + r,     y + r,     r,  M_PI,  -M_PI_2);
    cairo_close_path(cr);
}

/* ─── draw_glow ─────────────────────────────────────────────────────────── */
void draw_glow(cairo_t *cr, double x, double y,
               double w, double h, double r,
               double max_alpha, double size)
{
    cairo_save(cr);
    int steps = 8;
    for (int i = 0; i < steps; i++) {
        double offset = size * ((double)i / steps);
        double alpha  = max_alpha * (1.0 - (double)i / steps);
        cairo_set_source_rgba(cr,
            theme_focus_r, theme_focus_g, theme_focus_b, alpha);
        draw_rounded_rect(cr,
            x - offset, y - offset,
            w + offset * 2, h + offset * 2,
            r + offset);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

/* ─── draw_avatar ───────────────────────────────────────────────────────── */
void draw_avatar(cairo_t *cr, double cx, double cy, double radius)
{
    cairo_save(cr);
    /* Clip to circle */
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
        /* Fallback: gradient fill + person silhouette */
        cairo_pattern_t *grad = cairo_pattern_create_radial(
            cx, cy - radius * 0.2, radius * 0.1,
            cx, cy, radius);
        cairo_pattern_add_color_stop_rgb(grad, 0, 0.30, 0.25, 0.45);
        cairo_pattern_add_color_stop_rgb(grad, 1, 0.12, 0.10, 0.22);
        cairo_set_source(cr, grad);
        cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(grad);

        /* Head */
        double hr = radius * 0.32;
        cairo_arc(cr, cx, cy - radius * 0.18, hr, 0, 2 * M_PI);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.85);
        cairo_fill(cr);

        /* Body (ellipse) */
        cairo_save(cr);
        cairo_translate(cr, cx, cy + radius * 0.45);
        cairo_scale(cr, 1.0, 0.5);
        cairo_arc(cr, 0, 0, radius * 0.54, 0, 2 * M_PI);
        cairo_restore(cr);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.85);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    /* Outer ring */
    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
    cairo_set_source_rgba(cr,
        theme_title_r, theme_title_g, theme_title_b, 0.55);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);
}

/* ─── draw_icon_button ──────────────────────────────────────────────────── */
void draw_icon_button(cairo_t *cr, double cx, double cy, double r,
                      bool focused,
                      double fr, double fg, double fb,
                      const char *symbol)
{
    cairo_save(cr);

    /* Background circle */
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    if (focused) {
        /* Glow rings */
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

    /* Border */
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    if (focused) {
        cairo_set_source_rgba(cr, fr, fg, fb, 0.9);
        cairo_set_line_width(cr, 1.8);
    } else {
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.25);
        cairo_set_line_width(cr, 1.2);
    }
    cairo_stroke(cr);

    /* Symbol text */
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_text(layout, symbol, -1);
    PangoFontDescription *desc =
        pango_font_description_from_string("Sans 13");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    int tw, th;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, cx - tw / 2.0, cy - th / 2.0);
    if (focused)
        cairo_set_source_rgba(cr, fr, fg, fb, 1.0);
    else
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.75);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    cairo_restore(cr);
}

/* ─── draw_interface ────────────────────────────────────────────────────── */
void draw_interface(cairo_t *cr, int width, int height)
{
    /* 1. Background */
    if (bg_surface) {
        int img_w = cairo_image_surface_get_width(bg_surface);
        int img_h = cairo_image_surface_get_height(bg_surface);
        cairo_save(cr);
        double scale_x = (double)width  / img_w;
        double scale_y = (double)height / img_h;
        double scale   = (scale_x > scale_y) ? scale_x : scale_y;
        double offset_x = (width  - img_w * scale) / 2.0;
        double offset_y = (height - img_h * scale) / 2.0;
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, bg_surface, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
    } else {
        cairo_save(cr);
        cairo_pattern_t *pat =
            cairo_pattern_create_linear(0, 0, width, height);
        cairo_pattern_add_color_stop_rgb(pat, 0, 0.15, 0.10, 0.25);
        cairo_pattern_add_color_stop_rgb(pat, 1, 0.05, 0.05, 0.14);
        cairo_set_source(cr, pat);
        cairo_paint(cr);
        cairo_pattern_destroy(pat);
        cairo_restore(cr);
    }

    /* 2. Glassmorphic login card */
    double box_w = 300;
    double box_h = 440;
    double box_x = (width  - box_w) / 2.0;
    double box_y = (height - box_h) / 2.0;
    double box_r = 22.0;

    /* Drop Shadow */
    cairo_save(cr);
    for (int i = 12; i >= 0; i--) {
        double offset = 2.0 + (double)i * 2.0;
        double alpha  = 0.045 * (1.0 - (double)i / 13.0);
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, alpha);
        draw_rounded_rect(cr,
            box_x - offset, box_y - offset + 4,
            box_w + offset * 2, box_h + offset * 2,
            box_r + offset);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    /* Box body */
    cairo_save(cr);
    draw_rounded_rect(cr, box_x, box_y, box_w, box_h, box_r);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_fill_preserve(cr);
    cairo_pattern_t *top_shine =
        cairo_pattern_create_linear(box_x, box_y,
                                    box_x, box_y + box_h * 0.4);
    cairo_pattern_add_color_stop_rgba(top_shine, 0, 1, 1, 1, 0.06);
    cairo_pattern_add_color_stop_rgba(top_shine, 1, 1, 1, 1, 0.0);
    cairo_set_source(cr, top_shine);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(top_shine);
    cairo_set_source_rgba(cr,
        theme_title_r, theme_title_g, theme_title_b, 0.22);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    cairo_restore(cr);

    /* 3. Date / Time at top of card */
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char day_str[32], date_str[32], time_str[16];

        const char *days[] = {
            "Sunday","Monday","Tuesday","Wednesday",
            "Thursday","Friday","Saturday"
        };
        snprintf(day_str, sizeof(day_str), "%s", days[tm_info->tm_wday]);

        const char *months[] = {
            "January","February","March","April","May","June",
            "July","August","September","October","November","December"
        };
        snprintf(date_str, sizeof(date_str),
                 "%d %s", tm_info->tm_mday, months[tm_info->tm_mon]);
        snprintf(time_str, sizeof(time_str),
                 "- %02d:%02d -", tm_info->tm_hour, tm_info->tm_min);

        cairo_save(cr);

        /* Day name — large bold */
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, day_str, -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans Bold 22");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, box_y + 20);
        cairo_set_source_rgb(cr, 0.93, 0.93, 0.97);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        /* Date */
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

        /* Time */
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

    /* 4. User avatar circle */
    double av_cx = box_x + box_w / 2.0;
    double av_cy = box_y + 155;
    double av_r  = 38.0;
    draw_avatar(cr, av_cx, av_cy, av_r);

    /* Username below avatar */
    {
        cairo_save(cr);
        PangoLayout *layout = pango_cairo_create_layout(cr);
        char user_display[64];
        if (current_focus == FOCUS_USERNAME && system_user_count > 1)
            snprintf(user_display, sizeof(user_display), "< %s >", username);
        else if (strlen(username) > 0)
            snprintf(user_display, sizeof(user_display), "%s", username);
        else
            snprintf(user_display, sizeof(user_display), "user");

        pango_layout_set_text(layout, user_display, -1);
        PangoFontDescription *desc;
        if (current_focus == FOCUS_USERNAME) {
            desc = pango_font_description_from_string("Sans Bold 13");
            cairo_set_source_rgb(cr,
                theme_focus_r, theme_focus_g, theme_focus_b);
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

    /* 5. Password field */
    double field_w = 240;
    double field_h = 38;
    double field_x = box_x + (box_w - field_w) / 2.0;
    double pass_y  = box_y + 233;

    cairo_save(cr);
    if (current_focus == FOCUS_PASSWORD)
        draw_glow(cr, field_x, pass_y, field_w, field_h, 10.0, 0.18, 7.0);

    draw_rounded_rect(cr, field_x, pass_y, field_w, field_h, 10.0);
    cairo_set_source_rgba(cr, 0.05, 0.04, 0.10, 0.65);
    cairo_fill_preserve(cr);
    if (current_focus == FOCUS_PASSWORD) {
        cairo_set_source_rgba(cr,
            theme_focus_r, theme_focus_g, theme_focus_b, 0.85);
        cairo_set_line_width(cr, 1.8);
    } else {
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.20);
        cairo_set_line_width(cr, 1.0);
    }
    cairo_stroke(cr);

    {
        /* Lock icon placeholder */
        PangoLayout *layout = pango_cairo_create_layout(cr);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.45);
        pango_layout_set_text(layout, "●", -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, field_x + 12,
                      pass_y + (field_h - th) / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        /* Password text / placeholder */
        char masked[128] = "";
        int pw_len = strlen(password);
        for (int i = 0; i < pw_len && i < 32; i++)
            strcat(masked, "•");

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
        cairo_move_to(cr, field_x + 30,
                      pass_y + (field_h - text_h) / 2.0);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        /* Cursor */
        if (current_focus == FOCUS_PASSWORD && cursor_visible) {
            cairo_set_source_rgba(cr,
                theme_focus_r, theme_focus_g, theme_focus_b, 0.9);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, field_x + 30 + text_w + 2, pass_y + 8);
            cairo_line_to(cr, field_x + 30 + text_w + 2,
                          pass_y + field_h - 8);
            cairo_stroke(cr);
        }
    }
    cairo_restore(cr);

    /* 6. Bottom icon buttons (Shutdown / Reboot / Suspend) */
    double icon_r       = 18.0;
    double icons_y      = box_y + box_h - icon_r - 22;
    double icon_spacing = 58.0;
    double icon_cx_center = box_x + box_w / 2.0;

    draw_icon_button(cr, icon_cx_center - icon_spacing, icons_y, icon_r,
                     current_focus == FOCUS_SHUTDOWN,
                     0.953, 0.545, 0.659, "⏻");
    draw_icon_button(cr, icon_cx_center, icons_y, icon_r,
                     current_focus == FOCUS_REBOOT,
                     0.976, 0.886, 0.686, "↺");
    draw_icon_button(cr, icon_cx_center + icon_spacing, icons_y, icon_r,
                     current_focus == FOCUS_SUSPEND,
                     0.537, 0.702, 0.980, "☽");

    /* 7. Status message + selected session name */
    {
        cairo_save(cr);
        double text_base_y = icons_y - icon_r - 8;

        /* Welcome / status line */
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, status_message, -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans 10");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, box_x + (box_w - tw) / 2.0, text_base_y - th);
        cairo_set_source_rgba(cr,
            theme_status_r, theme_status_g, theme_status_b, 0.85);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        /* Selected session name */
        if (session_count > 0) {
            char sess_label[128];
            snprintf(sess_label, sizeof(sess_label),
                     "%s", sessions[selected_session_idx].name);
            layout = pango_cairo_create_layout(cr);
            pango_layout_set_text(layout, sess_label, -1);
            desc = pango_font_description_from_string("Sans Bold 9");
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr,
                box_x + (box_w - tw) / 2.0,
                text_base_y - th - 18);
            cairo_set_source_rgba(cr,
                theme_title_r, theme_title_g, theme_title_b, 0.80);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }
        cairo_restore(cr);
    }

    /* 8. Keyboard layout tag (top-left of card) */
    {
        cairo_save(cr);
        double tag_w = 30, tag_h = 18;
        double tag_x = box_x + 12, tag_y = box_y + 12;
        draw_rounded_rect(cr, tag_x, tag_y, tag_w, tag_h, 4.0);
        cairo_set_source_rgba(cr, 0.15, 0.13, 0.26, 0.80);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,
            theme_focus_r, theme_focus_g, theme_focus_b, 0.5);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);

        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, active_layout, -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans Bold 9");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr,
            tag_x + (tag_w - tw) / 2.0,
            tag_y + (tag_h - th) / 2.0);
        cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.9);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    /* 9. Settings gear button — bottom-right corner */
    double gear_r  = 22.0;
    double gear_cx = width  - gear_r - 20;
    double gear_cy = height - gear_r - 20;
    {
        cairo_save(cr);
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            for (int i = 6; i >= 0; i--) {
                double off = (double)i * 1.5;
                cairo_arc(cr, gear_cx, gear_cy, gear_r + off, 0, 2 * M_PI);
                cairo_set_source_rgba(cr,
                    theme_title_r, theme_title_g, theme_title_b,
                    0.03 * (7 - i));
                cairo_fill(cr);
            }
        }
        cairo_arc(cr, gear_cx, gear_cy, gear_r, 0, 2 * M_PI);
        if (current_focus == FOCUS_SETTINGS || settings_open)
            cairo_set_source_rgba(cr,
                theme_title_r, theme_title_g, theme_title_b, 0.28);
        else
            cairo_set_source_rgba(cr, 0.08, 0.07, 0.16, 0.80);
        cairo_fill(cr);

        cairo_arc(cr, gear_cx, gear_cy, gear_r, 0, 2 * M_PI);
        if (current_focus == FOCUS_SETTINGS || settings_open) {
            cairo_set_source_rgba(cr,
                theme_title_r, theme_title_g, theme_title_b, 0.85);
            cairo_set_line_width(cr, 1.8);
        } else {
            cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.22);
            cairo_set_line_width(cr, 1.2);
        }
        cairo_stroke(cr);

        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, "⚙", -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans 15");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, gear_cx - tw / 2.0, gear_cy - th / 2.0);
        if (current_focus == FOCUS_SETTINGS || settings_open)
            cairo_set_source_rgba(cr,
                theme_title_r, theme_title_g, theme_title_b, 1.0);
        else
            cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.55);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);
        cairo_restore(cr);
    }

    /* 10. Session popup panel */
    if (settings_open) {
        int visible_sessions = session_count > 0 ? session_count : 1;

        /* Compute minimum panel width from longest session name */
        double panel_w = 140;
        {
            cairo_save(cr);
            PangoLayout *tmp = pango_cairo_create_layout(cr);
            PangoFontDescription *fdesc =
                pango_font_description_from_string("Sans Bold 11");
            pango_layout_set_font_description(tmp, fdesc);
            pango_font_description_free(fdesc);
            for (int i = 0; i < visible_sessions; i++) {
                const char *sn = (session_count > 0)
                                 ? sessions[i].name : "No Sessions";
                pango_layout_set_text(tmp, sn, -1);
                int sw, sh; (void)sh;
                pango_layout_get_pixel_size(tmp, &sw, &sh);
                double needed = sw + 36.0;
                if (needed > panel_w) panel_w = needed;
            }
            g_object_unref(tmp);
            cairo_restore(cr);
        }

        double item_h        = 34;
        double header_h_approx = 22;
        double panel_h = item_h * visible_sessions + header_h_approx + 8;

        double panel_x = gear_cx + gear_r - panel_w;
        double panel_y = gear_cy - gear_r - panel_h - 8;

        /* Clamp to screen */
        if (panel_x < 10)               panel_x = 10;
        if (panel_x + panel_w > width - 10) panel_x = width - panel_w - 10;
        if (panel_y < 10)               panel_y = 10;
        if (panel_y + panel_h > height - 10) panel_y = height - panel_h - 10;

        cairo_save(cr);

        /* Panel shadow */
        for (int i = 10; i >= 0; i--) {
            double off = (double)i * 1.5;
            cairo_set_source_rgba(cr, 0, 0, 0, 0.05);
            draw_rounded_rect(cr,
                panel_x - off, panel_y - off,
                panel_w + off * 2, panel_h + off * 2,
                14 + off);
            cairo_fill(cr);
        }

        /* Panel body */
        draw_rounded_rect(cr, panel_x, panel_y, panel_w, panel_h, 14);
        cairo_set_source_rgba(cr, 0.09, 0.08, 0.18, 0.95);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr,
            theme_title_r, theme_title_g, theme_title_b, 0.28);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);

        /* Header label */
        PangoLayout *layout = pango_cairo_create_layout(cr);
        pango_layout_set_text(layout, "Session", -1);
        PangoFontDescription *desc =
            pango_font_description_from_string("Sans Bold 9");
        pango_layout_set_font_description(layout, desc);
        pango_font_description_free(desc);
        int tw, th;
        pango_layout_get_pixel_size(layout, &tw, &th);
        cairo_move_to(cr, panel_x + (panel_w - tw) / 2.0, panel_y + 6);
        cairo_set_source_rgba(cr,
            theme_title_r, theme_title_g, theme_title_b, 0.60);
        pango_cairo_show_layout(cr, layout);
        g_object_unref(layout);

        /* Separator under header */
        double sep_y = panel_y + 6 + th + 4;
        cairo_set_source_rgba(cr,
            theme_title_r, theme_title_g, theme_title_b, 0.15);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, panel_x + 10, sep_y);
        cairo_line_to(cr, panel_x + panel_w - 10, sep_y);
        cairo_stroke(cr);

        /* Session items */
        double items_start_y = sep_y + 4;
        for (int i = 0; i < visible_sessions; i++) {
            double iy         = items_start_y + i * item_h;
            bool   is_selected = (i == selected_session_idx);

            if (is_selected) {
                draw_rounded_rect(cr,
                    panel_x + 6, iy + 3,
                    panel_w - 12, item_h - 6, 8);
                cairo_set_source_rgba(cr,
                    theme_title_r, theme_title_g, theme_title_b, 0.20);
                cairo_fill(cr);
            }

            layout = pango_cairo_create_layout(cr);
            const char *sname = (session_count > 0)
                                ? sessions[i].name : "No Sessions";
            pango_layout_set_text(layout, sname, -1);
            desc = pango_font_description_from_string(
                is_selected ? "Sans Bold 11" : "Sans 11");
            pango_layout_set_font_description(layout, desc);
            pango_font_description_free(desc);
            pango_layout_get_pixel_size(layout, &tw, &th);
            cairo_move_to(cr,
                panel_x + (panel_w - tw) / 2.0,
                iy + (item_h - th) / 2.0);
            if (is_selected)
                cairo_set_source_rgba(cr,
                    theme_title_r, theme_title_g, theme_title_b, 1.0);
            else
                cairo_set_source_rgba(cr, 0.804, 0.839, 0.957, 0.75);
            pango_cairo_show_layout(cr, layout);
            g_object_unref(layout);
        }
        cairo_restore(cr);
    }
}

/* ─── draw_cursor ───────────────────────────────────────────────────────── */
void draw_cursor(cairo_t *cr, double x, double y)
{
    if (!mouse_shown) return;
    cairo_save(cr);
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
