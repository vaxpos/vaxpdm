#ifndef PAM_AUTH_H
#define PAM_AUTH_H

#include <stdbool.h>

// Struct to hold user credentials for the PAM conversation
typedef struct {
    const char *username;
    const char *password;
} user_creds_t;

// Context to keep PAM session state
typedef struct {
    void *pamh; // pam_handle_t pointer
    bool authenticated;
    bool mock_mode;
} pam_session_t;

// Initialize PAM session context
pam_session_t *pam_session_init(bool mock_mode);

// Authenticate user credentials
bool pam_session_authenticate(pam_session_t *session, const char *username, const char *password, bool is_autologin);

// Open systemd session (pam_open_session) and set environment variables
bool pam_session_start(pam_session_t *session, const char *tty_name, const char *session_type);

// Close systemd session and clean up
void pam_session_close(pam_session_t *session);

// Free PAM session context
void pam_session_free(pam_session_t *session);

#endif // PAM_AUTH_H
