#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

struct magnetic_state {
    bool active;
    bool moving;
};

struct magnetic_window_state {
    float vx;
    float vy;
    float vz;
    bool bonded;
};

static struct magnetic_state *state(void) {
    return api->module_state(host, "magnetic-windows");
}

static struct magnetic_window_state *window_state(shady_window window) {
    return api->window_state(window, "magnetic-windows");
}

static bool managed(shady_window window) {
    return window &&
        api->window_valid(host, window) &&
        api->window_mapped(window) &&
        api->window_visible(window) &&
        !api->window_maximized(window) &&
        !api->window_fullscreen(window);
}

static float clampf(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void clear_velocity(void) {
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window window = api->window_at(host, i);
        if (!managed(window)) continue;
        struct magnetic_window_state *ws = window_state(window);
        if (!ws) continue;
        ws->vx = 0.f;
        ws->vy = 0.f;
        ws->vz = 0.f;
        ws->bonded = false;
    }
}

static void set_active(bool active) {
    struct magnetic_state *s = state();
    if (!s) return;
    s->active = active;
    s->moving = active;
    if (!active) clear_velocity();
    api->log(SHADY_PLUGIN_LOG_INFO,
        active ? "magnetic-windows: magnetic docking enabled"
               : "magnetic-windows: magnetic docking disabled");
    api->schedule_render(host);
}

static void on_event(shady_host event_host,
        const struct shady_event *event, void *user_data) {
    (void)event_host;
    (void)user_data;

    switch (event->type) {
    case SHADY_EVENT_WINDOW_MAPPED:
    case SHADY_EVENT_WINDOW_UNMAPPED:
    case SHADY_EVENT_WINDOW_DESTROYED:
    case SHADY_EVENT_WINDOW_FOCUSED:
    case SHADY_EVENT_WINDOW_STATE_CHANGED:
        if (state() && state()->active) {
            state()->moving = true;
            api->schedule_render(host);
        }
        break;
    default:
        break;
    }
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
        int nsyms, uint32_t key_state, uint32_t modifiers) {
    (void)server;
    if (key_state != SHADY_KEY_PRESSED)
        return false;

    for (int i = 0; i < nsyms; ++i) {
        if ((modifiers & SHADY_MODIFIER_LOGO) &&
                (syms[i] == XKB_KEY_m || syms[i] == XKB_KEY_M)) {
            struct magnetic_state *s = state();
            if (s) set_active(!s->active);
            return true;
        }
    }
    return false;
}

static void add_force(struct magnetic_window_state *ws,
        float fx, float fy, float fz, float weight) {
    if (!ws) return;
    ws->vx += fx * weight;
    ws->vy += fy * weight;
    ws->vz += fz * weight;
    ws->bonded = true;
}

static void tick(struct shady_server *server, float dt,
        float logical_w, float logical_h) {
    (void)server;
    (void)logical_w;
    (void)logical_h;

    struct magnetic_state *s = state();
    if (!s || !s->active)
        return;

    size_t count = api->window_count(host);
    if (count < 2)
        return;

    const float attract_radius = 560.f;
    const float release_radius = 680.f;
    const float planar_spring = 18.f;
    const float depth_spring = 7.f;
    const float damping = expf(-8.5f * dt);
    const float max_speed = 900.f;
    const float max_z_speed = 0.75f;
    const float edge_gap = 28.f;

    shady_window focused = api->focused_window(host);

    for (size_t i = 0; i < count; ++i) {
        shady_window window = api->window_at(host, i);
        if (!managed(window)) continue;
        struct magnetic_window_state *ws = window_state(window);
        if (!ws) continue;
        ws->bonded = false;
        ws->vx *= damping;
        ws->vy *= damping;
        ws->vz *= damping;
    }

    for (size_t i = 0; i < count; ++i) {
        shady_window a = api->window_at(host, i);
        if (!managed(a)) continue;

        int aw = 0, ah = 0;
        double ax = 0.0, ay = 0.0;
        float az = 0.f;
        if (!api->window_size(a, &aw, &ah) ||
                !api->window_position(a, &ax, &ay, &az))
            continue;

        float acx = (float)ax + aw * .5f;
        float acy = (float)ay + ah * .5f;

        for (size_t j = i + 1; j < count; ++j) {
            shady_window b = api->window_at(host, j);
            if (!managed(b)) continue;

            int bw = 0, bh = 0;
            double bx = 0.0, by = 0.0;
            float bz = 0.f;
            if (!api->window_size(b, &bw, &bh) ||
                    !api->window_position(b, &bx, &by, &bz))
                continue;

            float bcx = (float)bx + bw * .5f;
            float bcy = (float)by + bh * .5f;
            float dx = bcx - acx;
            float dy = bcy - acy;
            float dz = bz - az;
            float planar_distance = sqrtf(dx * dx + dy * dy);

            struct magnetic_window_state *as = window_state(a);
            struct magnetic_window_state *bs = window_state(b);
            if (!as || !bs) continue;

            float radius = (as->bonded || bs->bonded) ?
                release_radius : attract_radius;
            if (planar_distance > radius || fabsf(dz) > 0.45f)
                continue;

            bool horizontal = fabsf(dx) >= fabsf(dy);
            float target_dx = 0.f;
            float target_dy = 0.f;
            if (horizontal) {
                float rest = (aw + bw) * .5f + edge_gap;
                target_dx = dx < 0.f ? -rest : rest;
            } else {
                float rest = (ah + bh) * .5f + edge_gap;
                target_dy = dy < 0.f ? -rest : rest;
            }

            float error_x = dx - target_dx;
            float error_y = dy - target_dy;
            float error_z = dz;

            float fx = error_x * planar_spring * dt;
            float fy = error_y * planar_spring * dt;
            float fz = error_z * depth_spring * dt;

            float a_weight = a == focused ? 0.16f : 0.5f;
            float b_weight = b == focused ? 0.16f : 0.5f;

            add_force(as, fx, fy, fz, a_weight);
            add_force(bs, -fx, -fy, -fz, b_weight);
        }
    }

    bool moving = false;
    for (size_t i = 0; i < count; ++i) {
        shady_window window = api->window_at(host, i);
        if (!managed(window)) continue;
        struct magnetic_window_state *ws = window_state(window);
        if (!ws) continue;

        if (!ws->bonded) {
            ws->vx *= damping;
            ws->vy *= damping;
            ws->vz *= damping;
        }

        ws->vx = clampf(ws->vx, -max_speed, max_speed);
        ws->vy = clampf(ws->vy, -max_speed, max_speed);
        ws->vz = clampf(ws->vz, -max_z_speed, max_z_speed);

        if (fabsf(ws->vx) < .02f && fabsf(ws->vy) < .02f &&
                fabsf(ws->vz) < .0002f) {
            ws->vx = 0.f;
            ws->vy = 0.f;
            ws->vz = 0.f;
            continue;
        }

        double x = 0.0, y = 0.0;
        float z = 0.f;
        if (!api->window_position(window, &x, &y, &z))
            continue;

        float step_x = ws->vx * dt;
        float step_y = ws->vy * dt;
        float step_z = ws->vz * dt;
        api->window_set_position(host, window,
            x + step_x, y + step_y, z + step_z);
        moving = true;
    }

    s->moving = moving;
    if (moving)
        api->schedule_render(host);
}

