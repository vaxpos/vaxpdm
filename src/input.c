/* input.c — معالجة لوحة المفاتيح والفأرة */

#include "input.h"
#include "globals.h"
#include "config.h"   /* load_avatar() */

#include <xkbcommon/xkbcommon.h>
#include <fcntl.h>
#include <unistd.h>
#include <glob.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <string.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * XKB
 * ═════════════════════════════════════════════════════════════════════════*/

void init_xkb(void)
{
    xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = {
        .rules   = NULL,
        .model   = NULL,
        .layout  = "us,ara",
        .variant = NULL,
        .options = "grp:alt_shift_toggle"
    };
    xkb_keymap = xkb_keymap_new_from_names(
        xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!xkb_keymap) {
        fprintf(stderr,
            "Failed to compile XKB keymap. Trying default.\n");
        xkb_keymap = xkb_keymap_new_from_names(
            xkb_ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    }
    if (xkb_keymap)
        xkb_state = xkb_state_new(xkb_keymap);

    strcpy(active_layout, "EN");
}

void cleanup_xkb(void)
{
    if (xkb_state)   { xkb_state_unref(xkb_state);     xkb_state   = NULL; }
    if (xkb_keymap)  { xkb_keymap_unref(xkb_keymap);   xkb_keymap  = NULL; }
    if (xkb_ctx)     { xkb_context_unref(xkb_ctx);      xkb_ctx     = NULL; }
}

void update_layout_name(void)
{
    if (!xkb_keymap || !xkb_state) {
        strcpy(active_layout, "EN");
        return;
    }
    xkb_layout_index_t num = xkb_keymap_num_layouts(xkb_keymap);
    for (xkb_layout_index_t i = 0; i < num; i++) {
        if (xkb_state_layout_index_is_active(
                xkb_state, i, XKB_STATE_LAYOUT_EFFECTIVE)) {
            const char *name = xkb_keymap_layout_get_name(xkb_keymap, i);
            strcpy(active_layout,
                   (name && strcasestr(name, "arabic")) ? "AR" : "EN");
            return;
        }
    }
    strcpy(active_layout, "EN");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Keyboard devices
 * ═════════════════════════════════════════════════════════════════════════*/

void init_keyboards(void)
{
    glob_t g;
    kbd_count = 0;
    if (glob("/dev/input/by-path/*-event-kbd", 0, NULL, &g) == 0) {
        for (size_t i = 0;
             i < g.gl_pathc && kbd_count < MAX_KEYBOARDS; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0) {
                if (ioctl(fd, EVIOCGRAB, 1) < 0)
                    perror("EVIOCGRAB failed");
                kbd_fds[kbd_count++] = fd;
            }
        }
        globfree(&g);
    }
    /* Fallback to event0 */
    if (kbd_count == 0) {
        int fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            ioctl(fd, EVIOCGRAB, 1);
            kbd_fds[kbd_count++] = fd;
        }
    }
}

