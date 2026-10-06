#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool model_logged;
static bool update_logged;

struct squash_state {
    unsigned model_calls;
    unsigned update_calls;
    float elapsed;
};

static bool squash_state_init(shady_host callback_host, shady_window window,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct squash_state *s = state;
    if (s) {
        s->model_calls = 0;
        s->update_calls = 0;
        s->elapsed = 0.f;
    }
    return true;
}

static void squash_state_destroy(shady_host callback_host, shady_window window,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct squash_state *s = state;
    if (s && s->model_calls > 0)
        api->log(SHADY_PLUGIN_LOG_DEBUG,
            "fps-squash: per-window provider state destroyed");
}

static bool squash_update(shady_host callback_host, shady_window window,
        float dt, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct squash_state *s = state;
    if (!s) return false;
    s->update_calls++;
    s->elapsed += dt;
    if (!update_logged) {
        update_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-squash: provider update callback active");
    }
    /* Keep frames alive briefly to demonstrate host-driven representation
     * animation without a plugin timer/render hook. */
    return s->elapsed < 0.25f;
}

static bool squash_model(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_representation_model *model,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;

    struct squash_state *s = state;
    if (s) s->model_calls++;

    if (!model_logged) {
        model_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-squash: model provider callback active");
    }

    /* The representation itself decides how interaction state changes shape.
     * A held tile becomes slightly thinner/wider without FPS knowing why. */
    if (s && s->elapsed < 0.25f)
        model->height += (0.25f - s->elapsed) * 0.018f;

    if (context->held) {
        model->width = 0.27f;
        model->height = 0.052f;
        model->depth = 0.115f;
    } else if (context->focused) {
        model->height = 0.072f;
    }
    return true;
}

static const struct shady_window_representation_provider squash_provider = {
    .struct_size = sizeof(struct shady_window_representation_provider),
    .base = {
        .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_BOX,
        .width = 0.24f,
        .height = 0.065f,
        .depth = 0.13f,
        .hide_titlebar = true,
    },
    .state_size = sizeof(struct squash_state),
    .state_init = squash_state_init,
    .state_destroy = squash_state_destroy,
    .update = squash_update,
    .model = squash_model,
};

static void apply_window(shady_window window) {
    if (!window || !api->window_valid(host, window))
        return;
    if (!api->window_set_representation_provider(host, window, &squash_provider))
        return;

    size_t state_size = 0;
    void *state = api->window_representation_state(
        host, window, &squash_provider, &state_size);
    if (!state || state_size != sizeof(struct squash_state))
        api->log(SHADY_PLUGIN_LOG_ERROR,
            "fps-squash: provider state unavailable after attach");
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
        "fps-squash: provider dynamically shapes folded FPS windows");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); i++) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation_provider(host, window, &squash_provider);
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