static void start(struct shady_server *server) {
    (void)server;
    struct magnetic_state *s = state();
    if (!s) return;
    s->active = true;
    s->moving = true;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "magnetic-windows: nearby windows attract and dock (Super+M toggles)");
    api->schedule_render(host);
}

static void stop(struct shady_server *server) {
    (void)server;
    clear_velocity();
}

static const char *const provides[] = {
    "spatial.magnetic-windows",
    NULL,
};

static const char *const requires[] = {
    "spatial.window-state",
    NULL,
};

static size_t module_snapshot_size(shady_host callback_host, const void *module_state) {
    (void)callback_host;
    (void)module_state;
    return sizeof(struct magnetic_state);
}

static bool save_module_state(shady_host callback_host, const void *module_state,
        void *snapshot, size_t snapshot_size) {
    (void)callback_host;
    if (!module_state || !snapshot || snapshot_size != sizeof(struct magnetic_state))
        return false;
    memcpy(snapshot, module_state, sizeof(struct magnetic_state));
    return true;
}

static bool restore_module_state(shady_host callback_host, void *module_state,
        const void *snapshot, size_t snapshot_size,
        uint32_t previous_schema_version) {
    (void)callback_host;
    if (previous_schema_version != 1 || !module_state || !snapshot ||
            snapshot_size != sizeof(struct magnetic_state))
        return false;
    memcpy(module_state, snapshot, sizeof(struct magnetic_state));
    return true;
}

static size_t window_snapshot_size(shady_host callback_host, shady_window window,
        const void *window_state_value) {
    (void)callback_host;
    (void)window;
    (void)window_state_value;
    return sizeof(struct magnetic_window_state);
}

static bool save_window_state(shady_host callback_host, shady_window window,
        const void *window_state_value, void *snapshot, size_t snapshot_size) {
    (void)callback_host;
    (void)window;
    if (!window_state_value || !snapshot ||
            snapshot_size != sizeof(struct magnetic_window_state))
        return false;
    memcpy(snapshot, window_state_value, sizeof(struct magnetic_window_state));
    return true;
}

static bool restore_window_state(shady_host callback_host, shady_window window,
        void *window_state_value, const void *snapshot, size_t snapshot_size,
        uint32_t previous_schema_version) {
    (void)callback_host;
    (void)window;
    if (previous_schema_version != 1 || !window_state_value || !snapshot ||
            snapshot_size != sizeof(struct magnetic_window_state))
        return false;
    memcpy(window_state_value, snapshot, sizeof(struct magnetic_window_state));
    return true;
}

static const struct shady_module module = {
    .name = "magnetic-windows",
    .provides = provides,
    .requires = requires,
    .state_size = sizeof(struct magnetic_state),
    .toplevel_state_size = sizeof(struct magnetic_window_state),
    .start = start,
    .stop = stop,
    .key = key,
    .tick = tick,
};

static const struct shady_plugin_v2 plugin_v2 = {
    .struct_size = sizeof(struct shady_plugin_v2),
    .module = &module,
    .state_schema_version = 1,
    .module_snapshot_size = module_snapshot_size,
    .save_module_state = save_module_state,
    .restore_module_state = restore_module_state,
    .window_snapshot_size = window_snapshot_size,
    .save_window_state = save_window_state,
    .restore_window_state = restore_window_state,
};

const struct shady_plugin_v2 *shady_plugin_entry_v2(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V2 || !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size < sizeof(*host_api))
        return NULL;

    api = host_api;
    host = host_handle;

    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);

    return &plugin_v2;
}

const struct shady_module *shady_plugin_entry_v1(
        uint32_t host_abi,
        const struct shady_plugin_api_v1 *host_api,
        shady_host host_handle) {
    if (host_abi != SHADY_PLUGIN_ABI_V1 ||
            !host_api ||
            host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
            host_api->struct_size < sizeof(*host_api) ||
            !host_api->window_position ||
            !host_api->window_set_position ||
            !host_api->window_state ||
            !host_api->module_state)
        return NULL;

    api = host_api;
    host = host_handle;

    api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);

    return &module;
}
