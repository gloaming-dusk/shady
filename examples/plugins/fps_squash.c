#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static const struct shady_window_representation squash = {
    .struct_size = sizeof(struct shady_window_representation),
    .kind = SHADY_WINDOW_REPRESENTATION_BOX,
    .width = 0.24f,
    .height = 0.065f,
    .depth = 0.13f,
    .hide_titlebar = true,
};

static void apply_window(shady_window window) {
    if (window && api->window_valid(host, window))
        api->window_set_representation(host, window, &squash);
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
        "fps-squash: folded FPS windows use a flattened box representation");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); i++) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation(host, window);
    }
}

static const char *const provides[] = {
    "spatial.window-representation.squash",
    NULL,
};

static const char *const requires[] = {
    "spatial.fps",
    NULL,
};

static const struct shady_module module = {
    .name = "fps-squash",
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
                offsetof(struct shady_plugin_api_v1, window_representation) +
                sizeof(host_api->window_representation) ||
            !host_api->window_set_representation ||
            !host_api->window_reset_representation)
        return NULL;

    api = host_api;
    host = host_handle;
    api->subscribe_event(host, SHADY_EVENT_WINDOW_CREATED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    return &module;
}
