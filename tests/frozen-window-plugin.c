/* Drive the frozen-window plugin against a fake host with fake windows:
 * Super+Z freezes the focused window and holds it, a second press thaws it
 * and releases the shader, Super+Shift+Z freezes/thaws the workspace, a
 * window claimed by another shader is dropped, and unload releases
 * everything. Uses only the public plugin API. */
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <shady/event.h>
#include <shady/plugin.h>
#include <stdio.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define WINDOWS 3
#define PROGRAM 42

struct fake_window {
    int w, h;
    bool visible;
    shady_shader_program shader;
    float params[SHADY_WINDOW_SHADER_PARAMS * 4];
    int param_updates;
};

static int host_object;
#define HOST ((shady_host)&host_object)
static struct fake_window windows[WINDOWS];
static shady_window focused;

static struct fake_window *W(shady_window w) { return (struct fake_window *)w; }
static size_t count(shady_host h) { assert(h == HOST); return WINDOWS; }
static shady_window at(shady_host h, size_t i) { assert(h == HOST && i < WINDOWS); return (shady_window)&windows[i]; }
static bool valid(shady_host h, shady_window w) { (void)h; return w != NULL; }
static bool mapped(shady_window w) { (void)w; return true; }
static bool visible(shady_window w) { return W(w)->visible; }
static bool size(shady_window w, int *wd, int *ht) { *wd = W(w)->w; *ht = W(w)->h; return true; }
static shady_window focused_window(shady_host h) { (void)h; return focused; }
static bool set_shader(shady_host h, shady_window w, shady_shader_program p) {
    (void)h; W(w)->shader = p; memset(W(w)->params, 0, sizeof(W(w)->params)); return true;
}
static bool reset_shader(shady_host h, shady_window w) { (void)h; W(w)->shader = 0; return true; }
/* Like the real host, only the shader's owner may set its params. */
static bool set_params(shady_host h, shady_window w, const float *p) {
    (void)h;
    if (W(w)->shader != PROGRAM) return false;
    for (int i = 0; i < SHADY_WINDOW_SHADER_PARAMS * 4; i++) assert(isfinite(p[i]));
    memcpy(W(w)->params, p, sizeof(W(w)->params));
    W(w)->param_updates++;
    return true;
}
static shady_shader_program program_create(shady_host h, const char *v, const char *f) {
    (void)h;
    assert(strstr(v, "frozen_window.vert") && strstr(f, "frozen_window.frag"));
    return PROGRAM;
}
static bool program_destroy(shady_host h, shady_shader_program p) { (void)h; return p == PROGRAM; }
static void schedule(shady_host h) { (void)h; }
static void log_msg(enum shady_plugin_log_level l, const char *m) { (void)l; fprintf(stderr, "[plugin] %s\n", m); }
static bool subscribe(shady_host h, uint32_t t, shady_event_callback cb, void *u) { (void)h; (void)t; (void)cb; (void)u; return true; }

static struct shady_plugin_api_v1 api = {
    .abi_version = SHADY_PLUGIN_ABI_V1, .struct_size = sizeof(api), .log = log_msg,
    .window_count = count, .window_at = at, .window_valid = valid, .window_mapped = mapped,
    .window_visible = visible, .window_size = size, .focused_window = focused_window,
    .window_set_shader = set_shader, .window_reset_shader = reset_shader,
    .window_set_shader_params = set_params, .shader_program_create = program_create,
    .shader_program_destroy = program_destroy, .schedule_render = schedule,
    .subscribe_event = subscribe,
};

static const struct shady_module *module;

static bool press(uint32_t modifiers) {
    xkb_keysym_t sym = (modifiers & SHADY_MODIFIER_SHIFT) ? XKB_KEY_Z : XKB_KEY_z;
    return module->key((struct shady_server *)HOST, &sym, 1, SHADY_KEY_PRESSED, modifiers);
}

