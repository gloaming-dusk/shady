/*
 * protocol-probe: a minimal third-party client of the standard window and
 * workspace protocols, used by tests/headless-standard-protocols.sh.
 *
 *   protocol-probe list                 print toplevels (both protocols) and workspaces
 *   protocol-probe activate <app_id>    zwlr_foreign_toplevel_handle_v1.activate
 *   protocol-probe maximize <app_id>    .set_maximized
 *   protocol-probe fullscreen <app_id>  .set_fullscreen
 *   protocol-probe close <app_id>       .close
 *   protocol-probe workspace <name>     ext_workspace_handle_v1.activate + commit
 *
 * `list` prints, in a stable order:
 *   wlr <app_id> | <title> | activated maximized fullscreen (flags present)
 *   ext <app_id> | <title>
 *   workspace <name> active|inactive
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-workspace-v1-client-protocol.h"

#define MAX_ITEMS 64

struct toplevel {
    void *handle;
    char app_id[256];
    char title[256];
    bool activated, maximized, fullscreen;
};

struct workspace {
    struct ext_workspace_handle_v1 *handle;
    char name[128];
    bool active;
};

static struct wl_seat *seat;
static struct zwlr_foreign_toplevel_manager_v1 *wlr_manager;
static struct ext_foreign_toplevel_list_v1 *ext_list;
static struct ext_workspace_manager_v1 *workspace_manager;
static struct toplevel wlr_toplevels[MAX_ITEMS], ext_toplevels[MAX_ITEMS];
static size_t wlr_count, ext_count;
static struct workspace workspaces[MAX_ITEMS];
static size_t workspace_count;

static struct toplevel *find(struct toplevel *list, size_t count, void *handle) {
    for (size_t i = 0; i < count; i++)
        if (list[i].handle == handle) return &list[i];
    return NULL;
}

/* ---- zwlr_foreign_toplevel ------------------------------------------- */

static void wlr_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *title) {
    (void)data;
    struct toplevel *t = find(wlr_toplevels, wlr_count, h);
    if (t) snprintf(t->title, sizeof(t->title), "%s", title);
}

static void wlr_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, const char *app_id) {
    (void)data;
    struct toplevel *t = find(wlr_toplevels, wlr_count, h);
    if (t) snprintf(t->app_id, sizeof(t->app_id), "%s", app_id);
}

static void wlr_output_enter(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
        struct wl_output *output) {
    (void)data; (void)h; (void)output;
}

static void wlr_output_leave(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
        struct wl_output *output) {
    (void)data; (void)h; (void)output;
}

static void wlr_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *h, struct wl_array *state) {
    (void)data;
    struct toplevel *t = find(wlr_toplevels, wlr_count, h);
    if (!t) return;
    t->activated = t->maximized = t->fullscreen = false;
    uint32_t *s;
    wl_array_for_each(s, state) {
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) t->activated = true;
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) t->maximized = true;
        if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) t->fullscreen = true;
    }
}

static void wlr_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)data; (void)h;
}

static void wlr_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)data;
    struct toplevel *t = find(wlr_toplevels, wlr_count, h);
    if (t) t->handle = NULL;
}

static void wlr_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *h,
        struct zwlr_foreign_toplevel_handle_v1 *parent) {
    (void)data; (void)h; (void)parent;
}

static const struct zwlr_foreign_toplevel_handle_v1_listener wlr_handle_listener = {
    .title = wlr_title,
    .app_id = wlr_app_id,
    .output_enter = wlr_output_enter,
    .output_leave = wlr_output_leave,
    .state = wlr_state,
    .done = wlr_done,
    .closed = wlr_closed,
    .parent = wlr_parent,
};

static void wlr_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *m,
        struct zwlr_foreign_toplevel_handle_v1 *h) {
    (void)data; (void)m;
    if (wlr_count == MAX_ITEMS) return;
    wlr_toplevels[wlr_count++] = (struct toplevel){ .handle = h };
    zwlr_foreign_toplevel_handle_v1_add_listener(h, &wlr_handle_listener, NULL);
}

static void wlr_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *m) {
    (void)data; (void)m;
}

static const struct zwlr_foreign_toplevel_manager_v1_listener wlr_manager_listener = {
    .toplevel = wlr_toplevel,
    .finished = wlr_finished,
};

/* ---- ext_foreign_toplevel_list --------------------------------------- */

static void ext_closed(void *data, struct ext_foreign_toplevel_handle_v1 *h) {
    (void)data;
    struct toplevel *t = find(ext_toplevels, ext_count, h);
    if (t) t->handle = NULL;
}

static void ext_done(void *data, struct ext_foreign_toplevel_handle_v1 *h) {
    (void)data; (void)h;
}

