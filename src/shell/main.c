#define _POSIX_C_SOURCE 200809L

/*
 * shady-shell entry point: binds the shady-shell-v1 protocol, keeps the
 * workspace/window model it describes, and gives every output a bar.
 * Drawing and input live in bar.c, launcher.c, menu.c and quick.c; the
 * Wayland plumbing, rendering and event loop in core.c and render_*.c.
 */

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shady-shell-v1-client-protocol.h"
#include "shell.h"
#include "theme.h"

struct shell_window *shell_find_window(struct shell *shell, uint32_t id) {
    for (size_t i = 0; i < shell->window_count; i++)
        if (shell->windows[i].id == id) return &shell->windows[i];
    return NULL;
}

void shell_flush(struct shell *shell) {
    wl_display_flush(shell->core.display);
}

/* Every surface shows some of the model, and repaints are coalesced per
 * loop iteration, so any model change simply marks everything dirty. */
static void model_changed(struct shell *shell) {
    shell_core_redraw_all(&shell->core);
}

static struct shell_window *ensure_window(struct shell *shell, uint32_t id) {
    struct shell_window *window = shell_find_window(shell, id);
    if (window) return window;
    if (shell->window_count >= MAX_WINDOWS) return NULL;
    window = &shell->windows[shell->window_count++];
    memset(window, 0, sizeof(*window));
    window->id = id;
    return window;
}

static void remove_window(struct shell *shell, uint32_t id) {
    if (shell->menu.window_id == id) menu_hide(shell);
    for (size_t i = 0; i < shell->window_count; i++) {
        if (shell->windows[i].id != id) continue;
        if (i + 1 < shell->window_count)
            memmove(&shell->windows[i], &shell->windows[i + 1],
                (shell->window_count - i - 1) * sizeof(shell->windows[0]));
        shell->window_count--;
        return;
    }
}

static bool add_workspace(struct shell *shell, const char *name) {
    if (!name || !*name) return false;
    for (size_t i = 0; i < shell->workspace_count; i++)
        if (strcmp(shell->workspaces[i], name) == 0) return false;
    if (shell->workspace_count >= MAX_WORKSPACES) return false;
    copy_text(shell->workspaces[shell->workspace_count],
        sizeof(shell->workspaces[shell->workspace_count]), name);
    shell->workspace_count++;
    return true;
}

/* ---- shady-shell-v1 --------------------------------------------------- */

static void protocol_workspace(void *data, struct shady_shell_v1 *protocol, const char *name) {
    (void)protocol;
    struct shell *shell = data;
    if (!add_workspace(shell, name)) return;
    /* Quick Settings is sized by the workspace count: reopen it. */
    if (shell->quick.surface) {
        struct bar *bar = shell->quick.bar;
        quick_hide(shell);
        quick_show(shell, bar);
    }
    model_changed(shell);
}

static void protocol_active_workspace(void *data, struct shady_shell_v1 *protocol,
        const char *name) {
    (void)protocol;
    struct shell *shell = data;
    add_workspace(shell, name);
    copy_text(shell->active_workspace, sizeof(shell->active_workspace), name);
    model_changed(shell);
}

static void protocol_focused_window(void *data, struct shady_shell_v1 *protocol,
        const char *app_id, const char *title) {
    (void)protocol;
    struct shell *shell = data;
    copy_text(shell->focused_app_id, sizeof(shell->focused_app_id), app_id);
    copy_text(shell->focused_title, sizeof(shell->focused_title), title);
    model_changed(shell);
}

static void protocol_window(void *data, struct shady_shell_v1 *protocol, uint32_t id,
        const char *app_id, const char *title, const char *workspace, uint32_t focused) {
    (void)protocol;
    struct shell *shell = data;
    struct shell_window *window = ensure_window(shell, id);
    if (!window) return;
    copy_text(window->app_id, sizeof(window->app_id), app_id);
    copy_text(window->title, sizeof(window->title), title);
    copy_text(window->workspace, sizeof(window->workspace), workspace);
    window->focused = focused != 0;
    /* Focus is exclusive, but the compositor only re-sends the newly focused
     * window, so clear the previous holder here. */
    if (window->focused) {
        for (size_t i = 0; i < shell->window_count; i++)
            if (&shell->windows[i] != window) shell->windows[i].focused = false;
    }
    model_changed(shell);
}

