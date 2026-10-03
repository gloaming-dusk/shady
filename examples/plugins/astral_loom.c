/* Astral Loom: live curved panels and a procedural energy observatory. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shady/event.h>
#include <shady/plugin.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define N 16
#define SIDE (N + 1)
#define TAU 6.28318530718f
static const struct shady_plugin_api_v1 *api;
static const struct shady_representation_api_v1 *rep;
static shady_host host;
static shady_shader_program sky, glass;
static shady_render_hook_id hook;
static bool sky_logged;
struct loom_state { bool active, restoring, paused, helix, initialized; float time, restore_time; };
struct window_state { bool captured, origin_valid; double x, y; float z; };
struct window_state_v1 { bool captured; double x, y; float z; };
struct panel_state {
    struct shady_representation_vertex vertices[SIDE * SIDE];
    uint16_t indices[N * N * 6];
    uint64_t revision;
    float time, bend;
};
static struct loom_state *state(void) { return api->module_state(host, "astral-loom"); }
static struct window_state *ws(shady_window w) { return api->window_state(w, "astral-loom"); }
static bool managed(shady_window w) {
    return w && api->window_valid(host, w) && api->window_mapped(w) &&
        api->window_visible(w) && !api->window_maximized(w) && !api->window_fullscreen(w);
}
static void rebuild(struct panel_state *s) {
    for (int y = 0; y <= N; ++y) for (int x = 0; x <= N; ++x) {
        float u = (float)x / N, v = (float)y / N;
        float angle = (u - .5f) * s->bend;
        float curve = s->bend > .001f ? sinf(angle) / s->bend : u - .5f;
        s->vertices[y * SIDE + x] = (struct shady_representation_vertex){
            .x = .5f + curve, .y = v,
            .z = -.5f + (1.f - cosf(angle)) * 2.2f +
                .035f * sinf(v * TAU + s->time) * sinf(u * 3.14159265f),
            .u = u, .v = v,
        };
    }
    ++s->revision;
}
static bool panel_init(shady_host h, shady_window w, void *value, void *user) {
    (void)h; (void)w; (void)user;
    struct panel_state *s = value;
    if (!s) return false;
    s->bend = 1.8f;
    size_t k = 0;
    for (int y = 0; y < N; ++y) for (int x = 0; x < N; ++x) {
        uint16_t a = y * SIDE + x, b = a + 1, c = a + SIDE, d = c + 1;
        s->indices[k++] = a; s->indices[k++] = b; s->indices[k++] = c;
        s->indices[k++] = c; s->indices[k++] = b; s->indices[k++] = d;
    }
    rebuild(s);
    return true;
}
static bool panel_update(shady_host h, shady_window w, float dt, void *value, void *user) {
    (void)h; (void)user;
    struct panel_state *s = value;
    struct loom_state *l = state();
    float target = api->focused_window(host) == w ? .08f : 1.8f;
    float next = target + (s->bend - target) * expf(-8.f * fminf(dt, .05f));
    bool settling = fabsf(next - s->bend) > .0001f;
    if (l && l->paused && !settling) return false;
    s->bend = next;
    if (!l || !l->paused) s->time += fminf(dt, .05f);
    rebuild(s);
    return true;
}
static bool panel_model(shady_host h, shady_window w,
        const struct shady_representation_context *ctx,
        struct shady_representation_model *model, void *value, void *user) {
    (void)h; (void)w; (void)value; (void)user;
    float height = ctx->focused ? .36f : .26f;
    model->height = height;
    model->width = height * ctx->window_width / fmaxf(ctx->window_height, 1.f);
    model->depth = .12f;
    model->tilt_y = ctx->focused ? 0.f : -.45f * sinf(state()->time * .3f + ctx->center_x * 3.f);
    model->tilt_x = ctx->focused ? 0.f : .12f;
    return true;
}
static bool panel_mesh(shady_host h, shady_window w,
        const struct shady_representation_context *ctx,
        struct shady_representation_mesh *mesh, void *value, void *user) {
    (void)h; (void)w; (void)ctx; (void)user;
    struct panel_state *s = value;
    *mesh = (struct shady_representation_mesh){
        .struct_size = sizeof(*mesh), .vertices = s->vertices,
        .vertex_count = SIDE * SIDE, .indices = s->indices,
        .index_count = N * N * 6, .revision = s->revision,
    };
    return true;
}
static const struct shady_window_representation_provider panel = {
    .struct_size = sizeof(panel),
    .base = { .struct_size = sizeof(struct shady_window_representation),
        .kind = SHADY_WINDOW_REPRESENTATION_MESH, .width = .42f,
        .height = .26f, .depth = .12f, .hide_titlebar = true },
    .state_size = sizeof(struct panel_state), .state_init = panel_init,
    .update = panel_update, .model = panel_model, .mesh = panel_mesh,
};
static void capture(shady_window w) {
    if (!managed(w)) return;
    struct window_state *s = ws(w);
    if (!s) return;
    if (!s->origin_valid) {
        if (!api->window_position(w, &s->x, &s->y, &s->z)) return;
        s->origin_valid = true;
    }
    if (s->captured) return;
    if (!rep->attach_provider(host, w, &panel)) return;
    if (!api->window_set_shader(host, w, glass)) {
        rep->detach_provider(host, w, &panel);
        return;
    }
    s->captured = true;
}
static void release(shady_window w, bool restore, bool forget_origin) {
    struct window_state *s = ws(w);
    if (!s) return;
    if (restore && s->origin_valid)
        api->window_set_position(host, w, s->x, s->y, s->z);
    if (s->captured) {
        rep->detach_provider(host, w, &panel);
        api->window_reset_shader(host, w);
        s->captured = false;
    }
    if (forget_origin) s->origin_valid = false;
}
static void draw(shady_host h, const struct shady_render_context *ctx, void *user) {
    (void)user;
    if (!sky || !ctx) return;
    api->shader_uniform_float(h, sky, "u_time", state()->time);
    api->shader_uniform_vec2(h, sky, "u_resolution", ctx->width, ctx->height);
    bool drawn = api->shader_draw_fullscreen(h, sky);
    if (!sky_logged) {
        api->log(drawn ? SHADY_PLUGIN_LOG_INFO : SHADY_PLUGIN_LOG_ERROR,
            drawn ? "astral-loom: energy sky rendered" : "astral-loom: energy sky draw failed");
        sky_logged = true;
    }
}
static bool init(struct shady_server *server) {
    (void)server;
    const char *root = getenv("SHADY_ROOT");
    if (!root) root = ".";
    char v[1024], f[1024];
    snprintf(v, sizeof(v), "%s/examples/plugins/shaders/overlay.vert", root);
    snprintf(f, sizeof(f), "%s/examples/plugins/shaders/astral_sky.frag", root);
    sky = api->shader_program_create(host, v, f);
    snprintf(v, sizeof(v), "%s/examples/plugins/shaders/astral_panel.vert", root);
    snprintf(f, sizeof(f), "%s/examples/plugins/shaders/astral_panel.frag", root);
    glass = api->shader_program_create(host, v, f);
    if (sky && glass) hook = api->render_hook_add(host, SHADY_RENDER_STAGE_AFTER_BACKGROUND, draw, NULL);
    if (!sky || !glass || !hook) {
        if (sky) api->shader_program_destroy(host, sky);
        if (glass) api->shader_program_destroy(host, glass);
        sky = glass = 0;
        api->log(SHADY_PLUGIN_LOG_ERROR, "astral-loom: shader initialization failed");
        return false;
    }
    return true;
}
static void start(struct shady_server *server) {
    (void)server;
    if (!state()->initialized) {
        state()->active = true;
        state()->initialized = true;
    }
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window w = api->window_at(host, i);
        struct window_state *saved = ws(w);
        /* Provider/shader ownership never survives a shared-object reload even
         * when the logical per-window snapshot says it was captured. */
        if (saved) saved->captured = false;
        if (state()->active || state()->restoring) capture(w);
    }
    api->log(SHADY_PLUGIN_LOG_INFO, "astral-loom: ready; Super+j layout, Super+Shift+j helix, Super+B pause");
    api->schedule_render(host);
}
static void stop(struct shady_server *server) {
    (void)server;
    for (size_t i = 0; i < api->window_count(host); ++i)
        release(api->window_at(host, i), true, false);
}
static void destroy(struct shady_server *server) {
    (void)server;
    if (hook) api->render_hook_remove(host, hook);
    if (sky) api->shader_program_destroy(host, sky);
    if (glass) api->shader_program_destroy(host, glass);
    hook = 0; sky = glass = 0;
}
static bool key(struct shady_server *server, const xkb_keysym_t *syms,
        int n, uint32_t pressed, uint32_t mods) {
    (void)server;
    if (pressed != SHADY_KEY_PRESSED || !(mods & SHADY_MODIFIER_LOGO) ||
        (mods & (SHADY_MODIFIER_CTRL | SHADY_MODIFIER_ALT))) return false;
    struct loom_state *s = state();
    for (int i = 0; i < n; ++i) {
        if ((syms[i] == XKB_KEY_j || syms[i] == XKB_KEY_J)) {
            if (mods & SHADY_MODIFIER_SHIFT) s->helix = !s->helix;
            else {
                s->active = !s->active;
                s->restoring = !s->active;
                s->restore_time = 0.f;
                if (s->active) for (size_t j = 0; j < api->window_count(host); ++j)
                    capture(api->window_at(host, j));
            }
            api->schedule_render(host);
            return true;
        }
        if ((syms[i] == XKB_KEY_b || syms[i] == XKB_KEY_B) && !(mods & SHADY_MODIFIER_SHIFT)) {
            s->paused = !s->paused;
            api->schedule_render(host);
            return true;
        }
    }
    return false;
}
static void tick(struct shady_server *server, float dt, float width, float height) {
    (void)server;
    struct loom_state *s = state();
    dt = fminf(fmaxf(dt, 0.f), .05f);
    if (!s->paused) s->time = fmodf(s->time + dt, 1200.f);
    if (s->restoring) s->restore_time += dt;
    size_t count = 0;
    shady_window focused = api->focused_window(host);
    for (size_t i = 0; i < api->window_count(host); ++i)
        if (managed(api->window_at(host, i)) && api->window_at(host, i) != focused) ++count;
    size_t slot = 0;
    for (size_t i = 0; i < api->window_count(host); ++i) {
        shady_window w = api->window_at(host, i);
        bool restore_done = s->restoring && s->restore_time >= 1.5f;
        if (!managed(w)) {
            release(w, true, restore_done || (!s->active && !s->restoring));
            continue;
        }
        if (s->active) capture(w);
        struct window_state *saved = ws(w);
        if (!saved || !saved->origin_valid) continue;
        double x, y; float z; int ww, wh;
        if (!api->window_position(w, &x, &y, &z) || !api->window_size(w, &ww, &wh)) continue;
        double tx = saved->x, ty = saved->y; float tz = saved->z;
        if (s->active) {
            float px = 0.f, py = .04f;
            tz = -.08f;
            if (w != focused) {
                float a = TAU * (float)slot / fmaxf((float)count, 1.f) + s->time * .12f;
                float radius = fminf(width / fmaxf(height, 1.f) * .32f, .60f);
                px = cosf(a) * radius;
                py = sinf(a) * .27f;
                tz = -.38f - .22f * (1.f + sinf(a));
                if (s->helix) { py = ((float)slot / fmaxf((float)count - 1.f, 1.f) - .5f) * .65f; tz -= .12f; }
                ++slot;
            }
            tx = width * .5f + px * height - ww * .5f;
            ty = height * .5f - py * height - wh * .5f;
        }
        float blend = 1.f - expf(-6.f * dt);
        api->window_set_position(host, w, x + (tx - x) * blend, y + (ty - y) * blend, z + (tz - z) * blend);
        if (restore_done) release(w, true, true);
    }
    if (s->restoring && s->restore_time >= 1.5f) s->restoring = false;
    if (!s->paused || s->active || s->restoring) api->schedule_render(host);
}
static const char *const provides[] = { "spatial.astral-loom", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };
static const struct shady_module module = {
    .name = "astral-loom", .provides = provides, .requires = requires,
    .state_size = sizeof(struct loom_state), .toplevel_state_size = sizeof(struct window_state),
    .init = init, .start = start, .stop = stop, .destroy = destroy, .key = key, .tick = tick,
};

