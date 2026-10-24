#ifndef SHADY_SHELL_AUTH_H
#define SHADY_SHELL_AUTH_H

#include <stdbool.h>

struct shell_core;
struct shell_auth;

/* Authenticate the user running the shell through the shady-auth helper.
 * `password` NULL answers no password (fingerprint services). on_message
 * gets PAM's info and error lines as they come; on_done the result, once;
 * the attempt is freed after it. */
struct shell_auth_handler {
    void (*message)(void *data, bool error, const char *text);
    void (*done)(void *data, bool ok, const char *reason);
};

struct shell_auth *shell_auth_start(struct shell_core *core, const char *service,
    const char *password, const struct shell_auth_handler *handler, void *data);
/* Stop an attempt: no callbacks follow. */
void shell_auth_cancel(struct shell_auth *auth);

#endif
