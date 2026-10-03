#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool collision_logged;

static bool cube_collision(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_collision_box *box, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)state;
    (void)user_data;
    /* Explicitly keep the physical cube equal to the visible cube. This
     * callback demonstrates that collision policy can live in the plugin. */
    if (!collision_logged) {
        collision_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-cube: collision provider callback active");
    }
    box->half[0] = 0.08f;
    box->half[1] = 0.08f;
    box->half[2] = 0.08f;
    return true;
}

static const struct shady_window_representation_provider cube_provider = {
    .struct_size = sizeof(struct shady_window_representation_provider),
    .base = {
        .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_BOX,
        .width = 0.16f,
        .height = 0.16f,
        .depth = 0.16f,
        .hide_titlebar = true,
    },
    .collision = cube_collision,
};

static void apply_window(shady_window window) {
    if (!window || !api->window_valid(host, window))
        return;
    api->window_set_representation_provider(host, window, &cube_provider);
}

static void apply_all(void) {
    for (size_t i = 0; i < api->window_count(host); i++)
        apply_window(api->window_at(host, i));
    api->schedule_render(host);
}

static void on_event(shady_host event_host,
        const struct shady_event *event, void *user_data) {
    (void)event_host;
    (void)user_data;
    if (event->type == SHADY_EVENT_WINDOW_CREATED ||
            event->type == SHADY_EVENT_WINDOW_MAPPED)
        apply_window(event->object.window);
}

static void start(struct shady_server *server) {
    (void)server;
    apply_all();
    api->log(SHADY_PLUGIN_LOG_INFO,
        "fps-cube: provider owns folded model and collision cube");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); i++) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation_provider(host, window, &cube_provider);
    }
}

static const char *const provides[] = {
    "spatial.window-representation.cube",
    NULL,
};

static const char *const requires[] = {
    "spatial.fps",
    NULL,
};

static const struct shady_module module = {
    .name = "fps-cube",
    .provides = provides,
    .requires = requires,
    .start = start,
    .stop = stop,
};

const struct shady_module *shady_plugin_entry_v1(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V1 ||
            !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size <
                offsetof(struct shady_plugin_api_v1,
                    window_representation_state) +
                sizeof(host_api->window_representation_state) ||
            !host_api->window_set_representation_provider ||
            !host_api->window_reset_representation_provider ||
            !host_api->window_representation_state)
        return NULL;

    api = host_api;
    host = host_handle;
    api->subscribe_event(host, SHADY_EVENT_WINDOW_CREATED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    return &module;
}