static size_t module_size(shady_host h, const void *value) {
    (void)h; (void)value; return sizeof(struct loom_state);
}
static bool save_module(shady_host h, const void *value, void *snapshot, size_t size) {
    (void)h;
    if (!value || !snapshot || size != sizeof(struct loom_state)) return false;
    memcpy(snapshot, value, size); return true;
}
static bool restore_module(shady_host h, void *value, const void *snapshot,
        size_t size, uint32_t schema) {
    (void)h;
    if ((schema != 1 && schema != 2) || !value || !snapshot ||
            size != sizeof(struct loom_state)) return false;
    memcpy(value, snapshot, size); return true;
}
static size_t window_size(shady_host h, shady_window w, const void *value) {
    (void)h; (void)w; (void)value; return sizeof(struct window_state);
}
static bool save_window(shady_host h, shady_window w, const void *value,
        void *snapshot, size_t size) {
    (void)h; (void)w;
    if (!value || !snapshot || size != sizeof(struct window_state)) return false;
    memcpy(snapshot, value, size); return true;
}
static bool restore_window(shady_host h, shady_window w, void *value,
        const void *snapshot, size_t size, uint32_t schema) {
    (void)h; (void)w;
    if (!value || !snapshot) return false;
    struct window_state *out = value;
    if (schema == 1 && size == sizeof(struct window_state_v1)) {
        const struct window_state_v1 *old = snapshot;
        *out = (struct window_state){
            .captured = old->captured,
            .origin_valid = old->captured,
            .x = old->x, .y = old->y, .z = old->z,
        };
        return true;
    }
    if (schema != 2 || size != sizeof(struct window_state)) return false;
    memcpy(out, snapshot, size); return true;
}
static const struct shady_plugin_v2 plugin = {
    .struct_size = sizeof(plugin), .module = &module, .state_schema_version = 2,
    .module_snapshot_size = module_size, .save_module_state = save_module,
    .restore_module_state = restore_module, .window_snapshot_size = window_size,
    .save_window_state = save_window, .restore_window_state = restore_window,
};
const struct shady_plugin_v2 *shady_plugin_entry_v2(uint32_t abi,
        const struct shady_plugin_api_v1 *table, shady_host h) {
    if (abi != SHADY_PLUGIN_ABI_V2 || !table || table->abi_version != SHADY_PLUGIN_ABI_V1 ||
        !SHADY_API_HAS(table, query_api) || !SHADY_API_HAS(table, window_set_shader) ||
        !SHADY_API_HAS(table, render_hook_add)) return NULL;
    rep = table->query_api(h, SHADY_REPRESENTATION_API, SHADY_REPRESENTATION_API_VERSION);
    if (!rep || rep->struct_size < sizeof(*rep) || !rep->attach_provider || !rep->detach_provider) return NULL;
    api = table; host = h;
    return &plugin;
}
