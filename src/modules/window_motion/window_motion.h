#ifndef SHADY_MODULE_WINDOW_MOTION_H
#define SHADY_MODULE_WINDOW_MOTION_H
#include "../../shady.h"
#if SHADY_HAS_WINDOW_MOTION
void shady_window_motion_update_toplevel(struct shady_server *server, struct shady_toplevel *toplevel, float dt);
void shady_window_motion_add_impulse(struct shady_server *server, struct shady_toplevel *toplevel,
	float wobble_x, float wobble_y, float tilt_x, float tilt_y);
void shady_window_motion_begin_drag(struct shady_toplevel *toplevel, double x, double y);
void shady_window_motion_drag(struct shady_server *server, struct shady_toplevel *toplevel, double x, double y);
void shady_window_motion_get_tilt(const struct shady_toplevel *toplevel, float *tilt_x, float *tilt_y);
void shady_window_motion_get_wobble(const struct shady_toplevel *toplevel, float *wobble_x, float *wobble_y);
void shady_window_motion_reset(struct shady_toplevel *toplevel);
void shady_window_motion_apply_damping(struct shady_toplevel *toplevel, float factor);
#else
static inline void shady_window_motion_get_tilt(const struct shady_toplevel*t,float*x,float*y){(void)t;if(x)*x=0.f;if(y)*y=0.f;}
static inline void shady_window_motion_get_wobble(const struct shady_toplevel*t,float*x,float*y){(void)t;if(x)*x=0.f;if(y)*y=0.f;}
static inline void shady_window_motion_reset(struct shady_toplevel*t){(void)t;}
static inline void shady_window_motion_apply_damping(struct shady_toplevel*t,float f){(void)t;(void)f;}
static inline void shady_window_motion_begin_drag(struct shady_toplevel*t,double x,double y){(void)t;(void)x;(void)y;}
static inline void shady_window_motion_drag(struct shady_server*s,struct shady_toplevel*t,double x,double y){(void)s;(void)t;(void)x;(void)y;}
static inline void shady_window_motion_add_impulse(struct shady_server*s,struct shady_toplevel*t,float wx,float wy,float tx,float ty){(void)s;(void)t;(void)wx;(void)wy;(void)tx;(void)ty;}
static inline void shady_window_motion_update_toplevel(struct shady_server*s,struct shady_toplevel*t,float dt){(void)s;(void)t;(void)dt;}
#endif
#endif