static void run(float seconds) {
    for (float t = 0; t < seconds; t += 1.f / 60.f)
        module->tick((struct shady_server *)HOST, 1.f / 60.f, 1920.f, 1080.f);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    windows[0] = (struct fake_window){ .w = 800, .h = 600, .visible = true };
    windows[1] = (struct fake_window){ .w = 600, .h = 400, .visible = true };
    windows[2] = (struct fake_window){ .w = 500, .h = 300, .visible = false }; /* other workspace */

    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    shady_plugin_entry_v1_fn entry = (shady_plugin_entry_v1_fn)dlsym(handle, SHADY_PLUGIN_ENTRY_V1);
    /* Hosts without per-window shader params are rejected. */
    uint32_t full = api.struct_size;
    api.struct_size = offsetof(struct shady_plugin_api_v1, window_set_shader_params);
    assert(!entry(SHADY_PLUGIN_ABI_V1, &api, HOST));
    api.struct_size = full;
    module = entry(SHADY_PLUGIN_ABI_V1, &api, HOST);
    assert(module && !strcmp(module->name, "frozen-window"));
    assert(module->init((struct shady_server *)HOST));
    module->start((struct shady_server *)HOST);

    /* Plain Z and Super+Z without a focused window are not frozen. */
    xkb_keysym_t z = XKB_KEY_z;
    assert(!module->key((struct shady_server *)HOST, &z, 1, SHADY_KEY_PRESSED, 0));
    assert(press(SHADY_MODIFIER_LOGO));
    assert(!windows[0].shader);

    /* Freeze the focused window: frost grows to 1 and then holds still. */
    focused = (shady_window)&windows[0];
    assert(press(SHADY_MODIFIER_LOGO));
    assert(windows[0].shader == PROGRAM && !windows[1].shader);
    run(0.2f);
    assert(windows[0].params[0] > 0.f && windows[0].params[0] < 1.f);
    assert(windows[0].params[1] == 0.f && windows[0].params[3] > 0.f);
    assert(windows[0].params[4] == 800.f && windows[0].params[5] == 600.f);
    run(2.3f);
    assert(windows[0].params[0] == 1.f && windows[0].params[3] == 0.f);
    int settled = windows[0].param_updates;
    run(1.0f);
    assert(windows[0].param_updates == settled); /* static once set */

    /* Thaw: frost recedes, then the shader is released. */
    assert(press(SHADY_MODIFIER_LOGO));
    assert(windows[0].params[1] == 1.f);
    run(0.4f);
    assert(windows[0].shader == PROGRAM && windows[0].params[0] < 1.f);
    /* Re-freezing mid-thaw continues from the current frost. */
    float partial = windows[0].params[0];
    assert(press(SHADY_MODIFIER_LOGO));
    assert(windows[0].params[1] == 0.f && windows[0].params[0] == partial);
    assert(press(SHADY_MODIFIER_LOGO));
    run(2.0f);
    assert(!windows[0].shader);

    /* Super+Shift+Z freezes the workspace, then thaws it. */
    assert(press(SHADY_MODIFIER_LOGO | SHADY_MODIFIER_SHIFT));
    assert(windows[0].shader == PROGRAM && windows[1].shader == PROGRAM && !windows[2].shader);
    run(2.0f);
    assert(windows[0].params[2] != windows[1].params[2]); /* distinct patterns */
    assert(press(SHADY_MODIFIER_LOGO | SHADY_MODIFIER_SHIFT));
    run(2.0f);
    assert(!windows[0].shader && !windows[1].shader);

    /* Another shader plugin claiming a freezing window: it is let go. */
    assert(press(SHADY_MODIFIER_LOGO));
    windows[0].shader = 7;
    run(0.2f);
    windows[0].shader = 0;
    run(0.2f);
    assert(!windows[0].shader);

    /* Unload mid-freeze: every window is released. */
    assert(press(SHADY_MODIFIER_LOGO | SHADY_MODIFIER_SHIFT));
    run(0.5f);
    module->stop((struct shady_server *)HOST);
    module->destroy((struct shady_server *)HOST);
    for (int i = 0; i < WINDOWS; i++) assert(!windows[i].shader);
    dlclose(handle);
    puts("frozen-window-plugin: PASS");
    return 0;
}
