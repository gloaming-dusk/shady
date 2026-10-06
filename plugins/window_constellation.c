#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <shady/event.h>
#include <shady/plugin.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const struct shady_plugin_api_v1 *api;
static shady_host host;

enum constellation_mode {
    CONSTELLATION_IDLE = 0,
    CONSTELLATION_ORBITING = 1,
    CONSTELLATION_RESTORING = 2,
};

struct constellation_state {
    uint32_t mode;
    float phase;
    float restore_elapsed;
};

struct constellation_window_state {
    bool captured;
    double origin_x;
    double origin_y;
    float origin_z;
    float vx;
    float vy;
    float vz;
};

static struct constellation_state *state(void) {
    return api->module_state(host, "window-constellation");
}

static struct constellation_window_state *window_state(shady_window window) {
    return api->window_state(window, "window-constellation");
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

static void capture_window(shady_window window) {
    if (!managed(window)) return;
    struct constellation_window_state *ws = window_state(window);
    if (!ws || ws->captured) return;
    if (!api->window_position(window, &ws->origin_x, &ws->origin_y, &ws->origin_z))
        return;
    ws->captured = true;
    ws->vx = ws->vy = ws->vz = 0.f;
}

static void capture_all(void) {
    for (size_t i = 0; i < api->window_count(host); ++i)
        capture_window(api->window_at(host, i));
}

static void begin_orbit(void) {
    struct constellation_state *s = state();
    if (!s) return;
    capture_all();
    s->mode = CONSTELLATION_ORBITING;
    s->restore_elapsed = 0.f;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "window-constellation: orbital workspace enabled");
    api->schedule_render(host);
}

static void begin_restore(void) {
    struct constellation_state *s = state();
    if (!s) return;
    s->mode = CONSTELLATION_RESTORING;
    s->restore_elapsed = 0.f;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "window-constellation: restoring original layout");
    api->schedule_render(host);
}

static bool key(struct shady_server *server, const xkb_keysym_t *syms,
        int nsyms, uint32_t key_state, uint32_t modifiers) {
    (void)server;
    if (key_state != SHADY_KEY_PRESSED) return false;
    for (int i = 0; i < nsyms; ++i) {
        if ((modifiers & SHADY_MODIFIER_LOGO) &&
                (syms[i] == XKB_KEY_c || syms[i] == XKB_KEY_C)) {
            struct constellation_state *s = state();
            if (!s) return true;
            if (s->mode == CONSTELLATION_ORBITING)
                begin_restore();
            else
                begin_orbit();
            return true;
        }
    }
    return false;
}

static void spring_window(shady_window window,
        double target_x, double target_y, float target_z,
        float dt, bool *moving_out) {
    struct constellation_window_state *ws = window_state(window);
    if (!ws) return;

    double x = 0.0, y = 0.0;
    float z = 0.f;
    if (!api->window_position(window, &x, &y, &z))
        return;

    const float spring = 24.f;
    const float damping = expf(-9.f * dt);

    ws->vx = (ws->vx + (float)(target_x - x) * spring * dt) * damping;
    ws->vy = (ws->vy + (float)(target_y - y) * spring * dt) * damping;
    ws->vz = (ws->vz + (target_z - z) * spring * dt) * damping;

    ws->vx = clampf(ws->vx, -1200.f, 1200.f);
    ws->vy = clampf(ws->vy, -1200.f, 1200.f);
    ws->vz = clampf(ws->vz, -1.4f, 1.4f);

    if (fabs(target_x - x) < .35 && fabs(target_y - y) < .35 &&
            fabsf(target_z - z) < .0015f &&
            fabsf(ws->vx) < .08f && fabsf(ws->vy) < .08f &&
            fabsf(ws->vz) < .001f) {
        api->window_set_position(host, window, target_x, target_y, target_z);
        ws->vx = ws->vy = ws->vz = 0.f;
        return;
    }

    api->window_set_position(host, window,
        x + ws->vx * dt,
        y + ws->vy * dt,
        z + ws->vz * dt);
    *moving_out = true;
}

