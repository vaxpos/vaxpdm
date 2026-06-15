/* main.c — نقطة الدخول الرئيسية لـ vaxp-dm */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "globals.h"
#include "config.h"
#include "session.h"
#include "login.h"
#include "gtk_mode.h"
#include "drm_mode.h"

int main(int argc, char **argv)
{
    mock_mode = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mock") == 0)
            mock_mode = true;
    }

    /* Non-root users are forced into mock mode */
    if (!mock_mode && getuid() != 0) {
        fprintf(stderr,
            "Warning: Running as non-root. "
            "DRM and sessions won't work correctly.\n"
            "Automatically switching to --mock mode.\n\n");
        sleep(2);
        mock_mode = true;
    }

    /* Discover available desktop sessions */
    sessions = discover_sessions(&session_count);

    /* Set default session from config */
    char def_session[64] = "";
    read_config_string("default_session", def_session,
                       sizeof(def_session), "");
    if (strlen(def_session) > 0 && session_count > 0) {
        for (int i = 0; i < session_count; i++) {
            if (strcasecmp(sessions[i].name, def_session) == 0 ||
                strcasecmp(sessions[i].exec, def_session) == 0) {
                selected_session_idx = i;
                break;
            }
        }
    }

    /* Load theme colors from config */
    char hex_title[32]    = "";
    char hex_focus[32]    = "";
    char hex_inactive[32] = "";
    char hex_status[32]   = "";
    read_config_string("color_title",    hex_title,    sizeof(hex_title),    "#cba6f7");
    read_config_string("color_focus",    hex_focus,    sizeof(hex_focus),    "#89b4fa");
    read_config_string("color_inactive", hex_inactive, sizeof(hex_inactive), "#7f8c8d");
    read_config_string("color_status",   hex_status,   sizeof(hex_status),   "#f9e2af");

    parse_hex_color(hex_title,    &theme_title_r,    &theme_title_g,    &theme_title_b);
    parse_hex_color(hex_focus,    &theme_focus_r,    &theme_focus_g,    &theme_focus_b);
    parse_hex_color(hex_inactive, &theme_inactive_r, &theme_inactive_g, &theme_inactive_b);
    parse_hex_color(hex_status,   &theme_status_r,   &theme_status_g,   &theme_status_b);

    /* Discover system users */
    discover_users();

    /* TTY switching configuration */
    char tty_switch_val[32] = "";
    read_config_string("tty_switching", tty_switch_val,
                       sizeof(tty_switch_val), "true");
    tty_switching = (strcmp(tty_switch_val, "false") != 0);

    char target_vt_val[32] = "";
    read_config_string("target_vt", target_vt_val,
                       sizeof(target_vt_val), "0");
    target_vt = atoi(target_vt_val);

    /* Autologin configuration */
    char autologin_val[32] = "";
    read_config_string("autologin", autologin_val,
                       sizeof(autologin_val), "false");
    autologin_enabled = (strcmp(autologin_val, "true") == 0);

    /* Determine initial user */
    char conf_user[32] = "";
    read_config_string("autologin_user", conf_user,
                       sizeof(conf_user), "");
    if (strlen(conf_user) > 0) {
        snprintf(username, sizeof(username), "%s", conf_user);
        for (int i = 0; i < system_user_count; i++) {
            if (strcmp(system_users[i], conf_user) == 0) {
                selected_user_idx = i;
                break;
            }
        }
    } else if (system_user_count > 0) {
        snprintf(username, sizeof(username), "%s", system_users[0]);
        selected_user_idx = 0;
    }

    current_focus = (strlen(username) > 0) ? FOCUS_PASSWORD : FOCUS_USERNAME;

    /* Launch the appropriate display mode */
    if (mock_mode)
        run_gtk_mock(argc, argv);
    else
        run_drm_kms();

    free_sessions(sessions, session_count);
    return EXIT_SUCCESS;
}
