#ifndef SHADY_SHELL_SHELL_H
#define SHADY_SHELL_SHELL_H

/*
 * The shady-shell UI on top of the shell core: one bar per output, plus
 * the launcher, the task context menu and Quick Settings popups. State
 * arrives over the shady-shell-v1 protocol (main.c); bar.c, launcher.c,
 * menu.c and quick.c draw and handle input for their surfaces.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "core.h"

#define BAR_HEIGHT 38
#define MAX_WORKSPACES 16
#define WORKSPACE_NAME_MAX 64
#define WINDOW_TEXT_MAX 256
#define MAX_WINDOWS 64
#define MAX_APPS 512
#define APP_NAME_MAX 128
#define APP_EXEC_MAX 512
#define LAUNCHER_WIDTH 680
#define LAUNCHER_HEIGHT 580
#define LAUNCHER_RESULTS 8
#define SEARCH_MAX 128
#define CONTEXT_WIDTH 230
#define CONTEXT_ROW_HEIGHT 34
#define QUICK_WIDTH 250
#define QUICK_ROW_HEIGHT 38

struct shady_shell_v1;

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

struct region {
    double x0;
    double x1;
};

struct task_region {
    double x0;
    double x1;
    uint32_t window_id;
};

struct bar {
    struct wl_list link;
    struct shell *shell;
    struct shell_output *output;
    struct shell_surface *surface;
    /* Hit regions, recomputed on every paint. */
    struct region workspace_regions[MAX_WORKSPACES];
    struct task_region task_regions[MAX_WINDOWS];
    size_t task_region_count;
    struct region clock;
    int hovered_workspace;
    int hovered_task;
};

struct launcher {
    struct shell_surface *surface; /* NULL while hidden */
    char search[SEARCH_MAX];
    size_t selected;
    int hovered;
};

struct menu {
    struct shell_surface *surface;
    uint32_t window_id;
    int hovered;
};

struct quick {
    struct shell_surface *surface;
    struct bar *bar; /* the bar whose clock opened it */
    int hovered;
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

    struct wl_list bars; /* bar.link */
    struct launcher launcher;
    struct menu menu;
    struct quick quick;
    struct shell_timer *clock_timer;
};

static inline void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

struct shell_window *shell_find_window(struct shell *shell, uint32_t id);
/* Flush pending protocol requests right away. */
void shell_flush(struct shell *shell);

/* apps.c */
void apps_load(struct shell *shell);
size_t apps_matching(struct shell *shell, const char *query, size_t out[LAUNCHER_RESULTS]);
void apps_spawn(const struct launcher_app *app);

/* bar.c */
void bar_create(struct shell *shell, struct shell_output *output);
void bar_destroy(struct bar *bar);
struct bar *bar_for_output(struct shell *shell, struct shell_output *output);
void bars_redraw(struct shell *shell);

/* launcher.c */
void launcher_show(struct shell *shell);
void launcher_hide(struct shell *shell);
void launcher_toggle(struct shell *shell);

/* menu.c */
void menu_show(struct shell *shell, struct bar *bar, uint32_t window_id, int x);
void menu_hide(struct shell *shell);
size_t menu_row_count(struct shell *shell);

/* quick.c */
void quick_show(struct shell *shell, struct bar *bar);
void quick_hide(struct shell *shell);
void quick_toggle(struct shell *shell, struct bar *bar);
size_t quick_row_count(struct shell *shell);

#endif
