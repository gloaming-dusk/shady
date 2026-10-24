#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    printf("done ok=%d reason=%s\n", ok, copy);
    done = 1;
}
static const struct shell_auth_handler handler = { message, finished };
int main(void) {
    struct shell_core core = {0};
    for (int round = 0; round < 3; round++) {
        done = 0;
        if (!shell_auth_start(&core, "other", round == 1 ? NULL : "wrong", &handler, NULL)) return 1;
        while (!done && active) {
            struct pollfd p = { active->fd, POLLIN, 0 };
            if (poll(&p, 1, 5000) <= 0) { puts("timeout"); return 1; }
            active->fn(p.fd, p.revents, active->data);
        }
    }
    puts("shell-auth: PASS");
    return 0;
}
