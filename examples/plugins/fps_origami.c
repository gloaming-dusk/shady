#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;
static bool mesh_logged;
static bool compound_logged;
static bool update_logged;

struct origami_state {
    struct shady_representation_vertex vertices[6];
    uint16_t mesh_indices[12];

    struct shady_collision_vertex left_hull[8];
    struct shady_collision_vertex right_hull[8];
    uint16_t hull_indices[36];
    struct shady_collision_hull hulls[2];

    float fold;
    float velocity;
    float target;
    uint64_t revision;
};

static const uint16_t box_indices[36] = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    0, 1, 4, 1, 5, 4,
    2, 6, 3, 3, 6, 7,
    0, 4, 2, 2, 4, 6,
    1, 3, 5, 3, 7, 5,
};

static void rebuild_geometry(struct origami_state *s) {
    float q = s->fold;
    float span = 0.5f * (1.f - 0.46f * q);
    float hinge_z = -0.10f;
    float edge_z = hinge_z - 0.72f * q;
    float left_x = 0.5f - span;
    float right_x = 0.5f + span;

    s->vertices[0] = (struct shady_representation_vertex){
        .x = left_x, .y = 0.f, .z = edge_z, .u = 0.f, .v = 0.f
    };
    s->vertices[1] = (struct shady_representation_vertex){
        .x = left_x, .y = 1.f, .z = edge_z, .u = 0.f, .v = 1.f
    };
    s->vertices[2] = (struct shady_representation_vertex){
        .x = 0.5f, .y = 0.f, .z = hinge_z, .u = 0.5f, .v = 0.f
    };
    s->vertices[3] = (struct shady_representation_vertex){
        .x = 0.5f, .y = 1.f, .z = hinge_z, .u = 0.5f, .v = 1.f
    };
    s->vertices[4] = (struct shady_representation_vertex){
        .x = right_x, .y = 0.f, .z = edge_z, .u = 1.f, .v = 0.f
    };
    s->vertices[5] = (struct shady_representation_vertex){
        .x = right_x, .y = 1.f, .z = edge_z, .u = 1.f, .v = 1.f
    };

    const uint16_t mesh_indices[12] = {
        0, 2, 1, 1, 2, 3,
        2, 4, 3, 3, 4, 5,
    };
    for (size_t i = 0; i < 12; ++i)
        s->mesh_indices[i] = mesh_indices[i];

    const float thickness = 0.075f;
    const float left_front[4][3] = {
        {left_x, 0.f, edge_z}, {0.5f, 0.f, hinge_z},
        {left_x, 1.f, edge_z}, {0.5f, 1.f, hinge_z},
    };
    const float right_front[4][3] = {
        {0.5f, 0.f, hinge_z}, {right_x, 0.f, edge_z},
        {0.5f, 1.f, hinge_z}, {right_x, 1.f, edge_z},
    };

    for (int i = 0; i < 4; ++i) {
        s->left_hull[i] = (struct shady_collision_vertex){
            left_front[i][0], left_front[i][1], left_front[i][2]
        };
        s->left_hull[i + 4] = (struct shady_collision_vertex){
            left_front[i][0], left_front[i][1], left_front[i][2] - thickness
        };
        s->right_hull[i] = (struct shady_collision_vertex){
            right_front[i][0], right_front[i][1], right_front[i][2]
        };
        s->right_hull[i + 4] = (struct shady_collision_vertex){
            right_front[i][0], right_front[i][1], right_front[i][2] - thickness
        };
    }

    for (size_t i = 0; i < 36; ++i)
        s->hull_indices[i] = box_indices[i];

    s->revision++;
    s->hulls[0] = (struct shady_collision_hull){
        .struct_size = sizeof(struct shady_collision_hull),
        .vertices = s->left_hull,
        .vertex_count = 8,
        .indices = s->hull_indices,
        .index_count = 36,
        .revision = s->revision,
    };
    s->hulls[1] = (struct shady_collision_hull){
        .struct_size = sizeof(struct shady_collision_hull),
        .vertices = s->right_hull,
        .vertex_count = 8,
        .indices = s->hull_indices,
        .index_count = 36,
        .revision = s->revision,
    };
}

