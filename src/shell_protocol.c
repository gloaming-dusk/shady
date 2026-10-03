#include "shell_protocol.h"

#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "shady.h"
#include "event/event.h"
#include "modules/desktop/state.h"
#include "modules/workspace/workspace.h"
#include "shady-shell-v1-server-protocol.h"

struct shady_shell_protocol_state;

struct shady_shell_client {
    struct wl_list link;
    struct wl_resource *resource;
    struct shady_shell_protocol_state *protocol;
};

struct shady_shell_protocol_state {
    struct shady_server *server;
    struct wl_global *global;
    struct wl_list clients;
    struct shady_toplevel *focused;
};

static struct shady_toplevel *find_window_id(
        struct shady_shell_protocol_state *state, uint32_t id) {
    struct shady_toplevel *toplevel;
    wl_list_for_each(toplevel, &state->server->all_toplevels, all_link) {
        if (toplevel->shell_id == id) return toplevel;
    }
    return NULL;
}

static bool resource_has_windows(struct wl_resource *resource) {
    return wl_resource_get_version(resource) >= 2;
}

static bool resource_has_window_actions(struct wl_resource *resource) {
    return wl_resource_get_version(resource) >= 3;
}

static void send_window(struct wl_resource *resource,
        struct shady_shell_protocol_state *state,
        struct shady_toplevel *toplevel) {
    if (!resource_has_windows(resource) || !toplevel || !toplevel->mapped) return;
    const char *app_id = toplevel->xdg_toplevel->app_id;
    const char *title = toplevel->xdg_toplevel->title;
    const char *workspace = shady_workspace_toplevel_name(toplevel);
    bool focused = state->focused == toplevel;
    shady_shell_v1_send_window(resource, toplevel->shell_id,
        app_id ? app_id : "", title ? title : "",
        workspace ? workspace : "", focused ? 1u : 0u);
    if (resource_has_window_actions(resource))
        shady_shell_v1_send_window_state(resource, toplevel->shell_id,
            toplevel->maximized ? 1u : 0u, toplevel->fullscreen ? 1u : 0u);
}

static void send_focused(struct wl_resource *resource,
        struct shady_shell_protocol_state *state) {
    struct shady_toplevel *toplevel = state->focused;
    const char *app_id = "";
    const char *title = "";
    if (toplevel && toplevel->xdg_toplevel) {
        if (toplevel->xdg_toplevel->app_id)
            app_id = toplevel->xdg_toplevel->app_id;
        if (toplevel->xdg_toplevel->title)
            title = toplevel->xdg_toplevel->title;
    }
    shady_shell_v1_send_focused_window(resource, app_id, title);
}

static void send_snapshot(struct wl_resource *resource,
        struct shady_shell_protocol_state *state) {
    struct shady_server *server = state->server;
    size_t count = shady_workspace_count(server);
    for (size_t i = 0; i < count; i++) {
        const char *name = shady_workspace_name_at(server, i);
        if (name && *name)
            shady_shell_v1_send_workspace(resource, name);
    }
    shady_shell_v1_send_active_workspace(resource,
        shady_workspace_current_name(server));
    send_focused(resource, state);
    if (resource_has_windows(resource)) {
        struct shady_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->all_toplevels, all_link)
            send_window(resource, state, toplevel);
    }
    shady_shell_v1_send_done(resource);
}

static void client_destroy(struct wl_resource *resource) {
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client) return;
    wl_list_remove(&client->link);
    free(client);
}

static void request_activate_workspace(struct wl_client *wl_client,
        struct wl_resource *resource, const char *name) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol || !name || !*name) return;
    (void)shady_workspace_switch(client->protocol->server, name);
}

static void request_activate_window(struct wl_client *wl_client,
        struct wl_resource *resource, uint32_t id) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol) return;
    struct shady_toplevel *toplevel = find_window_id(client->protocol, id);
    if (!toplevel || !toplevel->mapped) return;
    const char *workspace = shady_workspace_toplevel_name(toplevel);
    if (workspace && *workspace &&
            strcmp(workspace, shady_workspace_current_name(toplevel->server)) != 0)
        shady_workspace_switch(toplevel->server, workspace);
    focus_toplevel(toplevel);
}

static void request_close_window(struct wl_client *wl_client,
        struct wl_resource *resource, uint32_t id) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol) return;
    struct shady_toplevel *toplevel = find_window_id(client->protocol, id);
    if (toplevel && toplevel->mapped)
        wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}

static void request_toggle_maximize(struct wl_client *wl_client,
        struct wl_resource *resource, uint32_t id) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol) return;
    struct shady_toplevel *toplevel = find_window_id(client->protocol, id);
    if (toplevel && toplevel->mapped)
        shady_toplevel_set_maximized(toplevel, !toplevel->maximized);
}

static void request_toggle_fullscreen(struct wl_client *wl_client,
        struct wl_resource *resource, uint32_t id) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol) return;
    struct shady_toplevel *toplevel = find_window_id(client->protocol, id);
    if (toplevel && toplevel->mapped)
        shady_toplevel_set_fullscreen(toplevel, !toplevel->fullscreen);
}

static void request_move_window_to_workspace(struct wl_client *wl_client,
        struct wl_resource *resource, uint32_t id, const char *workspace) {
    (void)wl_client;
    struct shady_shell_client *client = wl_resource_get_user_data(resource);
    if (!client || !client->protocol || !workspace || !*workspace) return;
    struct shady_toplevel *toplevel = find_window_id(client->protocol, id);
    if (toplevel && toplevel->mapped)
        shady_workspace_move_toplevel(toplevel, workspace);
}

