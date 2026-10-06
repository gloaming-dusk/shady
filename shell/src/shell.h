#ifndef SHADY_SHELL_SHELL_H
#define SHADY_SHELL_SHELL_H

/*
 * shady-shell: the shell core, the compositor model it receives over
 * shady-shell-v1 (main.c), and the Lua runtime that turns that model into
 * bars and popups (lua.c, ui.c). The default UI is shell/default.lua.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "core.h"

#define MAX_WORKSPACES 16
#define WORKSPACE_NAME_MAX 64
#define WINDOW_TEXT_MAX 256
#define MAX_WINDOWS 64
#define MAX_APPS 512
#define APP_NAME_MAX 128
#define APP_EXEC_MAX 512

struct shady_shell_v1;
struct shell_lua;

struct shell_window {
    uint32_t id;
    char app_id[WINDOW_TEXT_MAX];
    char title[WINDOW_TEXT_MAX];
    char workspace[WORKSPACE_NAME_MAX];
    bool focused;
    bool maximized;
    bool fullscreen;
};

struct launcher_app {
    char name[APP_NAME_MAX];
    char exec[APP_EXEC_MAX];
};

struct shell {
    struct shell_core core;
    struct shady_shell_v1 *protocol;
    bool snapshot_done;

    char workspaces[MAX_WORKSPACES][WORKSPACE_NAME_MAX];
    size_t workspace_count;
    char active_workspace[WORKSPACE_NAME_MAX];
    char focused_app_id[WINDOW_TEXT_MAX];
    char focused_title[WINDOW_TEXT_MAX];
    struct shell_window windows[MAX_WINDOWS];
    size_t window_count;

    struct launcher_app apps[MAX_APPS];
    size_t app_count;

    struct shell_lua *lua;
};

static inline void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

/* Flush pending protocol requests right away. */
void shell_flush(struct shell *shell);

/* apps.c */
void apps_load(struct shell *shell);
/* Indices of up to `max` apps whose name or command contains `query`. */
size_t apps_matching(struct shell *shell, const char *query, size_t *out, size_t max);
void apps_spawn(const char *command);

#endif
