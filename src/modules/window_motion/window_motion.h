#ifndef SHADY_MODULE_WINDOW_MOTION_H
#define SHADY_MODULE_WINDOW_MOTION_H
#include "../../shady.h"
void shady_window_motion_add_impulse(struct shady_server *server, struct shady_toplevel *toplevel,
	float wobble_x, float wobble_y, float tilt_x, float tilt_y);
void shady_window_motion_begin_drag(struct shady_toplevel *toplevel, double x, double y);
void shady_window_motion_drag(struct shady_server *server, struct shady_toplevel *toplevel, double x, double y);
void shady_window_motion_get_tilt(const struct shady_toplevel *toplevel, float *tilt_x, float *tilt_y);
void shady_window_motion_get_wobble(const struct shady_toplevel *toplevel, float *wobble_x, float *wobble_y);
void shady_window_motion_reset(struct shady_toplevel *toplevel);
void shady_window_motion_apply_damping(struct shady_toplevel *toplevel, float factor);
#endif