static const struct shady_shell_v1_interface shell_impl = {
    .activate_workspace = request_activate_workspace,
    .activate_window = request_activate_window,
    .close_window = request_close_window,
    .toggle_maximize = request_toggle_maximize,
    .toggle_fullscreen = request_toggle_fullscreen,
    .move_window_to_workspace = request_move_window_to_workspace,
};

static void bind_shell(struct wl_client *wl_client, void *data,
        uint32_t version, uint32_t id) {
    struct shady_shell_protocol_state *state = data;
    struct wl_resource *resource = wl_resource_create(wl_client,
        &shady_shell_v1_interface, version < 3 ? version : 3, id);
    if (!resource) {
        wl_client_post_no_memory(wl_client);
        return;
    }

    struct shady_shell_client *client = calloc(1, sizeof(*client));
    if (!client) {
        wl_resource_destroy(resource);
        wl_client_post_no_memory(wl_client);
        return;
    }

    client->resource = resource;
    client->protocol = state;
    wl_list_insert(&state->clients, &client->link);
    wl_resource_set_implementation(resource, &shell_impl, client,
        client_destroy);
    send_snapshot(resource, state);
}

static void protocol_event(shady_host host,
        const struct shady_event *event, void *user_data) {
    (void)host;
    struct shady_shell_protocol_state *state = user_data;
    if (!state || !event) return;

    if (event->type == SHADY_EVENT_WINDOW_FOCUSED) {
        state->focused = (struct shady_toplevel *)event->object.window;
    } else if ((event->type == SHADY_EVENT_WINDOW_UNMAPPED ||
            event->type == SHADY_EVENT_WINDOW_DESTROYED) &&
            state->focused == (struct shady_toplevel *)event->object.window) {
        state->focused = NULL;
    }

    struct shady_shell_client *client;
    wl_list_for_each(client, &state->clients, link) {
        switch (event->type) {
        case SHADY_EVENT_WORKSPACE_CHANGED:
            if (event->object.workspace && *event->object.workspace) {
                /* Repeating workspace is intentional: clients dedupe names,
                 * so a dynamically-created workspace becomes discoverable. */
                shady_shell_v1_send_workspace(client->resource,
                    event->object.workspace);
                shady_shell_v1_send_active_workspace(client->resource,
                    event->object.workspace);
            }
            if (resource_has_windows(client->resource)) {
                struct shady_toplevel *window;
                wl_list_for_each(window, &state->server->all_toplevels, all_link)
                    send_window(client->resource, state, window);
            }
            break;
        case SHADY_EVENT_WINDOW_MAPPED:
        case SHADY_EVENT_WINDOW_FOCUSED:
        case SHADY_EVENT_WINDOW_RESIZED:
        case SHADY_EVENT_WINDOW_STATE_CHANGED:
            send_focused(client->resource, state);
            send_window(client->resource, state,
                (struct shady_toplevel *)event->object.window);
            break;
        case SHADY_EVENT_WINDOW_UNMAPPED:
        case SHADY_EVENT_WINDOW_DESTROYED: {
            struct shady_toplevel *window =
                (struct shady_toplevel *)event->object.window;
            send_focused(client->resource, state);
            if (resource_has_windows(client->resource) && window)
                shady_shell_v1_send_window_removed(client->resource,
                    window->shell_id);
            break;
        }
        default:
            break;
        }
    }
}

bool shady_shell_protocol_init(struct shady_server *server) {
    struct shady_desktop_state *desktop = shady_desktop_state(server);
    if (!desktop) return false;

    struct shady_shell_protocol_state *state = calloc(1, sizeof(*state));
    if (!state) return false;
    state->server = server;
    wl_list_init(&state->clients);

    state->global = wl_global_create(server->wl_display,
        &shady_shell_v1_interface, 3, state, bind_shell);
    if (!state->global) {
        free(state);
        return false;
    }
    desktop->shell_protocol = state;

    if (!shady_event_subscribe_owned(server, SHADY_EVENT_WORKSPACE_CHANGED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_MAPPED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_FOCUSED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_RESIZED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_STATE_CHANGED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_UNMAPPED,
            protocol_event, state, state) ||
        !shady_event_subscribe_owned(server, SHADY_EVENT_WINDOW_DESTROYED,
            protocol_event, state, state)) {
        shady_shell_protocol_finish(server);
        return false;
    }
    return true;
}

void shady_shell_protocol_toggle_launcher(struct shady_server *server) {
    struct shady_desktop_state *desktop = shady_desktop_state(server);
    if (!desktop || !desktop->shell_protocol) return;
    struct shady_shell_protocol_state *state = desktop->shell_protocol;
    struct shady_shell_client *client;
    wl_list_for_each(client, &state->clients, link) {
        shady_shell_v1_send_toggle_launcher(client->resource);
    }
}

void shady_shell_protocol_finish(struct shady_server *server) {
    struct shady_desktop_state *desktop = shady_desktop_state(server);
    if (!desktop || !desktop->shell_protocol) return;
    struct shady_shell_protocol_state *state = desktop->shell_protocol;

    shady_event_unsubscribe_owner(server, state);
    struct shady_shell_client *client, *tmp;
    wl_list_for_each_safe(client, tmp, &state->clients, link) {
        wl_resource_destroy(client->resource);
    }
    if (state->global) wl_global_destroy(state->global);
    free(state);
    desktop->shell_protocol = NULL;
}
