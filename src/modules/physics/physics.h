#ifndef SHADY_MODULE_PHYSICS_H
#define SHADY_MODULE_PHYSICS_H
#include "../../shady.h"
#include "../spatial/state.h"

struct shady_window_body {
	float center[3];
	float half[3];
	float tilt_x, tilt_y;
};
#if SHADY_HAS_PHYSICS
void shady_physics_init(struct shady_server *server);
void shady_physics_toggle_gravity(struct shady_server *server);
void shady_physics_update(struct shady_server *server, float dt, float logical_w, float logical_h);
void shady_physics_set_velocity(struct shady_toplevel *toplevel, float vx, float vy, float vz);
void shady_physics_stop(struct shady_toplevel *toplevel);
void shady_physics_respawn_window(struct shady_server *server, struct shady_toplevel *toplevel);
void shady_physics_respawn_all(struct shady_server *server);
/* Move an authoritative FPS cube through the static world without tunneling.
 * Used by both free physics and the held-window kinematic path. */
void shady_physics_move_cube(const struct shady_world *world,float center[3],
	const float target[3],float half);
bool shady_physics_window_body(const struct shady_toplevel *toplevel,
	float logical_w, float logical_h, struct shady_window_body *body);
#else
static inline void shady_physics_set_velocity(struct shady_toplevel*t,float x,float y,float z){(void)t;(void)x;(void)y;(void)z;}
static inline void shady_physics_stop(struct shady_toplevel*t){(void)t;}
static inline void shady_physics_move_cube(const struct shady_world*w,float c[3],const float t[3],float h){(void)w;(void)h;c[0]=t[0];c[1]=t[1];c[2]=t[2];}
static inline void shady_physics_init(struct shady_server *server) { (void)server; }
static inline void shady_physics_toggle_gravity(struct shady_server *server) { (void)server; }
static inline void shady_physics_update(struct shady_server *server,float dt,float w,float h){(void)server;(void)dt;(void)w;(void)h;}
static inline void shady_physics_respawn_window(struct shady_server *server, struct shady_toplevel *toplevel){(void)server;(void)toplevel;}
static inline void shady_physics_respawn_all(struct shady_server *server){(void)server;}
static inline bool shady_physics_window_body(const struct shady_toplevel *toplevel,float logical_w,float logical_h,struct shady_window_body *body){(void)toplevel;(void)logical_w;(void)logical_h;(void)body;return false;}
#endif
#endif