static void ext_title(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *title) {
    (void)data;
    struct toplevel *t = find(ext_toplevels, ext_count, h);
    if (t) snprintf(t->title, sizeof(t->title), "%s", title);
}

static void ext_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *app_id) {
    (void)data;
    struct toplevel *t = find(ext_toplevels, ext_count, h);
    if (t) snprintf(t->app_id, sizeof(t->app_id), "%s", app_id);
}

static void ext_identifier(void *data, struct ext_foreign_toplevel_handle_v1 *h, const char *id) {
    (void)data; (void)h; (void)id;
}

static const struct ext_foreign_toplevel_handle_v1_listener ext_handle_listener = {
    .closed = ext_closed,
    .done = ext_done,
    .title = ext_title,
    .app_id = ext_app_id,
    .identifier = ext_identifier,
};

static void ext_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *l,
        struct ext_foreign_toplevel_handle_v1 *h) {
    (void)data; (void)l;
    if (ext_count == MAX_ITEMS) return;
    ext_toplevels[ext_count++] = (struct toplevel){ .handle = h };
    ext_foreign_toplevel_handle_v1_add_listener(h, &ext_handle_listener, NULL);
}

static void ext_finished(void *data, struct ext_foreign_toplevel_list_v1 *l) {
    (void)data; (void)l;
}

static const struct ext_foreign_toplevel_list_v1_listener ext_list_listener = {
    .toplevel = ext_toplevel,
    .finished = ext_finished,
};

/* ---- ext_workspace ---------------------------------------------------- */

static struct workspace *find_workspace(struct ext_workspace_handle_v1 *h) {
    for (size_t i = 0; i < workspace_count; i++)
        if (workspaces[i].handle == h) return &workspaces[i];
    return NULL;
}

static void ws_id(void *data, struct ext_workspace_handle_v1 *h, const char *id) {
    (void)data; (void)h; (void)id;
}

static void ws_name(void *data, struct ext_workspace_handle_v1 *h, const char *name) {
    (void)data;
    struct workspace *w = find_workspace(h);
    if (w) snprintf(w->name, sizeof(w->name), "%s", name);
}

static void ws_coordinates(void *data, struct ext_workspace_handle_v1 *h, struct wl_array *c) {
    (void)data; (void)h; (void)c;
}

static void ws_state(void *data, struct ext_workspace_handle_v1 *h, uint32_t state) {
    (void)data;
    struct workspace *w = find_workspace(h);
    if (w) w->active = state & EXT_WORKSPACE_HANDLE_V1_STATE_ACTIVE;
}

static void ws_capabilities(void *data, struct ext_workspace_handle_v1 *h, uint32_t caps) {
    (void)data; (void)h; (void)caps;
}

static void ws_removed(void *data, struct ext_workspace_handle_v1 *h) {
    (void)data;
    struct workspace *w = find_workspace(h);
    if (w) w->handle = NULL;
}

static const struct ext_workspace_handle_v1_listener ws_listener = {
    .id = ws_id,
    .name = ws_name,
    .coordinates = ws_coordinates,
    .state = ws_state,
    .capabilities = ws_capabilities,
    .removed = ws_removed,
};

static void group_capabilities(void *data, struct ext_workspace_group_handle_v1 *g, uint32_t caps) {
    (void)data; (void)g; (void)caps;
}

static void group_output_enter(void *data, struct ext_workspace_group_handle_v1 *g,
        struct wl_output *o) {
    (void)data; (void)g; (void)o;
}

static void group_output_leave(void *data, struct ext_workspace_group_handle_v1 *g,
        struct wl_output *o) {
    (void)data; (void)g; (void)o;
}

static void group_workspace_enter(void *data, struct ext_workspace_group_handle_v1 *g,
        struct ext_workspace_handle_v1 *w) {
    (void)data; (void)g; (void)w;
}

static void group_workspace_leave(void *data, struct ext_workspace_group_handle_v1 *g,
        struct ext_workspace_handle_v1 *w) {
    (void)data; (void)g; (void)w;
}

static void group_removed(void *data, struct ext_workspace_group_handle_v1 *g) {
    (void)data; (void)g;
}

static const struct ext_workspace_group_handle_v1_listener group_listener = {
    .capabilities = group_capabilities,
    .output_enter = group_output_enter,
    .output_leave = group_output_leave,
    .workspace_enter = group_workspace_enter,
    .workspace_leave = group_workspace_leave,
    .removed = group_removed,
};

static void wm_group(void *data, struct ext_workspace_manager_v1 *m,
        struct ext_workspace_group_handle_v1 *g) {
    (void)data; (void)m;
    ext_workspace_group_handle_v1_add_listener(g, &group_listener, NULL);
}

