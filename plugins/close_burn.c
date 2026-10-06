#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static shady_shader_program burn_program;

static const struct shady_close_effect burn_effect = {
    .style = SHADY_CLOSE_EFFECT_SLIDE_FADE,
    .duration = 0.92f,
    .strength = 1.0f,
    .direction_x = 0.0f,
    .direction_y = 0.0f,
};

static void apply_window(shady_window window) {
    if (!window || !api->window_valid(host, window) || !api->window_mapped(window))
        return;
    if (burn_program)
        api->window_set_shader(host, window, burn_program);
    api->window_set_close_effect(host, window, &burn_effect);
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
    if (event->type == SHADY_EVENT_WINDOW_MAPPED ||
            event->type == SHADY_EVENT_WINDOW_STATE_CHANGED)
        apply_window(event->object.window);
}

static bool init(struct shady_server *server) {
    (void)server;
    const char *root = getenv("SHADY_ROOT");
    if (!root || !*root) root = ".";

    char vert[1024], frag[1024];
    snprintf(vert, sizeof(vert), "%s/plugins/shaders/close_burn.vert", root);
    snprintf(frag, sizeof(frag), "%s/plugins/shaders/close_burn.frag", root);

    burn_program = api->shader_program_create(host, vert, frag);
    if (!burn_program) {
        api->log(SHADY_PLUGIN_LOG_ERROR,
            "close-burn: failed to load burn shader");
        return false;
    }
    return true;
}

static void start(struct shady_server *server) {
    (void)server;
    apply_all();
    api->log(SHADY_PLUGIN_LOG_INFO,
        "close-burn: active (window burns away on close)");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); i++) {
        shady_window window = api->window_at(host, i);
        if (!api->window_valid(host, window)) continue;
        api->window_reset_shader(host, window);
        api->window_reset_close_effect(host, window);
    }
}

static void destroy(struct shady_server *server) {
    (void)server;
    if (burn_program)
        api->shader_program_destroy(host, burn_program);
    burn_program = 0;
}

static const char *const provides[] = {
    "spatial.close-burn",
    NULL,
};

static const char *const requires[] = {
    "spatial.window-state",
    NULL,
};

static const struct shady_module module = {
    .name = "close-burn",
    .provides = provides,
    .requires = requires,
    .init = init,
    .start = start,
    .stop = stop,
    .destroy = destroy,
};

const struct shady_module *shady_plugin_entry_v1(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V1 ||
            !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size < sizeof(*host_api) ||
            !host_api->shader_program_create ||
            !host_api->shader_program_destroy ||
            !host_api->window_set_shader ||
            !host_api->window_reset_shader ||
            !host_api->window_set_close_effect ||
            !host_api->window_reset_close_effect)
        return NULL;

    api = host_api;
    host = host_handle;
    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
    return &module;
}
