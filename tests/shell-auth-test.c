#define _POSIX_C_SOURCE 200809L
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "auth.h"
/* shell.authenticate's spawner (shell/src/auth.c) against the real
 * shady-auth helper and the "other" PAM service, which denies: a result
 * arrives once per attempt, its reason readable in the callback, and
 * nothing is touched after the attempt is freed (run under ASan).
 * Stand-ins for the shell core's fd watches: one watch, polled here. */
#include "core.h"
struct shell_watch { int fd; void (*fn)(int, short, void *); void *data; };
static struct shell_watch *active;
struct shell_watch *shell_watch_add(struct shell_core *core, int fd, short events,
        void (*fn)(int, short, void *), void *data) {
    (void)core; (void)events;
    active = calloc(1, sizeof(*active));
    active->fd = fd; active->fn = fn; active->data = data;
    return active;
}
void shell_watch_remove(struct shell_watch *w) { if (w == active) active = NULL; free(w); }
static int done;
static void message(void *d, bool error, const char *t) { (void)d; printf("message %s: %s\n", error ? "error" : "info", t); }
static void finished(void *d, bool ok, const char *reason) {
    (void)d;
    char copy[256];
    snprintf(copy, sizeof(copy), "%s", reason); /* read it: ASan checks the memory */
    if (ok) {
        printf("authenticated by a helper that should fail: %s\n", copy);
        exit(1);
    }
    done = 1;
}
static const struct shell_auth_handler handler = { message, finished };
/* A lock screen may cancel an attempt from its own on_message. */
static struct shell_auth *cancelling;
static int messages;
static void cancel_on_message(void *d, bool error, const char *t) {
    (void)d; (void)error; (void)t;
    messages++;
    shell_auth_cancel(cancelling);
}
static void finished_after_cancel(void *d, bool ok, const char *reason) {
    (void)d; (void)ok; (void)reason;
    puts("a cancelled attempt reported a result");
    exit(1);
}
static const struct shell_auth_handler cancel_handler = { cancel_on_message, finished_after_cancel };
int main(void) {
    struct shell_core core = {0};
    if (!shell_auth_available()) { puts("the helper next to the test is not found"); return 1; }
    /* Longer than the helper reads, or than a pipe holds: refused at
     * once, never a write that blocks the shell. */
    static char huge[1 << 20];
    memset(huge, 'x', sizeof(huge) - 1);
    if (shell_auth_start(&core, "other", huge, &handler, NULL)) {
        puts("started with a 1 MiB password");
        return 1;
    }
    for (int round = 0; round < 3; round++) {
        done = 0;
        if (!shell_auth_start(&core, "other", round == 1 ? NULL : "wrong", &handler, NULL)) return 1;
        while (!done && active) {
            struct pollfd p = { active->fd, POLLIN, 0 };
            if (poll(&p, 1, 5000) <= 0) { puts("timeout"); return 1; }
            active->fn(p.fd, p.revents, active->data);
        }
    }
    /* A helper that cannot even start: the attempt fails, and nothing
     * (SIGPIPE from writing the password to it) ends the process. */
    setenv("SHADY_AUTH_HELPER", "/nonexistent/shady-auth", 1);
    if (shell_auth_available()) { puts("a missing helper counted as available"); return 1; }
    for (int round = 0; round < 50; round++) {
        done = 0;
        if (!shell_auth_start(&core, "other", "wrong", &handler, NULL)) return 1;
        while (!done && active) {
            struct pollfd p = { active->fd, POLLIN, 0 };
            if (poll(&p, 1, 5000) <= 0) { puts("timeout"); return 1; }
            active->fn(p.fd, p.revents, active->data);
        }
    }
    /* Cancelled from on_message, with more lines in the same read: the
     * reader stops there, no further callback, no use after free. */
    char helper[] = "/tmp/shady-auth-test-XXXXXX";
    int fd = mkstemp(helper);
    if (fd < 0) return 1;
    const char *script = "#!/bin/sh\nprintf 'info one\\ninfo two\\nok\\n'\n";
    if (write(fd, script, strlen(script)) < 0) return 1;
    fchmod(fd, 0700);
    close(fd);
    setenv("SHADY_AUTH_HELPER", helper, 1);
    messages = 0;
    cancelling = shell_auth_start(&core, "other", NULL, &cancel_handler, NULL);
    if (!cancelling) return 1;
    while (active) {
        struct pollfd p = { active->fd, POLLIN, 0 };
        if (poll(&p, 1, 5000) <= 0) { puts("timeout"); return 1; }
        active->fn(p.fd, p.revents, active->data);
    }
    unlink(helper);
    if (messages != 1) { printf("%d messages after cancelling\n", messages); return 1; }
    puts("shell-auth: PASS");
    return 0;
}
