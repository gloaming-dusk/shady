#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool update_logged;
static bool model_logged;

struct jelly_state {
    float compression;
    float velocity;
    float target;
};

static bool jelly_state_init(shady_host callback_host, shady_window window,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct jelly_state *s = state;
    if (!s) return false;
    s->compression = 0.18f;
    s->velocity = 0.f;
    s->target = 0.f;
    return true;
}

static bool jelly_update(shady_host callback_host, shady_window window,
        float dt, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct jelly_state *s = state;
    if (!s) return false;

    if (!update_logged) {
        update_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-jelly: spring update callback active");
    }

    float diff = s->target - s->compression;
    float acceleration = diff * 72.f - s->velocity * 13.f;
    s->velocity += acceleration * dt;
    s->compression += s->velocity * dt;

    if (s->compression < -0.20f) s->compression = -0.20f;
    if (s->compression > 1.20f) s->compression = 1.20f;

    float abs_diff = diff < 0.f ? -diff : diff;
    float abs_velocity = s->velocity < 0.f ? -s->velocity : s->velocity;
    if (abs_diff < 0.0015f && abs_velocity < 0.002f) {
        s->compression = s->target;
        s->velocity = 0.f;
        return false;
    }
    return true;
}

static bool jelly_model(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_representation_model *model,
        void *state, void *user_data) {
    (void)window;
    (void)user_data;
    struct jelly_state *s = state;
    if (!s) return false;

    if (!model_logged) {
        model_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-jelly: deforming model callback active");
    }

    float target = context->held ? 1.0f : (context->focused ? 0.28f : 0.f);
    if (target != s->target) {
        s->target = target;
        api->schedule_render(callback_host);
    }

    float q = s->compression;
    model->width *= 1.f + q * 0.34f;
    model->height *= 1.f - q * 0.43f;
    model->depth *= 1.f + q * 0.20f;

    if (model->height < 0.035f) model->height = 0.035f;
    if (model->depth < 0.050f) model->depth = 0.050f;
    return true;
}

static const struct shady_window_representation_provider jelly_provider = {
    .struct_size = sizeof(struct shady_window_representation_provider),
    .base = {
        .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_BOX,
        .width = 0.17f,
        .height = 0.15f,
        .depth = 0.15f,
        .hide_titlebar = true,
    },
    .state_size = sizeof(struct jelly_state),
    .state_init = jelly_state_init,
    .update = jelly_update,
    .model = jelly_model,
};

static void apply_window(shady_window window) {
    if (window && api->window_valid(host, window))
        api->window_set_representation_provider(host, window, &jelly_provider);
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
        "fps-jelly: spring-driven folded window representation active");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); i++) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation_provider(host, window, &jelly_provider);
    }
}

static const char *const provides[] = {
    "spatial.window-representation.jelly",
    NULL,
};

static const char *const requires[] = {
    "spatial.fps",
    NULL,
};

static const struct shady_module module = {
    .name = "fps-jelly",
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
