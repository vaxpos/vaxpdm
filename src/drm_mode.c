/* drm_mode.c — تشغيل الواجهة في وضع DRM/KMS المباشر */

#include "drm_mode.h"
#include "globals.h"
#include "draw.h"
#include "input.h"
#include "login.h"
#include "config.h"

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cairo.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <termios.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

void run_drm_kms(void)
{
    /* Raw terminal setup */
    tcgetattr(STDIN_FILENO, &orig_term);
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    printf("\033[?25l");
    fflush(stdout);

    drmModeRes       *res  = NULL;
    drmModeConnector *conn = NULL;

    /* Retry opening DRM device (up to 10 seconds) */
    for (int retry = 0; retry < 50; retry++) {
        for (int i = 0; i < 5; i++) {
            char path[32];
            snprintf(path, sizeof(path), "/dev/dri/card%d", i);
            drm_fd = open(path, O_RDWR | O_CLOEXEC);
            if (drm_fd >= 0) {
                res = drmModeGetResources(drm_fd);
                if (res) {
                    for (int j = 0; j < res->count_connectors; j++) {
                        conn = drmModeGetConnector(drm_fd,
                                                   res->connectors[j]);
                        if (conn &&
                            conn->connection == DRM_MODE_CONNECTED &&
                            conn->count_modes > 0)
                            break;
                        if (conn) { drmModeFreeConnector(conn); conn = NULL; }
                    }
                    if (conn) break;
                    drmModeFreeResources(res);
                    res = NULL;
                }
                close(drm_fd);
                drm_fd = -1;
            }
        }
        if (drm_fd >= 0) break;
        usleep(200000);
    }

    if (drm_fd < 0 || !res || !conn) {
        fprintf(stderr,
            "Failed to open DRM device or no connected connector found.\n");
        if (res)  drmModeFreeResources(res);
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

    /* Find CRTC */
    uint32_t crtc_id = 0;
    drmModeEncoder *enc =
        drmModeGetEncoder(drm_fd, conn->encoder_id);
    if (enc) {
        crtc_id = enc->crtc_id;
        drmModeFreeEncoder(enc);
    }
    if (crtc_id == 0) {
        for (int i = 0; i < conn->count_encoders; i++) {
            drmModeEncoder *e =
                drmModeGetEncoder(drm_fd, conn->encoders[i]);
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

    /* Create dumb framebuffer */
    struct drm_mode_create_dumb creq = {
        .width  = screen_w,
        .height = screen_h,
        .bpp    = 32,
        .flags  = 0
    };
    if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        perror("DRM_IOCTL_MODE_CREATE_DUMB failed");
        exit(EXIT_FAILURE);
    }
    dumb_handle = creq.handle;
    dumb_size   = creq.size;

    if (drmModeAddFB(drm_fd, screen_w, screen_h, 24, 32,
                     creq.pitch, creq.handle, &fb_id) < 0) {
        perror("drmModeAddFB failed");
        exit(EXIT_FAILURE);
    }

    struct drm_mode_map_dumb mreq = { .handle = creq.handle };
    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
        perror("DRM_IOCTL_MODE_MAP_DUMB failed");
        exit(EXIT_FAILURE);
    }

    map_ptr = mmap(0, creq.size,
                   PROT_READ | PROT_WRITE, MAP_SHARED,
                   drm_fd, mreq.offset);
    if (map_ptr == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    orig_crtc = drmModeGetCrtc(drm_fd, crtc_id);

    if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0,
                       &connector_id, 1, &mode) < 0) {
        perror("drmModeSetCrtc failed");
        exit(EXIT_FAILURE);
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    /* Cairo surfaces */
    cairo_surface_t *dumb_surf =
        cairo_image_surface_create_for_data(
            map_ptr, CAIRO_FORMAT_ARGB32,
            screen_w, screen_h, creq.pitch);
    cairo_surface_t *offscreen =
        cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, screen_w, screen_h);

    load_background();
    load_avatar();
    init_xkb();
    init_keyboards();
    init_mice();
    mouse_x = screen_w_g / 2.0;
    mouse_y = screen_h_g / 2.0;
    ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS);

    if (autologin_enabled) {
        if (!attempt_login(true))
            autologin_enabled = false;
    }

    struct pollfd fds[MAX_KEYBOARDS + MAX_MICE];
    int timeout = 500;

    /* ── Main render loop ─────────────────────────────────────────────── */
    while (running) {
        /* Render to offscreen, then blit to dumb buffer */
        cairo_t *cr_off = cairo_create(offscreen);
        draw_interface(cr_off, screen_w_g, screen_h_g);
        draw_cursor(cr_off, mouse_x, mouse_y);
        cairo_destroy(cr_off);

        cairo_t *cr_dumb = cairo_create(dumb_surf);
        cairo_set_source_surface(cr_dumb, offscreen, 0, 0);
        cairo_paint(cr_dumb);
        cairo_destroy(cr_dumb);

        drmModeDirtyFB(drm_fd, fb_id, NULL, 0);

        /* Build poll list */
        int total_fds = 0;
        for (int i = 0; i < kbd_count;   i++) {
            fds[total_fds].fd     = kbd_fds[i];
            fds[total_fds++].events = POLLIN;
        }
        for (int i = 0; i < mouse_count; i++) {
            fds[total_fds].fd     = mouse_fds[i];
            fds[total_fds++].events = POLLIN;
        }

        int ret = poll(fds, total_fds, timeout);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            cursor_visible = !cursor_visible;
            continue;
        }

        /* Keyboard events */
        for (int i = 0; i < kbd_count; i++) {
            if (!(fds[i].revents & POLLIN)) continue;
            struct input_event ev;
            while (read(fds[i].fd, &ev, sizeof(ev)) > 0) {
                if (ev.type == EV_KEY &&
                    (ev.value == 1 || ev.value == 2)) {
                    if (xkb_state) {
                        xkb_state_update_key(xkb_state,
                            ev.code + 8, XKB_KEY_DOWN);
                        update_layout_name();
                        xkb_keysym_t sym =
                            xkb_state_key_get_one_sym(
                                xkb_state, ev.code + 8);
                        char utf8_buf[32] = "";
                        xkb_state_key_get_utf8(
                            xkb_state, ev.code + 8,
                            utf8_buf, sizeof(utf8_buf));
                        handle_keysym(sym, utf8_buf);
                        cursor_visible = true;
                    }
                } else if (ev.type == EV_KEY && ev.value == 0) {
                    if (xkb_state) {
                        xkb_state_update_key(xkb_state,
                            ev.code + 8, XKB_KEY_UP);
                        update_layout_name();
                    }
                }
            }
        }

        /* Mouse events */
        for (int i = 0; i < mouse_count; i++) {
            int fi = kbd_count + i;
            if (!(fds[fi].revents & POLLIN)) continue;
            struct input_event ev;
            int  dx = 0, dy = 0;
            bool btn_left = false;
            while (read(mouse_fds[i], &ev, sizeof(ev)) > 0) {
                if (ev.type == EV_REL) {
                    if (ev.code == REL_X) dx += ev.value;
                    if (ev.code == REL_Y) dy += ev.value;
                } else if (ev.type == EV_KEY &&
                           ev.code == BTN_LEFT &&
                           ev.value == 1) {
                    btn_left = true;
                }
            }
            if (dx || dy) {
                mouse_x += dx;
                mouse_y += dy;
                if (mouse_x < 0)               mouse_x = 0;
                if (mouse_x > screen_w_g - 1)  mouse_x = screen_w_g - 1;
                if (mouse_y < 0)               mouse_y = 0;
                if (mouse_y > screen_h_g - 1)  mouse_y = screen_h_g - 1;
                mouse_shown = true;
                handle_mouse_motion(mouse_x, mouse_y,
                                    screen_w_g, screen_h_g);
            }
            if (btn_left)
                handle_mouse_click(mouse_x, mouse_y,
                                   screen_w_g, screen_h_g);
        }

        /* Post-event action checks */
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
