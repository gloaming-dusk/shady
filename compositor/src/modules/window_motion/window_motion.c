#include "window_motion.h"

void shady_window_motion_add_impulse(struct shady_server *s, struct shady_toplevel *t,
        float wx, float wy, float tx, float ty) {
    if (s->motion_driver && s->motion_driver->impulse)
        s->motion_driver->impulse((shady_host)s, (shady_window)t, wx, wy, tx, ty);
}
void shady_window_motion_begin_drag(struct shady_toplevel *t, double x, double y) {
    struct shady_server *s = t->server;
    if (s->motion_driver && s->motion_driver->begin_drag)
        s->motion_driver->begin_drag((shady_host)s, (shady_window)t, x, y);
}
void shady_window_motion_drag(struct shady_server *s, struct shady_toplevel *t,
        double x, double y) {
    if (s->motion_driver && s->motion_driver->drag)
        s->motion_driver->drag((shady_host)s, (shady_window)t, x, y);
}
void shady_window_motion_reset(struct shady_toplevel *t) {
    struct shady_server *s = t->server;
    if (s->motion_driver && s->motion_driver->reset)
        s->motion_driver->reset((shady_host)s, (shady_window)t);
    t->motion = (struct shady_motion_visual){0};
}
void shady_window_motion_apply_damping(struct shady_toplevel *t, float factor) {
    struct shady_server *s = t->server;
    if (s->motion_driver && s->motion_driver->damp)
        s->motion_driver->damp((shady_host)s, (shady_window)t, factor);
}
void shady_window_motion_get_tilt(const struct shady_toplevel *t, float *x, float *y) {
    if (x) *x = t->motion.tilt_x;
    if (y) *y = t->motion.tilt_y;
}
void shady_window_motion_get_wobble(const struct shady_toplevel *t, float *x, float *y) {
    if (x) *x = t->motion.wobble_x;
    if (y) *y = t->motion.wobble_y;
}
