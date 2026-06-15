/* login.c — منطق تسجيل الدخول، VT switching، تنظيف الرسومات */

#include "login.h"
#include "globals.h"
#include "input.h"    /* cleanup_keyboards(), cleanup_xkb() */

#include "pam_auth.h"
#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <sys/kd.h>
#include <sys/mman.h>
#include <gtk/gtk.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

/* ─── cleanup_graphics ──────────────────────────────────────────────────── */
void cleanup_graphics(void)
{
    if (mock_mode) {
        if (gtk_main_level() > 0)
            gtk_main_quit();
    } else {
        if (orig_crtc && drm_fd >= 0) {
            drmModeSetCrtc(drm_fd,
                           orig_crtc->crtc_id, orig_crtc->buffer_id,
                           orig_crtc->x, orig_crtc->y,
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

/* ─── get_next_free_vt ──────────────────────────────────────────────────── */
int get_next_free_vt(void)
{
    int fd = open("/dev/tty0", O_WRONLY);
    if (fd < 0) return -1;
    int vt_num = -1;
    if (ioctl(fd, VT_OPENQRY, &vt_num) < 0 || vt_num <= 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return vt_num;
}

/* ─── switch_vt ─────────────────────────────────────────────────────────── */
void switch_vt(const char *tty_name)
{
    if (!tty_name) return;
    int vt_num = -1;
    if (sscanf(tty_name, "/dev/tty%d", &vt_num) == 1 && vt_num > 0) {
        int fd = open("/dev/tty0", O_WRONLY);
        if (fd >= 0) {
            ioctl(fd, VT_ACTIVATE,  vt_num);
            ioctl(fd, VT_WAITACTIVE, vt_num);
            close(fd);
        }
    }
}

/* ─── attempt_login ─────────────────────────────────────────────────────── */
bool attempt_login(bool is_autologin)
{
    if (strlen(username) == 0) {
        strcpy(status_message, "Username cannot be empty");
        theme_status_r = 0.953;
        theme_status_g = 0.545;
        theme_status_b = 0.659;
        return false;
    }

    strcpy(status_message, "Authenticating...");
    theme_status_r = 0.537;
    theme_status_g = 0.702;
    theme_status_b = 0.980;

    pam_session_t *pam_sess = pam_session_init(mock_mode);
    if (!pam_sess) {
        strcpy(status_message, "PAM init failed");
        theme_status_r = 0.953;
        theme_status_g = 0.545;
        theme_status_b = 0.659;
        return false;
    }

    if (!pam_session_authenticate(pam_sess, username, password, is_autologin)) {
        strcpy(status_message, "Incorrect username or password");
        theme_status_r = 0.953;
        theme_status_g = 0.545;
        theme_status_b = 0.659;
        memset(password, 0, sizeof(password));
        current_focus = FOCUS_PASSWORD;
        pam_session_free(pam_sess);
        return false;
    }

    /* Determine TTY for the new session */
    char target_tty[32] = "";
    char orig_tty[32]   = "";
    const char *current_tty = ttyname(STDIN_FILENO);
    snprintf(orig_tty, sizeof(orig_tty),
             "%s", current_tty ? current_tty : "/dev/tty1");

    if (!mock_mode && tty_switching) {
        int vt_num = (target_vt > 0) ? target_vt : get_next_free_vt();
        if (vt_num > 0)
            snprintf(target_tty, sizeof(target_tty), "/dev/tty%d", vt_num);
        else
            snprintf(target_tty, sizeof(target_tty), "%s", orig_tty);
    } else {
        snprintf(target_tty, sizeof(target_tty), "%s", orig_tty);
    }

    if (!pam_session_start(pam_sess, target_tty,
                           sessions[selected_session_idx].type)) {
        strcpy(status_message, "Failed to start PAM session");
        theme_status_r = 0.953;
        theme_status_g = 0.545;
        theme_status_b = 0.659;
        pam_session_free(pam_sess);
        return false;
    }

    cleanup_graphics();
    printf("Authentication successful. Launching %s...\n",
           sessions[selected_session_idx].name);
    launch_session(&sessions[selected_session_idx],
                   username, target_tty, mock_mode);

    if (!mock_mode && tty_switching)
        switch_vt(orig_tty);

    pam_session_close(pam_sess);
    pam_session_free(pam_sess);
    free_sessions(sessions, session_count);

    exit(EXIT_SUCCESS);
}
