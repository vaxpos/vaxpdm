/* gtk_mode.c — تشغيل الواجهة في وضع GTK Mock */

#include "gtk_mode.h"
#include "globals.h"
#include "draw.h"
#include "input.h"
#include "login.h"
#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <xkbcommon/xkbcommon.h>
#include <stdio.h>
#include <stdlib.h>

/* ─── GTK draw callback ─────────────────────────────────────────────────── */
static gboolean on_draw_event(GtkWidget *widget, cairo_t *cr,
                               gpointer user_data)
{
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    draw_interface(cr, alloc.width, alloc.height);
    return FALSE;
}

/* ─── GTK key-press callback ────────────────────────────────────────────── */
static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                              gpointer user_data)
{
    (void)widget;
    (void)user_data;

    if (xkb_state) {
        xkb_state_update_key(xkb_state,
                             event->hardware_keycode, XKB_KEY_DOWN);
        update_layout_name();
    }

    /* Build UTF-8 string from GDK key value */
    char utf8[32] = "";
    uint32_t unicode = gdk_keyval_to_unicode(event->keyval);
    if (unicode >= 32 && unicode != 0x7F) {
        if (unicode < 0x80) {
            utf8[0] = (char)unicode;
            utf8[1] = '\0';
        } else if (unicode < 0x800) {
            utf8[0] = (char)(0xC0 | (unicode >> 6));
            utf8[1] = (char)(0x80 | (unicode & 0x3F));
            utf8[2] = '\0';
        } else if (unicode < 0x10000) {
            utf8[0] = (char)(0xE0 | (unicode >> 12));
            utf8[1] = (char)(0x80 | ((unicode >> 6) & 0x3F));
            utf8[2] = (char)(0x80 | (unicode & 0x3F));
            utf8[3] = '\0';
        }
    }

    handle_keysym(event->keyval, utf8);
    cursor_visible = true;
    gtk_widget_queue_draw(drawing_area);

    if (do_login) {
        do_login = false;
        if (!attempt_login(false))
            gtk_widget_queue_draw(drawing_area);
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

/* ─── GTK key-release callback ──────────────────────────────────────────── */
static gboolean on_key_release(GtkWidget *widget, GdkEventKey *event,
                                gpointer user_data)
{
    (void)widget;
    (void)user_data;
    if (xkb_state) {
        xkb_state_update_key(xkb_state,
                             event->hardware_keycode, XKB_KEY_UP);
        update_layout_name();
    }
    gtk_widget_queue_draw(drawing_area);
    return TRUE;
}

/* ─── Cursor blink timer ────────────────────────────────────────────────── */
static gboolean blink_cursor(gpointer user_data)
{
    (void)user_data;
    cursor_visible = !cursor_visible;
    if (drawing_area)
        gtk_widget_queue_draw(drawing_area);
    return TRUE;
}

/* ─── GTK mouse button callback ─────────────────────────────────────────── */
static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                 gpointer user_data)
{
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    if (event->button == 1) {
        handle_mouse_click(event->x, event->y,
                           alloc.width, alloc.height);
        cursor_visible = true;
        gtk_widget_queue_draw(drawing_area);

        if (do_login) {
            do_login = false;
            if (!attempt_login(false))
                gtk_widget_queue_draw(drawing_area);
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
        if (!running) { cleanup_graphics(); exit(EXIT_SUCCESS); }
    }
    return TRUE;
}

/* ─── GTK mouse motion callback ─────────────────────────────────────────── */
static gboolean on_motion_notify(GtkWidget *widget, GdkEventMotion *event,
                                  gpointer user_data)
{
    (void)user_data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    handle_mouse_motion(event->x, event->y, alloc.width, alloc.height);
    gtk_widget_queue_draw(drawing_area);
    return TRUE;
}

/* ─── run_gtk_mock ──────────────────────────────────────────────────────── */
void run_gtk_mock(int argc, char **argv)
{
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "vaxp-dm Mock Screen");
    gtk_window_set_default_size(GTK_WINDOW(window), 1024, 768);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    drawing_area = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(window), drawing_area);

    g_signal_connect(window, "destroy",
                     G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(drawing_area, "draw",
                     G_CALLBACK(on_draw_event), NULL);
    g_signal_connect(window, "key-press-event",
                     G_CALLBACK(on_key_press), NULL);
    g_signal_connect(window, "key-release-event",
                     G_CALLBACK(on_key_release), NULL);
    g_signal_connect(drawing_area, "button-press-event",
                     G_CALLBACK(on_button_press), NULL);
    g_signal_connect(drawing_area, "motion-notify-event",
                     G_CALLBACK(on_motion_notify), NULL);

    gtk_widget_add_events(drawing_area,
        GDK_BUTTON_PRESS_MASK |
        GDK_POINTER_MOTION_MASK |
        GDK_POINTER_MOTION_HINT_MASK);

    g_timeout_add(500, blink_cursor, NULL);

    load_background();
    load_avatar();
    init_xkb();

    gtk_widget_show_all(window);

    if (autologin_enabled) {
        if (!attempt_login(true))
            autologin_enabled = false;
    }

    gtk_main();
    cleanup_graphics();
}