static void tick(struct shady_server *server, float dt,
        float logical_w, float logical_h) {
    (void)server;
    (void)logical_w;
    (void)logical_h;

    struct constellation_state *s = state();
    if (!s || s->mode == CONSTELLATION_IDLE) return;

    shady_window focused = api->focused_window(host);
    if (!managed(focused)) {
        for (size_t i = 0; i < api->window_count(host); ++i) {
            shady_window candidate = api->window_at(host, i);
            if (managed(candidate)) {
                focused = candidate;
                break;
            }
        }
    }
    if (!managed(focused)) return;

    int fw = 0, fh = 0;
    double fx = 0.0, fy = 0.0;
    float fz = 0.f;
    if (!api->window_size(focused, &fw, &fh) ||
            !api->window_position(focused, &fx, &fy, &fz))
        return;

    float anchor_x = (float)fx + fw * .5f;
    float anchor_y = (float)fy + fh * .5f;

    if (s->mode == CONSTELLATION_ORBITING)
        s->phase += dt * .38f;
    else if (s->mode == CONSTELLATION_RESTORING)
        s->restore_elapsed += dt;

    size_t orbit_count = 0;
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window window = api->window_at(host, i);
        if (managed(window) && window != focused)
            orbit_count++;
    }

    bool moving = false;
    size_t slot = 0;
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window window = api->window_at(host, i);
        if (!managed(window)) continue;
        struct constellation_window_state *ws = window_state(window);
        if (!ws) continue;

        if (!ws->captured)
            capture_window(window);

        if (s->mode == CONSTELLATION_RESTORING) {
            if (ws->captured)
                spring_window(window, ws->origin_x, ws->origin_y,
                    ws->origin_z, dt, &moving);
            continue;
        }

        if (window == focused) {
            ws->vx *= expf(-10.f * dt);
            ws->vy *= expf(-10.f * dt);
            ws->vz *= expf(-10.f * dt);
            continue;
        }

        int ww = 0, wh = 0;
        if (!api->window_size(window, &ww, &wh)) continue;

        float angle = s->phase;
        if (orbit_count > 0)
            angle += (float)(2.0 * M_PI) * ((float)slot / (float)orbit_count);
        slot++;

        float layer = (float)(slot % 3) - 1.f;
        float radius_x = 430.f + 45.f * layer;
        float radius_y = 235.f + 24.f * layer;
        float orbit_x = cosf(angle) * radius_x;
        float orbit_y = sinf(angle) * radius_y;
        float orbit_z = sinf(angle * 1.7f) * .24f +
            cosf(angle * .55f) * .07f;

        double target_x = anchor_x + orbit_x - ww * .5f;
        double target_y = anchor_y + orbit_y - wh * .5f;
        float target_z = fz + orbit_z;

        spring_window(window, target_x, target_y, target_z, dt, &moving);
    }

    if (s->mode == CONSTELLATION_RESTORING &&
            (!moving || s->restore_elapsed >= 3.f)) {
        for (size_t i = 0; i < api->window_count(host); ++i) {
            shady_window window = api->window_at(host, i);
            struct constellation_window_state *ws = window_state(window);
            if (!ws) continue;
            if (ws->captured && managed(window))
                api->window_set_position(host, window,
                    ws->origin_x, ws->origin_y, ws->origin_z);
            ws->captured = false;
            ws->vx = ws->vy = ws->vz = 0.f;
        }
        s->mode = CONSTELLATION_IDLE;
        api->log(SHADY_PLUGIN_LOG_INFO,
            "window-constellation: original layout restored");
        return;
    }

    api->schedule_render(host);
}

static void on_event(shady_host event_host,
        const struct shady_event *event, void *user_data) {
    (void)event_host;
    (void)user_data;
    struct constellation_state *s = state();
    if (!s || s->mode == CONSTELLATION_IDLE) return;

    if (event->type == SHADY_EVENT_WINDOW_MAPPED)
        capture_window(event->object.window);

    switch (event->type) {
    case SHADY_EVENT_WINDOW_MAPPED:
    case SHADY_EVENT_WINDOW_UNMAPPED:
    case SHADY_EVENT_WINDOW_DESTROYED:
    case SHADY_EVENT_WINDOW_FOCUSED:
    case SHADY_EVENT_WINDOW_RESIZED:
    case SHADY_EVENT_WINDOW_STATE_CHANGED:
        api->schedule_render(host);
        break;
    default:
        break;
    }
}

static void start(struct shady_server *server) {
    (void)server;
    struct constellation_state *s = state();
    if (!s) return;
    api->log(SHADY_PLUGIN_LOG_INFO,
        "window-constellation: Super+C toggles a focused-window orbital workspace");
}

static void stop(struct shady_server *server) {
    (void)server;
}

static const char *const provides[] = {
    "spatial.window-constellation",
    NULL,
};

static const char *const requires[] = {
    "spatial.window-state",
    NULL,
};

static const struct shady_module module = {
    .name = "window-constellation",
    .provides = provides,
    .requires = requires,
    .state_size = sizeof(struct constellation_state),
    .toplevel_state_size = sizeof(struct constellation_window_state),
    .start = start,
    .stop = stop,
    .key = key,
    .tick = tick,
};

static size_t module_snapshot_size(shady_host callback_host, const void *module_state) {
    (void)callback_host;
    (void)module_state;
    return sizeof(struct constellation_state);
}

static bool save_module_state(shady_host callback_host, const void *module_state,
        void *snapshot, size_t snapshot_size) {
    (void)callback_host;
    if (!module_state || !snapshot ||
            snapshot_size != sizeof(struct constellation_state))
        return false;
    memcpy(snapshot, module_state, sizeof(struct constellation_state));
    return true;
}

static bool restore_module_state(shady_host callback_host, void *module_state,
        const void *snapshot, size_t snapshot_size,
        uint32_t previous_schema_version) {
    (void)callback_host;
    if (previous_schema_version != 1 || !module_state || !snapshot ||
            snapshot_size != sizeof(struct constellation_state))
        return false;
    memcpy(module_state, snapshot, sizeof(struct constellation_state));
    return true;
}

static size_t window_snapshot_size(shady_host callback_host, shady_window window,
        const void *window_state_value) {
    (void)callback_host;
    (void)window;
    (void)window_state_value;
    return sizeof(struct constellation_window_state);
}

static bool save_window_state(shady_host callback_host, shady_window window,
        const void *window_state_value, void *snapshot, size_t snapshot_size) {
    (void)callback_host;
    (void)window;
    if (!window_state_value || !snapshot ||
            snapshot_size != sizeof(struct constellation_window_state))
        return false;
    memcpy(snapshot, window_state_value, sizeof(struct constellation_window_state));
    return true;
}

static bool restore_window_state(shady_host callback_host, shady_window window,
        void *window_state_value, const void *snapshot, size_t snapshot_size,
        uint32_t previous_schema_version) {
    (void)callback_host;
    (void)window;
    if (previous_schema_version != 1 || !window_state_value || !snapshot ||
            snapshot_size != sizeof(struct constellation_window_state))
        return false;
    memcpy(window_state_value, snapshot, sizeof(struct constellation_window_state));
    return true;
}

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
    api->subscribe_event(host, SHADY_EVENT_WINDOW_RESIZED, on_event, NULL);
    api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);

    return &plugin_v2;
}
