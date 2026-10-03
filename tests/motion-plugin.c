/* Exercise a dlopened feature using only its installed public API. */
#include <assert.h>
#include <dlfcn.h>
#include <math.h>
#include <shady/motion.h>
#include <shady/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct shady_motion_driver *driver;
static struct shady_motion_visual visual;
static void *window_state;
static bool wobble = true;
static int host_object, window_object;
#define HOST ((shady_host)&host_object)
#define WINDOW ((shady_window)&window_object)

static bool spatial_enabled(shady_host h) { assert(h == HOST); return true; }
static bool wobble_enabled(shady_host h) { assert(h == HOST); return wobble; }
static bool attach(shady_host h, const struct shady_motion_driver *d) {
    assert(h == HOST);
    if (driver) return false;
    driver = d; return true;
}
static bool detach(shady_host h, const struct shady_motion_driver *d) {
    assert(h == HOST);
    if (driver != d) return false;
    driver = NULL; visual = (struct shady_motion_visual){0}; return true;
}
static bool set_visual(shady_host h, shady_window w, const struct shady_motion_visual *v) {
    assert(h == HOST && w == WINDOW); visual = *v; return true;
}
static const struct shady_motion_api_v1 feature = {
    .struct_size = sizeof(feature), .spatial_enabled = spatial_enabled,
    .wobble_enabled = wobble_enabled, .attach = attach, .detach = detach,
    .set_visual = set_visual,
};
static const void *query_api(shady_host h, const char *name, uint32_t version) {
    assert(h == HOST && !strcmp(name, SHADY_MOTION_API));
    return version == 1 ? &feature : NULL;
}
static void *state(shady_window w, const char *name) {
    assert(w == WINDOW && !strcmp(name, "window-motion")); return window_state;
}
static size_t count(shady_host h) { assert(h == HOST); return 1; }
static shady_window at(shady_host h, size_t index) { assert(h == HOST && index == 0); return WINDOW; }
static bool mapped(shady_window w) { assert(w == WINDOW); return true; }
static struct shady_plugin_api_v1 api = {
    .abi_version = 1, .struct_size = sizeof(api), .query_api = query_api,
    .window_state = state, .window_count = count, .window_at = at, .window_mapped = mapped,
};

static const struct shady_plugin_v2 *load(const char *path, void **handle) {
    *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!*handle) { fprintf(stderr, "%s\n", dlerror()); abort(); }
    shady_plugin_entry_v2_fn entry = (shady_plugin_entry_v2_fn)dlsym(*handle, SHADY_PLUGIN_ENTRY_V2);
    assert(entry);
    assert(!entry(1, &api, HOST));
    uint32_t size = api.struct_size;
    api.struct_size = offsetof(struct shady_plugin_api_v1, query_api);
    assert(!entry(2, &api, HOST));
    api.struct_size = size;
    const struct shady_plugin_v2 *v2 = entry(2, &api, HOST);
    assert(v2 && v2->state_schema_version == 1);
    return v2;
}
int main(int argc, char **argv) {
    assert(argc == 2);
    void *handle;
    const struct shady_plugin_v2 *v2 = load(argv[1], &handle);
    const struct shady_module *module = v2->module;
    window_state = calloc(1, module->toplevel_state_size);
    assert(window_state && module->init((struct shady_server *)HOST));
    assert(driver && module->enabled((struct shady_server *)HOST));
    driver->impulse(HOST, WINDOW, .15f, -.12f, .5f, -.4f);
    assert(visual.animating);
    module->tick((struct shady_server *)HOST, .016f, 1280.f, 720.f);
    assert(visual.wobble_x > 0 && visual.wobble_y < 0 && visual.tilt_x > 0);
    struct shady_motion_visual before = visual;
    size_t size = v2->window_snapshot_size(HOST, WINDOW, window_state);
    void *snapshot = malloc(size);
    assert(snapshot && v2->save_window_state(HOST, WINDOW, window_state, snapshot, size));
    module->destroy((struct shady_server *)HOST);
    assert(!driver && !visual.animating);
    free(window_state);
    assert(dlclose(handle) == 0);
    v2 = load(argv[1], &handle); module = v2->module;
    window_state = calloc(1, module->toplevel_state_size);
    assert(window_state && module->init((struct shady_server *)HOST));
    assert(!v2->restore_window_state(HOST, WINDOW, window_state, snapshot, size, 99));
    assert(v2->restore_window_state(HOST, WINDOW, window_state, snapshot, size, 1));
    assert(visual.wobble_x == before.wobble_x && visual.tilt_x == before.tilt_x);
    wobble = false;
    module->tick((struct shady_server *)HOST, .016f, 1280.f, 720.f);
    assert(visual.wobble_x == 0 && visual.wobble_y == 0 && visual.tilt_x > before.tilt_x);
    driver->reset(HOST, WINDOW);
    assert(!visual.animating && visual.tilt_x == 0);
    driver->begin_drag(HOST, WINDOW, 10, 10);
    driver->drag(HOST, WINDOW, 35, 30);
    assert(visual.animating);
    for (int i = 0; i < 200; ++i) module->tick((struct shady_server *)HOST, .016f, 1280, 720);
    assert(!visual.animating);
    module->destroy((struct shady_server *)HOST);
    free(snapshot); free(window_state); assert(dlclose(handle) == 0);
    puts("motion-plugin: PASS");
    return 0;
}