void cleanup_keyboards(void)
{
    for (int i = 0; i < kbd_count; i++) {
        ioctl(kbd_fds[i], EVIOCGRAB, 0);
        close(kbd_fds[i]);
    }
    kbd_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mouse devices
 * ═════════════════════════════════════════════════════════════════════════*/

void init_mice(void)
{
    mouse_count = 0;
    glob_t g;
    if (glob("/dev/input/by-path/*-event-mouse", 0, NULL, &g) == 0) {
        for (size_t i = 0;
             i < g.gl_pathc && mouse_count < MAX_MICE; i++) {
            int fd = open(g.gl_pathv[i], O_RDONLY | O_NONBLOCK);
            if (fd >= 0)
                mouse_fds[mouse_count++] = fd;
        }
        globfree(&g);
    }
    /* Fallback: scan event devices for relative axis capability */
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

void cleanup_mice(void)
{
    for (int i = 0; i < mouse_count; i++)
        close(mouse_fds[i]);
    mouse_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Keyboard event handler
 * ═════════════════════════════════════════════════════════════════════════*/

void handle_keysym(xkb_keysym_t sym, const char *utf8)
{
    /* Settings panel open: navigate sessions only */
    if (settings_open) {
        if (sym == XKB_KEY_Escape) {
            settings_open = false;
            return;
        }
        if (sym == XKB_KEY_Up) {
            if (session_count > 0)
                selected_session_idx =
                    (selected_session_idx - 1 + session_count) % session_count;
            return;
        }
        if (sym == XKB_KEY_Down) {
            if (session_count > 0)
                selected_session_idx =
                    (selected_session_idx + 1) % session_count;
            return;
        }
        if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            settings_open = false;
            return;
        }
        return; /* swallow other keys while panel is open */
    }

    /* Normal navigation */
    if (sym == XKB_KEY_Tab || sym == XKB_KEY_Down) {
        current_focus = (current_focus + 1) % FOCUS_COUNT;
    } else if (sym == XKB_KEY_ISO_Left_Tab || sym == XKB_KEY_Up) {
        current_focus = (current_focus - 1 + FOCUS_COUNT) % FOCUS_COUNT;
    } else if (sym == XKB_KEY_Left) {
        if (current_focus == FOCUS_USERNAME && system_user_count > 1) {
            selected_user_idx =
                (selected_user_idx - 1 + system_user_count) % system_user_count;
            strncpy(username, system_users[selected_user_idx],
                    sizeof(username) - 1);
            load_avatar();
        }
    } else if (sym == XKB_KEY_Right) {
        if (current_focus == FOCUS_USERNAME && system_user_count > 1) {
            selected_user_idx =
                (selected_user_idx + 1) % system_user_count;
            strncpy(username, system_users[selected_user_idx],
                    sizeof(username) - 1);
            load_avatar();
        }
    } else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        if (current_focus == FOCUS_USERNAME) {
            current_focus = FOCUS_PASSWORD;
        } else if (current_focus == FOCUS_PASSWORD ||
                   current_focus == FOCUS_LOGIN) {
            do_login = true;
            running  = false;
        } else if (current_focus == FOCUS_REBOOT) {
            do_reboot = true; running = false;
        } else if (current_focus == FOCUS_SHUTDOWN) {
            do_shutdown = true; running = false;
        } else if (current_focus == FOCUS_SUSPEND) {
            do_suspend = true; running = false;
        } else if (current_focus == FOCUS_SETTINGS) {
            settings_open = !settings_open;
        }
    } else if (sym == XKB_KEY_BackSpace) {
        if (current_focus == FOCUS_USERNAME) {
            int len = strlen(username);
            while (len > 0) {
                len--;
                if ((username[len] & 0xC0) != 0x80) {
                    username[len] = '\0';
                    break;
                }
            }
        } else if (current_focus == FOCUS_PASSWORD) {
            int len = strlen(password);
            while (len > 0) {
                len--;
                if ((password[len] & 0xC0) != 0x80) {
                    password[len] = '\0';
                    break;
                }
            }
        }
    } else if (sym == XKB_KEY_Escape) {
        if (settings_open)
            settings_open = false;
        else if (mock_mode)
            running = false;
    } else {
        /* Printable character */
        if (utf8 && strlen(utf8) > 0) {
            if (current_focus == FOCUS_USERNAME) {
                size_t len = strlen(username);
                if (len + strlen(utf8) < sizeof(username) - 1)
                    strcat(username, utf8);
            } else if (current_focus == FOCUS_PASSWORD) {
                size_t len = strlen(password);
                if (len + strlen(utf8) < sizeof(password) - 1)
                    strcat(password, utf8);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mouse hit testing
 * ═════════════════════════════════════════════════════════════════════════*/

int hit_test_element(double mx, double my, int width, int height,
                     int *sess_idx)
{
    if (sess_idx) *sess_idx = -1;

    double box_w = 300, box_h = 440;
    double box_x = (width  - box_w) / 2.0;
    double box_y = (height - box_h) / 2.0;

    /* Avatar / username area */
    double av_cx = box_x + box_w / 2.0;
    double av_cy = box_y + 155;
    double av_r  = 48.0;
    double dx = mx - av_cx, dy = my - av_cy;
    if (dx*dx + dy*dy <= av_r*av_r) return FOCUS_USERNAME;

    /* Password field */
    double field_w = 240, field_h = 38;
    double field_x = box_x + (box_w - field_w) / 2.0;
    double pass_y  = box_y + 233;
    if (mx >= field_x && mx <= field_x + field_w &&
        my >= pass_y  && my <= pass_y  + field_h)
        return FOCUS_PASSWORD;

    /* Bottom icon buttons */
    double icon_r      = 24.0;
    double icons_y     = box_y + box_h - 18.0 - 22;
    double icon_spacing = 58.0;
    double icon_center = box_x + box_w / 2.0;

    dx = mx - (icon_center - icon_spacing); dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_SHUTDOWN;
    dx = mx - icon_center; dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_REBOOT;
    dx = mx - (icon_center + icon_spacing); dy = my - icons_y;
    if (dx*dx + dy*dy <= icon_r*icon_r) return FOCUS_SUSPEND;

    /* Settings gear button */
    double gear_r  = 28.0;
    double gear_cx = (double)width  - 22.0 - 22.0;
    double gear_cy = (double)height - 22.0 - 22.0;
    dx = mx - gear_cx; dy = my - gear_cy;
    if (dx*dx + dy*dy <= gear_r*gear_r) return FOCUS_SETTINGS;

    /* Session popup items */
    if (settings_open && session_count > 0) {
        double panel_w = 140;
        for (int i = 0; i < session_count; i++) {
            int name_len = (int)strlen(sessions[i].name);
            double needed = name_len * 7.5 + 36.0;
            if (needed > panel_w) panel_w = needed;
        }
        double item_h   = 34;
        double header_h = 22;
        double panel_h  = item_h * session_count + header_h + 8;
        double panel_x  = gear_cx + 22.0 - panel_w;
        double panel_y  = gear_cy - 22.0 - panel_h - 8;

        if (panel_x < 10)               panel_x = 10;
        if (panel_x + panel_w > width - 10) panel_x = width - panel_w - 10;
        if (panel_y < 10)               panel_y = 10;
        if (panel_y + panel_h > height - 10) panel_y = height - panel_h - 10;

        double sep_y       = panel_y + 6 + 12 + 4;
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

    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Mouse click / motion
 * ═════════════════════════════════════════════════════════════════════════*/

void handle_mouse_click(double mx, double my, int width, int height)
{
    int sess_idx = -1;
    int elem = hit_test_element(mx, my, width, height, &sess_idx);

    if (elem < 0) return;

    switch (elem) {
    case FOCUS_USERNAME:
        if (settings_open) { settings_open = false; return; }
        if (system_user_count > 1) {
            selected_user_idx =
                (selected_user_idx + 1) % system_user_count;
            strncpy(username, system_users[selected_user_idx],
                    sizeof(username) - 1);
            load_avatar();
        }
        current_focus = FOCUS_USERNAME;
        break;

    case FOCUS_PASSWORD:
        if (settings_open) { settings_open = false; return; }
        current_focus  = FOCUS_PASSWORD;
        cursor_visible = true;
        break;

    case FOCUS_SETTINGS:
        if (sess_idx >= 0 && sess_idx < session_count) {
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
        do_shutdown   = true;
        running       = false;
        break;

    case FOCUS_REBOOT:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_REBOOT;
        do_reboot     = true;
        running       = false;
        break;

    case FOCUS_SUSPEND:
        if (settings_open) { settings_open = false; return; }
        current_focus = FOCUS_SUSPEND;
        do_suspend    = true;
        running       = false;
        break;

    default:
        if (settings_open) settings_open = false;
        break;
    }
}

void handle_mouse_motion(double mx, double my, int width, int height)
{
    mouse_x    = mx;
    mouse_y    = my;
    mouse_shown = true;
    int elem = hit_test_element(mx, my, width, height, NULL);
    if (elem >= 0 && !settings_open)
        current_focus = elem;
}
