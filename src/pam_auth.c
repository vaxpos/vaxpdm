#include "pam_auth.h"
#include <security/pam_appl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

// Service name for PAM (needs a config file /etc/pam.d/vaxp-dm)
#define PAM_SERVICE_NAME "vaxp-dm"

static int pam_conversation(int num_msg, const struct pam_message **msg,
                            struct pam_response **resp, void *appdata) {
    if (appdata == NULL) return PAM_CONV_ERR;
    user_creds_t *creds = (user_creds_t *)appdata;

    struct pam_response *reply = calloc(num_msg, sizeof(struct pam_response));
    if (!reply) return PAM_BUF_ERR;

    for (int i = 0; i < num_msg; ++i) {
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:
                // Password prompt
                reply[i].resp = strdup(creds->password ? creds->password : "");
                reply[i].resp_retcode = 0;
                break;
            case PAM_PROMPT_ECHO_ON:
                // Username prompt
                reply[i].resp = strdup(creds->username ? creds->username : "");
                reply[i].resp_retcode = 0;
                break;
            case PAM_TEXT_INFO:
            case PAM_ERROR_MSG:
            default:
                reply[i].resp = NULL;
                reply[i].resp_retcode = 0;
                break;
        }
    }

    *resp = reply;
    return PAM_SUCCESS;
}

pam_session_t *pam_session_init(bool mock_mode) {
    pam_session_t *session = malloc(sizeof(pam_session_t));
    if (!session) return NULL;
    session->pamh = NULL;
    session->authenticated = false;
    session->mock_mode = mock_mode;
    return session;
}

bool pam_session_authenticate(pam_session_t *session, const char *username, const char *password, bool is_autologin) {
    if (!session) return false;

    if (session->mock_mode) {
        // Mock authentication: accept any username/password as long as they are not empty, OR if is_autologin is true and username is set
        if (is_autologin && username && strlen(username) > 0) {
            session->authenticated = true;
            return true;
        }
        if (username && strlen(username) > 0 && password && strlen(password) > 0) {
            session->authenticated = true;
            return true;
        }
        return false;
    }

    user_creds_t creds = { .username = username, .password = password };
    struct pam_conv conv = {
        .conv = pam_conversation,
        .appdata_ptr = &creds
    };

    pam_handle_t *pamh = NULL;
    const char *service_name = is_autologin ? "vaxp-dm-autologin" : PAM_SERVICE_NAME;
    int retval = pam_start(service_name, username, &conv, &pamh);
    if (retval != PAM_SUCCESS) {
        return false;
    }

    retval = pam_authenticate(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return false;
    }

    retval = pam_acct_mgmt(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return false;
    }

    session->pamh = pamh;
    session->authenticated = true;
    return true;
}

bool pam_session_start(pam_session_t *session, const char *tty_name, const char *session_type) {
    if (!session || !session->authenticated) return false;
    if (session->mock_mode) return true;

    pam_handle_t *pamh = (pam_handle_t *)session->pamh;

    // Set TTY
    if (tty_name) {
        pam_set_item(pamh, PAM_TTY, tty_name);
    }

    // Set session environment variables for systemd-logind
    if (session_type) {
        setenv("XDG_SESSION_TYPE", session_type, 1);
        pam_putenv(pamh, session_type[0] == 'w' ? "XDG_SESSION_TYPE=wayland" : "XDG_SESSION_TYPE=x11");
    }
    pam_putenv(pamh, "XDG_SESSION_CLASS=user");

    // Establish credentials
    int retval = pam_setcred(pamh, PAM_ESTABLISH_CRED);
    if (retval != PAM_SUCCESS) {
        return false;
    }

    // Open PAM session (this launches systemd-logind hooks)
    retval = pam_open_session(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_setcred(pamh, PAM_DELETE_CRED);
        return false;
    }

    // Import PAM environment variables into current process
    char **envlist = pam_getenvlist(pamh);
    if (envlist) {
        for (int i = 0; envlist[i] != NULL; ++i) {
            char *eq = strchr(envlist[i], '=');
            if (eq) {
                *eq = '\0';
                setenv(envlist[i], eq + 1, 1);
                *eq = '=';
            }
            free(envlist[i]);
        }
        free(envlist);
    }

    return true;
}

void pam_session_close(pam_session_t *session) {
    if (!session || !session->authenticated || session->mock_mode) return;

    pam_handle_t *pamh = (pam_handle_t *)session->pamh;
    pam_close_session(pamh, 0);
    pam_setcred(pamh, PAM_DELETE_CRED);
    pam_end(pamh, PAM_SUCCESS);
    session->pamh = NULL;
    session->authenticated = false;
}

void pam_session_free(pam_session_t *session) {
    if (!session) return;
    if (session->authenticated && !session->mock_mode) {
        pam_session_close(session);
    }
    free(session);
}
