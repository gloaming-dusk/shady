/* Drive the black-hole plugin against a fake host with fake windows:
 * swallow -> windows parked on the hidden workspace with shaders released,
 * emit -> windows back on the current workspace, and unload mid-animation
 * releases everything. Uses only the public plugin API. */
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <shady/event.h>
#include <shady/plugin.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define WINDOWS 3

struct fake_window {
    double x, y;
    float z;
    int w, h;
    char workspace[32];
    shady_shader_program shader;
    float params[SHADY_WINDOW_SHADER_PARAMS * 4];
    int param_updates;
};

static int host_object;
#define HOST ((shady_host)&host_object)
static struct fake_window windows[WINDOWS];
static char current[32] = "1";
static shady_render_callback hooks[4];
static int hook_count, draws;

static struct fake_window *W(shady_window w) { return (struct fake_window *)w; }
static size_t count(shady_host h) { assert(h == HOST); return WINDOWS; }
static shady_window at(shady_host h, size_t i) { assert(h == HOST && i < WINDOWS); return (shady_window)&windows[i]; }
static bool valid(shady_host h, shady_window w) { (void)h; return w != NULL; }
static bool mapped(shady_window w) { (void)w; return true; }
static bool visible(shady_window w) { return !strcmp(W(w)->workspace, current); }
static bool position(shady_window w, double *x, double *y, float *z) {
    *x = W(w)->x; *y = W(w)->y; *z = W(w)->z; return true;
}
static bool size(shady_window w, int *wd, int *ht) { *wd = W(w)->w; *ht = W(w)->h; return true; }
static const char *current_workspace(shady_host h) { (void)h; return current; }
static bool move_to(shady_host h, shady_window w, const char *name) {
    (void)h; snprintf(W(w)->workspace, sizeof(W(w)->workspace), "%s", name); return true;
}
static bool set_shader(shady_host h, shady_window w, shady_shader_program p) {
    (void)h; W(w)->shader = p; memset(W(w)->params, 0, sizeof(W(w)->params)); return true;
}
static bool reset_shader(shady_host h, shady_window w) { (void)h; W(w)->shader = 0; return true; }
static bool set_params(shady_host h, shady_window w, const float *p) {
    (void)h;
    if (!W(w)->shader) return false;
    for (int i = 0; i < SHADY_WINDOW_SHADER_PARAMS * 4; i++) assert(isfinite(p[i]));
    memcpy(W(w)->params, p, sizeof(W(w)->params));
    W(w)->param_updates++;
    return true;
}
static shady_shader_program program_create(shady_host h, const char *v, const char *f) {
    (void)h; assert(strstr(v, ".vert") && strstr(f, ".frag")); return 42;
}
static bool program_destroy(shady_host h, shady_shader_program p) { (void)h; return p == 42; }
static shady_render_hook_id hook_add(shady_host h, uint32_t stage, shady_render_callback cb, void *u) {
    (void)h; (void)stage; (void)u; hooks[hook_count] = cb; return (shady_render_hook_id)++hook_count;
}
static bool hook_remove(shady_host h, shady_render_hook_id id) { (void)h; (void)id; return true; }
static bool uniform_f(shady_host h, shady_shader_program p, const char *n, float v) { (void)h; (void)p; (void)n; (void)v; return true; }
static bool uniform_2(shady_host h, shady_shader_program p, const char *n, float x, float y) { (void)h; (void)p; (void)n; (void)x; (void)y; return true; }
static bool uniform_4(shady_host h, shady_shader_program p, const char *n, float x, float y, float z, float w) { (void)h; (void)p; (void)n; (void)x; (void)y; (void)z; (void)w; return true; }
static bool draw_scene(shady_host h, shady_shader_program p) { (void)h; (void)p; draws++; return true; }
static void schedule(shady_host h) { (void)h; }
static void log_msg(enum shady_plugin_log_level l, const char *m) { (void)l; fprintf(stderr, "[plugin] %s\n", m); }
static bool subscribe(shady_host h, uint32_t t, shady_event_callback cb, void *u) { (void)h; (void)t; (void)cb; (void)u; return true; }

