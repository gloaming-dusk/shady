#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

#define PAPER_N 8
#define PAPER_SIDE (PAPER_N + 1)
#define PAPER_VERTICES (PAPER_SIDE * PAPER_SIDE)
#define PAPER_INDICES (PAPER_N * PAPER_N * 6)

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool mesh_logged;

struct paper_state {
    struct shady_representation_vertex vertices[PAPER_VERTICES];
    uint16_t indices[PAPER_INDICES];
    float phase;
    float velocity;
    uint64_t revision;
};

static void paper_rebuild(struct paper_state *s) {
    size_t out = 0;
    for (int y = 0; y < PAPER_SIDE; ++y) {
        float py = (float)y / (float)PAPER_N;
        for (int x = 0; x < PAPER_SIDE; ++x) {
            float px = (float)x / (float)PAPER_N;
            float dx = px - 0.5f;
            float dy = py - 0.5f;
            float ridge = 1.0f - (dx < 0.f ? -dx : dx) * 2.0f;
            if (ridge < 0.f) ridge = 0.f;
            float fold = (dx >= 0.f ? 1.f : -1.f) * ridge * s->phase;
            float ripple = (1.f - (dy < 0.f ? -dy : dy) * 2.f) *
                s->phase * 0.35f;
            s->vertices[out++] = (struct shady_representation_vertex){
                .x = px,
                .y = py,
                .z = -0.30f + fold * 0.42f + ripple * 0.08f,
                .u = px,
                .v = py,
            };
        }
    }
    s->revision++;
}

static void paper_build_indices(struct paper_state *s) {
    size_t out = 0;
    for (int y = 0; y < PAPER_N; ++y) {
        for (int x = 0; x < PAPER_N; ++x) {
            uint16_t i00 = (uint16_t)(y * PAPER_SIDE + x);
            uint16_t i10 = (uint16_t)(i00 + 1);
            uint16_t i01 = (uint16_t)(i00 + PAPER_SIDE);
            uint16_t i11 = (uint16_t)(i01 + 1);
            s->indices[out++] = i00;
            s->indices[out++] = i10;
            s->indices[out++] = i01;
            s->indices[out++] = i01;
            s->indices[out++] = i10;
            s->indices[out++] = i11;
        }
    }
}

static bool paper_state_init(shady_host callback_host, shady_window window,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct paper_state *s = state;
    if (!s) return false;
    s->phase = 0.0f;
    s->velocity = 1.35f;
    s->revision = 0;
    paper_build_indices(s);
    paper_rebuild(s);
    return true;
}

static bool paper_update(shady_host callback_host, shady_window window,
        float dt, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct paper_state *s = state;
    if (!s) return false;

    s->phase += s->velocity * dt;
    if (s->phase > 0.85f) {
        s->phase = 0.85f;
        s->velocity = -s->velocity;
    } else if (s->phase < 0.18f) {
        s->phase = 0.18f;
        s->velocity = -s->velocity;
    }
    paper_rebuild(s);
    return true;
}

static bool paper_mesh(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_representation_mesh *mesh,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)user_data;
    struct paper_state *s = state;
    if (!s || !mesh) return false;
    if (!mesh_logged) {
        mesh_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-folded-paper: indexed deformable mesh callback active");
    }
    mesh->struct_size = sizeof(*mesh);
    mesh->vertices = s->vertices;
    mesh->vertex_count = PAPER_VERTICES;
    mesh->indices = s->indices;
    mesh->index_count = PAPER_INDICES;
    mesh->revision = s->revision;
    return true;
}

static const struct shady_window_representation_provider paper_provider = {
    .struct_size = sizeof(struct shady_window_representation_provider),
    .base = {
        .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_MESH,
        .width = 0.22f,
        .height = 0.16f,
        .depth = 0.12f,
        .hide_titlebar = true,
    },
    .state_size = sizeof(struct paper_state),
    .state_init = paper_state_init,
    .update = paper_update,
    .mesh = paper_mesh,
};

static void apply_window(shady_window window) {
    if (window && api->window_valid(host, window))
        api->window_set_representation_provider(host, window, &paper_provider);
}

static void apply_all(void) {
    for (size_t i = 0; i < api->window_count(host); ++i)
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
        "fps-folded-paper: plugin-owned indexed deformable surface active");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation_provider(host, window, &paper_provider);
    }
}

static const char *const provides[] = {
    "spatial.window-representation.folded-paper",
    NULL,
};

static const char *const requires[] = {
    "spatial.fps",
    NULL,
};

static const struct shady_module module = {
    .name = "fps-folded-paper",
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
            !host_api->window_reset_representation_provider)
        return NULL;

    api = host_api;
    host = host_handle;
    api->subscribe_event(host, SHADY_EVENT_WINDOW_CREATED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    return &module;
}
