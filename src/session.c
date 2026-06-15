#include "session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>

extern char **environ;

static void parse_desktop_file(const char *filepath, const char *type, session_t *session) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char line[512];
    session->name = NULL;
    session->exec = NULL;
    session->type = strdup(type);

    while (fgets(line, sizeof(line), f)) {
        // Strip newline
        line[strcspn(line, "\r\n")] = '\0';

        // Check if we are in the main Desktop Entry group
        if (strncmp(line, "Name=", 5) == 0 && !session->name) {
            session->name = strdup(line + 5);
        } else if (strncmp(line, "Exec=", 5) == 0 && !session->exec) {
            session->exec = strdup(line + 5);
        }
    }
    fclose(f);

    if (!session->name && session->exec) {
        session->name = strdup(session->exec);
    }
}

session_t *discover_sessions(int *count) {
    int max_sessions = 32;
    session_t *sessions = malloc(sizeof(session_t) * max_sessions);
    int c = 0;

    const char *dirs[] = {
        "/usr/share/wayland-sessions",
        "/usr/share/xsessions",
        "/usr/local/share/wayland-sessions",
        "/usr/local/share/xsessions"
    };
    const char *types[] = { "wayland", "x11", "wayland", "x11" };

    for (int d = 0; d < 4; ++d) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Check if it's a regular file or link
            if (entry->d_type == DT_REG || entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN) {
                // Verify .desktop extension
                char *dot = strrchr(entry->d_name, '.');
                if (dot && strcmp(dot, ".desktop") == 0) {
                    char path[1024];
                    snprintf(path, sizeof(path), "%s/%s", dirs[d], entry->d_name);

                    session_t s = {0};
                    parse_desktop_file(path, types[d], &s);
                    if (s.exec) {
                        sessions[c++] = s;
                        if (c >= max_sessions) {
                            max_sessions *= 2;
                            sessions = realloc(sessions, sizeof(session_t) * max_sessions);
                        }
                    } else {
                        free(s.name);
                        free(s.type);
                    }
                }
            }
        }
        closedir(dir);
    }

    // Fallback if no sessions are found
    if (c == 0) {
        sessions[c].name = strdup("Mock Terminal (xterm)");
        sessions[c].exec = strdup("xterm");
        sessions[c].type = strdup("x11");
        c++;
    }

    *count = c;
    return sessions;
}

void free_sessions(session_t *sessions, int count) {
    if (!sessions) return;
    for (int i = 0; i < count; ++i) {
        free(sessions[i].name);
        free(sessions[i].exec);
        free(sessions[i].type);
    }
    free(sessions);
}

bool launch_session(const session_t *session, const char *username, const char *tty_name, bool mock_mode) {
    if (!session || !username) return false;

    if (mock_mode) {
        printf("\n[Mock Mode] Starting session: %s (%s) using command: '%s' for user: %s (TTY: %s)\n",
               session->name, session->type, session->exec, username, tty_name ? tty_name : "none");
        sleep(2); // Simulate launching
        return true;
    }

    struct passwd *pw = getpwnam(username);
    if (!pw) {
        fprintf(stderr, "User %s not found on system.\n", username);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return false;
    }

    if (pid == 0) {
        // Child process: setup terminal, environment, drop privileges and execute

        // 1. Start a new session leader
        setsid();

        // 2. Set controlling terminal if tty_name is provided
        if (tty_name) {
            int fd = open(tty_name, O_RDWR);
            if (fd >= 0) {
                // TIOCSCTTY with arg=1 steals the TTY if it's already in use
                ioctl(fd, TIOCSCTTY, 1);
                dup2(fd, STDIN_FILENO);
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                if (fd > 2) close(fd);
            }
        }

        // 3. Drop privileges: set GID, groups, and UID
        if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
            perror("initgroups failed");
            exit(EXIT_FAILURE);
        }
        if (setgid(pw->pw_gid) != 0) {
            perror("setgid failed");
            exit(EXIT_FAILURE);
        }
        if (setuid(pw->pw_uid) != 0) {
            perror("setuid failed");
            exit(EXIT_FAILURE);
        }

        // 4. Set essential environment variables
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("SHELL", pw->pw_shell, 1);
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        if (tty_name) {
            setenv("TTY", tty_name, 1);
        }
        
        // Change working directory to user home
        if (chdir(pw->pw_dir) != 0) {
            perror("chdir failed");
            exit(EXIT_FAILURE);
        }

        // Exec user shell with the session command
        char *args[] = { pw->pw_shell, "-c", session->exec, NULL };
        execve(pw->pw_shell, args, environ);

        // If execve returns, an error occurred
        perror("execve failed");
        exit(EXIT_FAILURE);
    }

    // Parent process: wait for user session to exit
    int status;
    waitpid(pid, &status, 0);
    return true;
}
