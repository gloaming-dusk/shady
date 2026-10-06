#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool collision_logged;
static bool hull_logged;
static bool compound_logged;

static const struct shady_collision_vertex cube_hull_vertices[] = {
    {0.f, 0.f, -1.f}, {1.f, 0.f, -1.f},
    {0.f, 1.f, -1.f}, {1.f, 1.f, -1.f},
    {0.f, 0.f,  0.f}, {1.f, 0.f,  0.f},
    {0.f, 1.f,  0.f}, {1.f, 1.f,  0.f},
};

static const struct shady_collision_vertex cube_left_vertices[] = {
    {0.f, 0.f, -1.f}, {.5f, 0.f, -1.f},
    {0.f, 1.f, -1.f}, {.5f, 1.f, -1.f},
    {0.f, 0.f,  0.f}, {.5f, 0.f,  0.f},
    {0.f, 1.f,  0.f}, {.5f, 1.f,  0.f},
};

static const struct shady_collision_vertex cube_right_vertices[] = {
    {.5f, 0.f, -1.f}, {1.f, 0.f, -1.f},
    {.5f, 1.f, -1.f}, {1.f, 1.f, -1.f},
    {.5f, 0.f,  0.f}, {1.f, 0.f,  0.f},
    {.5f, 1.f,  0.f}, {1.f, 1.f,  0.f},
};

static const uint16_t cube_hull_indices[] = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    0, 1, 4, 1, 5, 4,
    2, 6, 3, 3, 6, 7,
    0, 4, 2, 2, 4, 6,
    1, 3, 5, 3, 7, 5,
};

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

static bool cube_collision_hull(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_collision_hull *hull, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)state;
    (void)user_data;
    if (!hull_logged) {
        hull_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-cube: convex collision hull callback active");
    }
    hull->struct_size = sizeof(*hull);
    hull->vertices = cube_hull_vertices;
    hull->vertex_count = sizeof(cube_hull_vertices) / sizeof(cube_hull_vertices[0]);
    hull->indices = cube_hull_indices;
    hull->index_count = sizeof(cube_hull_indices) / sizeof(cube_hull_indices[0]);
    hull->revision = 1;
    return true;
}

static const struct shady_collision_hull cube_compound_parts[] = {
    {
        .struct_size = sizeof(struct shady_collision_hull),
        .vertices = cube_left_vertices,
        .vertex_count = sizeof(cube_left_vertices) / sizeof(cube_left_vertices[0]),
        .indices = cube_hull_indices,
        .index_count = sizeof(cube_hull_indices) / sizeof(cube_hull_indices[0]),
        .revision = 1,
    },
    {
        .struct_size = sizeof(struct shady_collision_hull),
        .vertices = cube_right_vertices,
        .vertex_count = sizeof(cube_right_vertices) / sizeof(cube_right_vertices[0]),
        .indices = cube_hull_indices,
        .index_count = sizeof(cube_hull_indices) / sizeof(cube_hull_indices[0]),
        .revision = 1,
    },
};

static bool cube_collision_compound(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_collision_compound *compound,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)state;
    (void)user_data;
    if (!compound_logged) {
        compound_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-cube: two-part compound collision callback active");
    }
    compound->struct_size = sizeof(*compound);
    compound->parts = cube_compound_parts;
    compound->part_count = sizeof(cube_compound_parts) / sizeof(cube_compound_parts[0]);
    compound->revision = 1;
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
    .collision_hull = cube_collision_hull,
    .collision_compound = cube_collision_compound,
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
