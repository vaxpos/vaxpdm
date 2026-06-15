#ifndef GLOBALS_H
#define GLOBALS_H

#include <gtk/gtk.h>
#include <cairo.h>
#include <xkbcommon/xkbcommon.h>
#include <stdbool.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "session.h"

/* ─── Focus / UI Constants ─────────────────────────────────────────────── */
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

/* ─── UI State ──────────────────────────────────────────────────────────── */
extern int   current_focus;
extern char  username[32];
extern char  password[32];
extern int   selected_session_idx;
extern int   session_count;
extern session_t *sessions;
extern bool  mock_mode;
extern bool  autologin_enabled;
extern bool  tty_switching;
extern int   target_vt;
extern char  status_message[64];
extern char  active_layout[8];

/* ─── User discovery ────────────────────────────────────────────────────── */
extern char system_users[16][32];
extern int  system_user_count;
extern int  selected_user_idx;

/* ─── Theme Colors ──────────────────────────────────────────────────────── */
extern double theme_title_r,    theme_title_g,    theme_title_b;
extern double theme_focus_r,    theme_focus_g,    theme_focus_b;
extern double theme_inactive_r, theme_inactive_g, theme_inactive_b;
extern double theme_status_r,   theme_status_g,   theme_status_b;

/* ─── Event Loop Flags ──────────────────────────────────────────────────── */
extern bool running;
extern bool do_login;
extern bool do_reboot;
extern bool do_shutdown;
extern bool do_suspend;
extern bool cursor_visible;

/* ─── Surfaces ──────────────────────────────────────────────────────────── */
extern cairo_surface_t *bg_surface;
extern cairo_surface_t *avatar_surface;

/* ─── GTK Mock Mode ─────────────────────────────────────────────────────── */
extern GtkWidget *drawing_area;

/* ─── DRM/KMS State ─────────────────────────────────────────────────────── */
extern int       drm_fd;
extern uint32_t  connector_id;
extern uint32_t  fb_id;
extern uint32_t  dumb_handle;
extern uint64_t  dumb_size;
extern void     *map_ptr;
extern drmModeCrtc *orig_crtc;

/* ─── Keyboard ──────────────────────────────────────────────────────────── */
#include <termios.h>
extern int           kbd_fds[MAX_KEYBOARDS];
extern int           kbd_count;
extern struct termios orig_term;

/* ─── XKB ───────────────────────────────────────────────────────────────── */
extern struct xkb_context *xkb_ctx;
extern struct xkb_keymap  *xkb_keymap;
extern struct xkb_state   *xkb_state;

/* ─── Mouse ─────────────────────────────────────────────────────────────── */
extern double mouse_x, mouse_y;
extern bool   mouse_shown;
extern int    screen_w_g;
extern int    screen_h_g;
extern int    mouse_fds[MAX_MICE];
extern int    mouse_count;

/* ─── Settings Panel ────────────────────────────────────────────────────── */
extern bool settings_open;

#endif /* GLOBALS_H */
