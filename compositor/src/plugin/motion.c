#include "internal.h"
#include "../shady.h"
#include "../render/render.h"
#include <math.h>

static bool spatial_enabled(shady_host host) {
    return ((struct shady_server *)host)->config.spatial_mode;
}
static bool wobble_enabled(shady_host host) {
    return ((struct shady_server *)host)->config.window_wobble;
}
static bool callback_owned(void *owner, void *callback) {
    return !callback || shady_plugin_owner_from_address(callback) == owner;
}
static bool attach(shady_host host, const struct shady_motion_driver *driver) {
    struct shady_server *s = (struct shady_server *)host;
    if (!driver || driver->struct_size < sizeof(*driver)) return false;
    void *owner = shady_plugin_owner_from_address((void *)driver);
    if (!owner || (s->motion_driver && s->motion_driver != driver)) return false;
    if (!callback_owned(owner, (void *)driver->impulse) ||
            !callback_owned(owner, (void *)driver->begin_drag) ||
            !callback_owned(owner, (void *)driver->drag) ||
            !callback_owned(owner, (void *)driver->reset) ||
            !callback_owned(owner, (void *)driver->damp)) return false;
    s->motion_driver = driver;
    s->motion_owner = owner;
    return true;
}
void shady_plugin_motion_cleanup_owner(struct shady_server *s, void *owner) {
    if (!owner || s->motion_owner != owner) return;
    s->motion_driver = NULL;
    s->motion_owner = NULL;
    struct shady_toplevel *t;
    wl_list_for_each(t, &s->all_toplevels, all_link)
        t->motion = (struct shady_motion_visual){0};
    if (s->renderer) shady_render_schedule_all_outputs(s);
}
static bool detach(shady_host host, const struct shady_motion_driver *driver) {
    struct shady_server *s = (struct shady_server *)host;
    if (!driver || s->motion_driver != driver) return false;
    shady_plugin_motion_cleanup_owner(s, shady_plugin_owner_from_address((void *)driver));
    return true;
}
static bool get_visual(shady_host host, shady_window window, struct shady_motion_visual *out) {
    if (!out || !shady_plugin_window_valid(host, window)) return false;
    *out = ((struct shady_toplevel *)window)->motion;
    return true;
}
static bool set_visual(shady_host host, shady_window window, const struct shady_motion_visual *v) {
    struct shady_server *s = (struct shady_server *)host;
    if (!v || !shady_plugin_window_valid(host, window) || !s->motion_driver ||
            s->motion_owner != shady_plugin_owner_from_address(__builtin_return_address(0)) ||
            !isfinite(v->tilt_x) || !isfinite(v->tilt_y) ||
            !isfinite(v->wobble_x) || !isfinite(v->wobble_y)) return false;
    struct shady_toplevel *t = (struct shady_toplevel *)window;
    bool changed = t->motion.wobble_x != v->wobble_x || t->motion.wobble_y != v->wobble_y ||
        t->motion.tilt_x != v->tilt_x || t->motion.tilt_y != v->tilt_y ||
        t->motion.animating != v->animating;
    t->motion = *v;
    if (changed && s->renderer) shady_render_schedule_all_outputs(s);
    return true;
}
static bool add_impulse(shady_host host, shady_window window, float wx, float wy, float tx, float ty) {
    struct shady_server *s = (struct shady_server *)host;
    if (!shady_plugin_window_valid(host, window) || !s->motion_driver ||
            !s->motion_driver->impulse || !isfinite(wx) || !isfinite(wy) ||
            !isfinite(tx) || !isfinite(ty)) return false;
    s->motion_driver->impulse(host, window, wx, wy, tx, ty);
    return true;
}
static bool reset(shady_host host, shady_window window) {
    struct shady_server *s = (struct shady_server *)host;
    if (!shady_plugin_window_valid(host, window) || !s->motion_driver ||
            !s->motion_driver->reset) return false;
    s->motion_driver->reset(host, window);
    return true;
}
static bool damp(shady_host host, shady_window window, float factor) {
    struct shady_server *s = (struct shady_server *)host;
    if (!shady_plugin_window_valid(host, window) || !s->motion_driver ||
            !s->motion_driver->damp || !isfinite(factor)) return false;
    s->motion_driver->damp(host, window, factor);
    return true;
}
const struct shady_motion_api_v1 shady_motion_api = {
    .struct_size = sizeof(shady_motion_api),
    .spatial_enabled = spatial_enabled, .wobble_enabled = wobble_enabled,
    .attach = attach, .detach = detach, .get_visual = get_visual, .set_visual = set_visual,
    .add_impulse = add_impulse, .reset = reset, .damp = damp,
};
