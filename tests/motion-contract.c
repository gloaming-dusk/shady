#include <assert.h>
#include <math.h>
#include <string.h>
#include <shady/plugin.h>
#include <shady/motion.h>
#include <shady/event.h>
static const struct shady_plugin_api_v1 *api;
static const struct shady_motion_api_v1 *motion;
static shady_host host;
static shady_window window;
static struct shady_motion_visual last_visual;
static bool sampled;
static const struct shady_motion_driver competing_driver = {.struct_size = sizeof(competing_driver)};
static bool init(struct shady_server *server) {
    (void)server;
    assert(!api->query_api(host, "missing.feature", 1));
    assert(!api->query_api(host, SHADY_MOTION_API, 99));
    const struct shady_representation_api_v1 *representation =
        api->query_api(host, SHADY_REPRESENTATION_API, 1);
    assert(representation && representation->struct_size >= sizeof(*representation));
    assert(!motion->attach(host, &competing_driver));
    assert(!motion->get_visual(host, NULL, &last_visual));
    return true;
}
static void mapped(struct shady_toplevel *toplevel) {
    window = (shady_window)toplevel;
    struct shady_motion_visual visual = {0};
    assert(motion->get_visual(host, window, &visual));
    assert(!motion->set_visual(host, window, &visual));
    assert(!motion->add_impulse(host, window, NAN, 0, 0, 0));
    assert(!motion->damp(host, window, INFINITY));
    assert(motion->add_impulse(host, window, .15f, -.12f, .5f, -.4f));
}
static void tick(struct shady_server *server, float dt, float width, float height) {
    (void)server; (void)dt; (void)width; (void)height;
    if (!window) return;
    assert(motion->get_visual(host, window, &last_visual));
    if (fabsf(last_visual.wobble_x) > .00001f && last_visual.animating && !sampled) {
        sampled = true;
        api->log(SHADY_PLUGIN_LOG_INFO, "motion-contract: ACTIVE_PASS");
    }
}
static void module_started(shady_host h, const struct shady_event *event, void *data) {
    (void)data;
    if (strcmp(api->module_name(event->object.module), "window-motion")) return;
    assert(sampled && window);
    struct shady_motion_visual after;
    assert(motion->get_visual(h, window, &after));
    assert(after.animating == last_visual.animating);
    assert(after.wobble_x == last_visual.wobble_x && after.tilt_x == last_visual.tilt_x);
    assert(motion->reset(h, window));
    assert(motion->get_visual(h, window, &after) && !after.animating && after.tilt_x == 0);
    api->log(SHADY_PLUGIN_LOG_INFO, "motion-contract: RELOAD_PASS");
}
static void start(struct shady_server *server) {
    (void)server;
    assert(api->subscribe_event(host, SHADY_EVENT_MODULE_STARTED, module_started, NULL));
}
static const char *const requires[] = {"spatial.window-motion", NULL};
static const struct shady_module module = {
    .name = "motion-contract", .requires = requires,
    .init = init, .start = start, .toplevel_map = mapped, .tick = tick,
};
const struct shady_module *shady_plugin_entry_v1(uint32_t abi,
        const struct shady_plugin_api_v1 *host_api, shady_host h) {
    if (abi != 1 || !SHADY_API_HAS(host_api, query_api)) return NULL;
    motion = host_api->query_api(h, SHADY_MOTION_API, 1);
    if (!motion || motion->struct_size < sizeof(*motion)) return NULL;
    api = host_api; host = h; return &module;
}
