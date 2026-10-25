/* Runs shady-auth (auth_helper.c) and reads what it says. */
#define _GNU_SOURCE
#include "auth.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "core.h"

struct shell_auth {
    struct shell_core *core;
    const struct shell_auth_handler *handler;
    void *data;
    pid_t pid;
    int fd;
    struct shell_watch *watch;
    char buffer[1024];
    size_t length;
    bool finished;
    bool dispatching; /* inside a handler: cancelling must wait */
    bool cancelled;
    bool ok;
    char reason[256];
};

static void helper_path(char *out, size_t size) {
    /* Tests point this at a helper that misbehaves. */
    const char *env = getenv("SHADY_AUTH_HELPER");
    if (env && *env) {
        snprintf(out, size, "%s", env);
        return;
    }
    char self[PATH_MAX - 16];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            *slash = '\0';
            snprintf(out, size, "%s/shady-auth", self);
            if (access(out, X_OK) == 0) return;
        }
    }
    snprintf(out, size, "shady-auth");
}

static void auth_free(struct shell_auth *auth) {
    if (auth->watch) shell_watch_remove(auth->watch);
    if (auth->fd >= 0) close(auth->fd);
    if (auth->pid > 0) {
        kill(auth->pid, SIGTERM);
        waitpid(auth->pid, NULL, 0);
    }
    free(auth);
}

/* Note the result; the reader reports it and frees the attempt once it is
 * done with the buffer (the reason may point into it). */
static void finish(struct shell_auth *auth, bool ok, const char *reason) {
    if (auth->finished) return;
    auth->finished = true;
    auth->ok = ok;
    snprintf(auth->reason, sizeof(auth->reason), "%s", reason);
}

static void report(struct shell_auth *auth) {
    if (auth->pid > 0) {
        waitpid(auth->pid, NULL, 0);
        auth->pid = -1;
    }
    const struct shell_auth_handler *handler = auth->handler;
    void *data = auth->data;
    bool ok = auth->ok;
    char reason[sizeof(auth->reason)];
    memcpy(reason, auth->reason, sizeof(reason));
    auth_free(auth);
    handler->done(data, ok, reason);
}

static void handle_line(struct shell_auth *auth, char *line) {
    if (!strcmp(line, "ok")) finish(auth, true, "");
    else if (!strncmp(line, "fail", 4)) finish(auth, false, line[4] ? line + 5 : "Authentication failed");
    else if (!strncmp(line, "info ", 5)) auth->handler->message(auth->data, false, line + 5);
    else if (!strncmp(line, "error ", 6)) auth->handler->message(auth->data, true, line + 6);
}

static void readable(int fd, short revents, void *data) {
    struct shell_auth *auth = data;
    if (revents & POLLIN) {
        ssize_t n = read(fd, auth->buffer + auth->length, sizeof(auth->buffer) - 1 - auth->length);
        if (n > 0) {
            auth->length += (size_t)n;
            auth->buffer[auth->length] = '\0';
            char *start = auth->buffer, *nl;
            while (!auth->finished && (nl = strchr(start, '\n'))) {
                *nl = '\0';
                auth->dispatching = true;
                handle_line(auth, start);
                auth->dispatching = false;
                if (auth->cancelled) {
                    auth_free(auth); /* cancelled from the message handler */
                    return;
                }
                start = nl + 1;
            }
            if (auth->finished) {
                report(auth); /* frees it */
                return;
            }
            auth->length = strlen(start);
            memmove(auth->buffer, start, auth->length + 1);
            if (auth->length == sizeof(auth->buffer) - 1) auth->length = 0;
            return;
        }
        if (n < 0 && errno == EINTR) return;
    }
    finish(auth, false, "The authentication helper stopped");
    report(auth);
}

struct shell_auth *shell_auth_start(struct shell_core *core, const char *service,
        const char *password, const struct shell_auth_handler *handler, void *data) {
    struct passwd *pw = getpwuid(getuid());
    if (!pw || !service) return NULL;
    char helper[PATH_MAX];
    helper_path(helper, sizeof(helper));

    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC) < 0) return NULL;
    if (pipe2(out, O_CLOEXEC) < 0) {
        close(in[0]);
        close(in[1]);
        return NULL;
    }
    /* The password goes into the pipe before the helper exists. A pipe
     * holds far more than a password, so this never blocks, and with the
     * read end still ours it cannot meet a helper that already exited
     * (exec failed, PAM refused without asking): that write would raise
     * SIGPIPE and end the shell under its own lock screen. */
    if (password) {
        size_t length = strlen(password);
        if (write(in[1], password, length) < 0 || write(in[1], "\n", 1) < 0) {
            close(in[0]); close(in[1]); close(out[0]); close(out[1]);
            return NULL;
        }
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        return NULL;
    }
    if (pid == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        execlp(helper, "shady-auth", service, pw->pw_name, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    close(in[1]);

    struct shell_auth *auth = calloc(1, sizeof(*auth));
    if (!auth) {
        close(out[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return NULL;
    }
    auth->core = core;
    auth->handler = handler;
    auth->data = data;
    auth->pid = pid;
    auth->fd = out[0];
    auth->watch = shell_watch_add(core, auth->fd, POLLIN, readable, auth);
    if (!auth->watch) {
        auth_free(auth);
        return NULL;
    }
    return auth;
}

void shell_auth_cancel(struct shell_auth *auth) {
    if (!auth || auth->finished) return;
    if (auth->dispatching) auth->cancelled = true; /* the reader frees it */
    else auth_free(auth);
}
