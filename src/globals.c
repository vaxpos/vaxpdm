/* globals.c — تعريف جميع المتغيرات العامة المشتركة */

#include "globals.h"

/* ─── UI State ──────────────────────────────────────────────────────────── */
int   current_focus        = FOCUS_USERNAME;
char  username[32]         = "";
char  password[32]         = "";
int   selected_session_idx = 0;
int   session_count        = 0;
session_t *sessions        = NULL;
bool  mock_mode            = false;
bool  autologin_enabled    = false;
bool  tty_switching        = true;
int   target_vt            = 0;
char  status_message[64]   = "Welcome to VAXP OS";
char  active_layout[8]     = "EN";

/* ─── User discovery ────────────────────────────────────────────────────── */
char system_users[16][32];
int  system_user_count = 0;
int  selected_user_idx = 0;

/* ─── Theme Colors (#cba6f7, #89b4fa, #7f8c8d, #f9e2af) ────────────────── */
double theme_title_r    = 0.796, theme_title_g    = 0.651, theme_title_b    = 0.969;
double theme_focus_r    = 0.537, theme_focus_g    = 0.702, theme_focus_b    = 0.980;
double theme_inactive_r = 0.498, theme_inactive_g = 0.549, theme_inactive_b = 0.553;
double theme_status_r   = 0.976, theme_status_g   = 0.886, theme_status_b   = 0.686;

/* ─── Event Loop Flags ──────────────────────────────────────────────────── */
bool running       = true;
bool do_login      = false;
bool do_reboot     = false;
bool do_shutdown   = false;
bool do_suspend    = false;
bool cursor_visible = true;

/* ─── Surfaces ──────────────────────────────────────────────────────────── */
cairo_surface_t *bg_surface     = NULL;
cairo_surface_t *avatar_surface = NULL;

/* ─── GTK Mock Mode ─────────────────────────────────────────────────────── */
GtkWidget *drawing_area = NULL;

/* ─── DRM/KMS State ─────────────────────────────────────────────────────── */
int       drm_fd       = -1;
uint32_t  connector_id = 0;
uint32_t  fb_id        = 0;
uint32_t  dumb_handle  = 0;
uint64_t  dumb_size    = 0;
void     *map_ptr      = NULL;
drmModeCrtc *orig_crtc = NULL;

/* ─── Keyboard ──────────────────────────────────────────────────────────── */
int           kbd_fds[MAX_KEYBOARDS];
int           kbd_count = 0;
struct termios orig_term;

/* ─── XKB ───────────────────────────────────────────────────────────────── */
struct xkb_context *xkb_ctx    = NULL;
struct xkb_keymap  *xkb_keymap = NULL;
struct xkb_state   *xkb_state  = NULL;

/* ─── Mouse ─────────────────────────────────────────────────────────────── */
double mouse_x    = 512;
double mouse_y    = 384;
bool   mouse_shown = false;
int    screen_w_g  = 1024;
int    screen_h_g  = 768;
int    mouse_fds[MAX_MICE];
int    mouse_count = 0;

/* ─── Settings Panel ────────────────────────────────────────────────────── */
bool settings_open = false;
