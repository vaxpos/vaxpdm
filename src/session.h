#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>

typedef struct {
    char *name;      // Display name (e.g., "Sway", "KDE Plasma")
    char *exec;      // Executable command (e.g., "dbus-run-session sway")
    char *type;      // "wayland" or "x11"
} session_t;

// Discover available sessions in /usr/share/xsessions and /usr/share/wayland-sessions
session_t *discover_sessions(int *count);

// Free memory allocated for sessions list
void free_sessions(session_t *sessions, int count);

// Launch the chosen session as the authenticated user (forks and drops privileges)
bool launch_session(const session_t *session, const char *username, const char *tty_name, bool mock_mode);

#endif // SESSION_H