static bool origami_state_init(shady_host callback_host, shady_window window,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct origami_state *s = state;
    if (!s) return false;

    s->fold = 0.16f;
    s->velocity = 0.f;
    s->target = 0.16f;
    s->revision = 0;
    rebuild_geometry(s);
    return true;
}

static bool origami_update(shady_host callback_host, shady_window window,
        float dt, void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)user_data;
    struct origami_state *s = state;
    if (!s) return false;
    if (!update_logged) {
        update_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-origami: spring update active");
    }

    float diff = s->target - s->fold;
    float acceleration = diff * 92.f - s->velocity * 14.f;
    s->velocity += acceleration * dt;
    s->fold += s->velocity * dt;

    if (s->fold < 0.05f) {
        s->fold = 0.05f;
        s->velocity *= -0.25f;
    } else if (s->fold > 0.92f) {
        s->fold = 0.92f;
        s->velocity *= -0.25f;
    }

    float abs_diff = diff < 0.f ? -diff : diff;
    float abs_velocity = s->velocity < 0.f ? -s->velocity : s->velocity;
    bool moving = abs_diff > 0.0015f || abs_velocity > 0.003f;
    if (moving)
        rebuild_geometry(s);
    else {
        s->fold = s->target;
        s->velocity = 0.f;
    }
    return moving;
}

static bool origami_model(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_representation_model *model,
        void *state, void *user_data) {
    (void)window;
    (void)user_data;
    struct origami_state *s = state;
    if (!s) return false;

    float target = context->held ? 0.84f :
        (context->focused ? 0.46f : 0.16f);
    if (target != s->target) {
        s->target = target;
        api->schedule_render(callback_host);
    }

    model->width = 0.25f;
    model->height = 0.17f;
    model->depth = 0.16f;
    model->hide_titlebar = true;
    return true;
}

static bool origami_mesh(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_representation_mesh *mesh,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)user_data;
    struct origami_state *s = state;
    if (!s || !mesh) return false;

    if (!mesh_logged) {
        mesh_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-origami: hinged deformable mesh active");
    }

    mesh->struct_size = sizeof(*mesh);
    mesh->vertices = s->vertices;
    mesh->vertex_count = 6;
    mesh->indices = s->mesh_indices;
    mesh->index_count = 12;
    mesh->revision = s->revision;
    return true;
}

static bool origami_compound(shady_host callback_host, shady_window window,
        const struct shady_representation_context *context,
        struct shady_collision_compound *compound,
        void *state, void *user_data) {
    (void)callback_host;
    (void)window;
    (void)context;
    (void)user_data;
    struct origami_state *s = state;
    if (!s || !compound) return false;

    if (!compound_logged) {
        compound_logged = true;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "fps-origami: two-panel compound collision active");
    }

    compound->struct_size = sizeof(*compound);
    compound->parts = s->hulls;
    compound->part_count = 2;
    compound->revision = s->revision;
    return true;
}

static const struct shady_window_representation_provider origami_provider = {
    .struct_size = sizeof(struct shady_window_representation_provider),
    .base = {
        .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_MESH,
        .width = 0.25f,
        .height = 0.17f,
        .depth = 0.16f,
        .hide_titlebar = true,
    },
    .state_size = sizeof(struct origami_state),
    .state_init = origami_state_init,
    .update = origami_update,
    .model = origami_model,
    .mesh = origami_mesh,
    .collision_compound = origami_compound,
};

static void apply_window(shady_window window) {
    if (window && api->window_valid(host, window))
        api->window_set_representation_provider(host, window, &origami_provider);
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
        "fps-origami: spring-driven hinged windows active");
}

static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window window = api->window_at(host, i);
        if (api->window_valid(host, window))
            api->window_reset_representation_provider(
                host, window, &origami_provider);
    }
}

static const char *const provides[] = {
    "spatial.window-representation.origami",
    NULL,
};

static const char *const requires[] = {
    "spatial.fps",
    NULL,
};

static const struct shady_module module = {
    .name = "fps-origami",
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