static void protocol_window_removed(void *data, struct shady_shell_v1 *protocol, uint32_t id) {
    (void)protocol;
    struct shell *shell = data;
    remove_window(shell, id);
    model_changed(shell);
}

static void protocol_window_state(void *data, struct shady_shell_v1 *protocol, uint32_t id,
        uint32_t maximized, uint32_t fullscreen) {
    (void)protocol;
    struct shell *shell = data;
    struct shell_window *window = ensure_window(shell, id);
    if (!window) return;
    window->maximized = maximized != 0;
    window->fullscreen = fullscreen != 0;
    model_changed(shell);
}

static void protocol_toggle_launcher(void *data, struct shady_shell_v1 *protocol) {
    (void)protocol;
    launcher_toggle(data);
}

static void protocol_done(void *data, struct shady_shell_v1 *protocol) {
    (void)protocol;
    struct shell *shell = data;
    shell->snapshot_done = true;
    fprintf(stderr, "shady-shell: state workspaces=%zu active=%s focus=%s\n",
        shell->workspace_count,
        shell->active_workspace[0] ? shell->active_workspace : "<none>",
        shell->focused_title[0] ? shell->focused_title :
            (shell->focused_app_id[0] ? shell->focused_app_id : "<none>"));
    model_changed(shell);
}

static const struct shady_shell_v1_listener protocol_listener = {
    .workspace = protocol_workspace,
    .active_workspace = protocol_active_workspace,
    .focused_window = protocol_focused_window,
    .window = protocol_window,
    .window_removed = protocol_window_removed,
    .window_state = protocol_window_state,
    .toggle_launcher = protocol_toggle_launcher,
    .done = protocol_done,
};

/* ---- core callbacks --------------------------------------------------- */

static void output_added(void *data, struct shell_output *output) {
    struct shell *shell = data;
    if (!bar_for_output(shell, output)) bar_create(shell, output);
}

static void output_removed(void *data, struct shell_output *output) {
    struct shell *shell = data;
    struct bar *bar = bar_for_output(shell, output);
    if (bar) bar_destroy(bar);
}

static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    struct shell *shell = data;
    if (strcmp(interface, shady_shell_v1_interface.name) != 0 || shell->protocol) return;
    shell->protocol = wl_registry_bind(registry, name, &shady_shell_v1_interface,
        version < 4 ? version : 4);
    shady_shell_v1_add_listener(shell->protocol, &protocol_listener, shell);
}

static const struct shell_core_listener core_listener = {
    .output_added = output_added,
    .output_removed = output_removed,
    .global = global,
};

/* The clock shows minutes: repaint the bars on each minute boundary. */
static void clock_tick(void *data) {
    struct shell *shell = data;
    bars_redraw(shell);
    time_t now = time(NULL);
    uint32_t ms = (uint32_t)(60 - now % 60) * 1000u;
    shell->clock_timer = shell_timer_add(&shell->core, ms, clock_tick, shell);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    ui_load_theme();
    static struct shell shell;
    wl_list_init(&shell.bars);
    shell.launcher.hovered = -1;
    shell.menu.hovered = -1;
    shell.quick.hovered = -1;
    apps_load(&shell);

    int status = 1;
    if (!shell_core_init(&shell.core, &core_listener, &shell)) goto out;
    if (!shell.protocol) {
        fprintf(stderr, "shady-shell: compositor lacks the shady-shell-v1 protocol\n");
        goto out;
    }
    const char *open_launcher = getenv("SHADY_SHELL_OPEN_LAUNCHER");
    if (open_launcher && *open_launcher && strcmp(open_launcher, "0") != 0)
        launcher_show(&shell);
    clock_tick(&shell);
    status = shell_core_run(&shell.core) < 0 ? 1 : 0;

out:
    launcher_hide(&shell);
    menu_hide(&shell);
    quick_hide(&shell);
    struct bar *bar, *tmp;
    wl_list_for_each_safe(bar, tmp, &shell.bars, link) bar_destroy(bar);
    if (shell.protocol) shady_shell_v1_destroy(shell.protocol);
    shell_core_finish(&shell.core);
    return status;
}