static struct shady_plugin_api_v1 api = {
    .abi_version = SHADY_PLUGIN_ABI_V1, .struct_size = sizeof(api), .log = log_msg,
    .window_count = count, .window_at = at, .window_valid = valid, .window_mapped = mapped,
    .window_visible = visible, .window_position = position, .window_size = size,
    .current_workspace = current_workspace, .window_move_to_workspace = move_to,
    .window_set_shader = set_shader, .window_reset_shader = reset_shader,
    .window_set_shader_params = set_params, .shader_program_create = program_create,
    .shader_program_destroy = program_destroy, .render_hook_add = hook_add,
    .render_hook_remove = hook_remove, .shader_uniform_float = uniform_f,
    .shader_uniform_vec2 = uniform_2, .shader_uniform_vec4 = uniform_4,
    .shader_draw_fullscreen_scene = draw_scene, .schedule_render = schedule, .subscribe_event = subscribe,
};

static const struct shady_module *module;

static bool press_super_h(void) {
    xkb_keysym_t sym = XKB_KEY_h;
    return module->key((struct shady_server *)HOST, &sym, 1, SHADY_KEY_PRESSED, SHADY_MODIFIER_LOGO);
}

static void run(float seconds) {
    struct shady_render_context ctx = {
        .struct_size = sizeof(ctx), .width = 1920, .height = 1080,
        .camera_forward = {0, 0, -1}, .camera_right = {1, 0, 0}, .camera_up = {0, 1, 0},
        .tan_half_fov_y = 0.47f, .aspect = 16.f / 9.f,
    };
    for (float t = 0; t < seconds; t += 1.f / 60.f) {
        module->tick((struct shady_server *)HOST, 1.f / 60.f, 1920.f, 1080.f);
        for (int i = 0; i < hook_count; i++) hooks[i](HOST, &ctx, (void *)(uintptr_t)i);
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    windows[0] = (struct fake_window){ .x = 100, .y = 80, .z = 0.f, .w = 800, .h = 600, .workspace = "1" };
    windows[1] = (struct fake_window){ .x = 1100, .y = 500, .z = -0.1f, .w = 600, .h = 400, .workspace = "1" };
    windows[2] = (struct fake_window){ .x = 400, .y = 300, .z = 0.f, .w = 500, .h = 300, .workspace = "2" }; /* other workspace */

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    shady_plugin_entry_v1_fn entry = (shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
    /* Hosts without per-window params or scene capture are rejected. */
    uint32_t full = api.struct_size;
    api.struct_size = offsetof(struct shady_plugin_api_v1, window_set_shader_params);
    assert(!entry(SHADY_PLUGIN_ABI_V1, &api, HOST));
    api.struct_size = offsetof(struct shady_plugin_api_v1, shader_draw_fullscreen_scene);
    assert(!entry(SHADY_PLUGIN_ABI_V1, &api, HOST));
    api.struct_size = full;
    module = entry(SHADY_PLUGIN_ABI_V1, &api, HOST);
    assert(module && !strcmp(module->name, "black-hole"));
    assert(module->init((struct shady_server *)HOST) && hook_count == 1);
    module->start((struct shady_server *)HOST);

    /* Swallow: only the current workspace's windows fall in. */
    assert(press_super_h());
    assert(windows[0].shader && windows[1].shader && !windows[2].shader);
    assert(press_super_h()); /* busy: consumed, ignored */
    run(1.0f);
    assert(draws > 0);
    assert(windows[0].param_updates > 0 && windows[0].params[3] > 0.f);
    run(4.0f);
    for (int i = 0; i < 2; i++) {
        assert(!strcmp(windows[i].workspace, "black-hole"));
        assert(!windows[i].shader);
        /* Positions are never touched: motion lives in the shader. */
    }
    assert(windows[0].x == 100 && windows[1].y == 500);
    assert(!strcmp(windows[2].workspace, "2"));

    /* Emit onto whichever workspace is current now. */
    snprintf(current, sizeof(current), "3");
    assert(press_super_h());
    assert(!strcmp(windows[0].workspace, "3") && !strcmp(windows[1].workspace, "3"));
    assert(windows[0].shader && fabsf(windows[0].params[3] - 1.f) < 1e-3f);
    run(5.0f);
    assert(!windows[0].shader && !windows[1].shader);
    assert(!strcmp(windows[0].workspace, "3"));

    /* Once the aftershock has died down the post-process stops entirely. */
    run(2.0f);
    int settled = draws;
    run(1.0f);
    assert(draws == settled);

    /* Unload mid-swallow: everything comes straight back. */
    assert(press_super_h());
    run(1.4f);
    module->stop((struct shady_server *)HOST);
    module->destroy((struct shady_server *)HOST);
    for (int i = 0; i < 2; i++) {
        assert(!windows[i].shader);
        assert(!strcmp(windows[i].workspace, "3"));
    }
    dlclose(handle);
    puts("black-hole-plugin: PASS");
    return 0;
}