static void wm_workspace(void *data, struct ext_workspace_manager_v1 *m,
        struct ext_workspace_handle_v1 *w) {
    (void)data; (void)m;
    if (workspace_count == MAX_ITEMS) return;
    workspaces[workspace_count++] = (struct workspace){ .handle = w };
    ext_workspace_handle_v1_add_listener(w, &ws_listener, NULL);
}

static void wm_done(void *data, struct ext_workspace_manager_v1 *m) {
    (void)data; (void)m;
}

static void wm_finished(void *data, struct ext_workspace_manager_v1 *m) {
    (void)data; (void)m;
}

static const struct ext_workspace_manager_v1_listener wm_listener = {
    .workspace_group = wm_group,
    .workspace = wm_workspace,
    .done = wm_done,
    .finished = wm_finished,
};

/* ---- registry --------------------------------------------------------- */

static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    (void)data;
    if (!strcmp(interface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    } else if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        wlr_manager = wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface,
            version < 3 ? version : 3);
        zwlr_foreign_toplevel_manager_v1_add_listener(wlr_manager, &wlr_manager_listener, NULL);
    } else if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
        ext_list = wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1);
        ext_foreign_toplevel_list_v1_add_listener(ext_list, &ext_list_listener, NULL);
    } else if (!strcmp(interface, ext_workspace_manager_v1_interface.name)) {
        workspace_manager = wl_registry_bind(registry, name, &ext_workspace_manager_v1_interface, 1);
        ext_workspace_manager_v1_add_listener(workspace_manager, &wm_listener, NULL);
    }
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = global,
    .global_remove = global_remove,
};

static struct toplevel *by_app_id(const char *app_id) {
    for (size_t i = 0; i < wlr_count; i++)
        if (wlr_toplevels[i].handle && !strcmp(wlr_toplevels[i].app_id, app_id))
            return &wlr_toplevels[i];
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: protocol-probe list | activate|maximize|fullscreen|close <app_id> | workspace <name>\n");
        return 2;
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "protocol-probe: cannot connect\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    /* Globals, then their initial objects, then those objects' state. */
    for (int i = 0; i < 3; i++) wl_display_roundtrip(display);
    if (!wlr_manager || !ext_list || !workspace_manager) {
        fprintf(stderr, "protocol-probe: missing globals (wlr %d, ext %d, workspace %d)\n",
            !!wlr_manager, !!ext_list, !!workspace_manager);
        return 1;
    }

    const char *cmd = argv[1];
    const char *arg = argc > 2 ? argv[2] : "";
    int status = 0;
    if (!strcmp(cmd, "list")) {
        for (size_t i = 0; i < wlr_count; i++) {
            struct toplevel *t = &wlr_toplevels[i];
            if (!t->handle) continue;
            printf("wlr %s | %s |%s%s%s\n", t->app_id, t->title,
                t->activated ? " activated" : "", t->maximized ? " maximized" : "",
                t->fullscreen ? " fullscreen" : "");
        }
        for (size_t i = 0; i < ext_count; i++)
            if (ext_toplevels[i].handle)
                printf("ext %s | %s\n", ext_toplevels[i].app_id, ext_toplevels[i].title);
        for (size_t i = 0; i < workspace_count; i++)
            if (workspaces[i].handle)
                printf("workspace %s %s\n", workspaces[i].name,
                    workspaces[i].active ? "active" : "inactive");
    } else if (!strcmp(cmd, "workspace")) {
        struct workspace *target = NULL;
        for (size_t i = 0; i < workspace_count; i++)
            if (workspaces[i].handle && !strcmp(workspaces[i].name, arg)) target = &workspaces[i];
        if (!target) {
            fprintf(stderr, "protocol-probe: no workspace %s\n", arg);
            status = 1;
        } else {
            ext_workspace_handle_v1_activate(target->handle);
            ext_workspace_manager_v1_commit(workspace_manager);
        }
    } else {
        struct toplevel *t = by_app_id(arg);
        if (!t) {
            fprintf(stderr, "protocol-probe: no toplevel %s\n", arg);
            status = 1;
        } else if (!strcmp(cmd, "activate")) {
            zwlr_foreign_toplevel_handle_v1_activate(t->handle, seat);
        } else if (!strcmp(cmd, "maximize")) {
            zwlr_foreign_toplevel_handle_v1_set_maximized(t->handle);
        } else if (!strcmp(cmd, "fullscreen")) {
            zwlr_foreign_toplevel_handle_v1_set_fullscreen(t->handle, NULL);
        } else if (!strcmp(cmd, "close")) {
            zwlr_foreign_toplevel_handle_v1_close(t->handle);
        } else {
            fprintf(stderr, "protocol-probe: unknown command %s\n", cmd);
            status = 2;
        }
    }
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return status;
}
