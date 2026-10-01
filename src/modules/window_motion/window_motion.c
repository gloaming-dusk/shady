#include "window_motion.h"
#include "../spatial/state.h"
#include <math.h>

void shady_window_motion_update_toplevel(struct shady_server *server,
		struct shady_toplevel *toplevel, float dt) {
	if (dt <= 0.f) return;

	/* Flexible wobble is a configurable visual effect. When disabled, clear
	 * all accumulated state so other modules cannot leave a latent impulse. */
	if (server->config.window_wobble) {
		const float spring = 42.f, damping_rate = 7.5f;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vx += -shady_spatial_toplevel_state(toplevel)->motion.wobble_x * spring * dt;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vy += -shady_spatial_toplevel_state(toplevel)->motion.wobble_y * spring * dt;
		float damping = 1.f - damping_rate * dt;
		if (damping < 0.f) damping = 0.f;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vx *= damping;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vy *= damping;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_x += shady_spatial_toplevel_state(toplevel)->motion.wobble_vx * dt;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_y += shady_spatial_toplevel_state(toplevel)->motion.wobble_vy * dt;
		if (fabsf(shady_spatial_toplevel_state(toplevel)->motion.wobble_x)<.00005f && fabsf(shady_spatial_toplevel_state(toplevel)->motion.wobble_vx)<.00005f)
			shady_spatial_toplevel_state(toplevel)->motion.wobble_x=shady_spatial_toplevel_state(toplevel)->motion.wobble_vx=0.f;
		if (fabsf(shady_spatial_toplevel_state(toplevel)->motion.wobble_y)<.00005f && fabsf(shady_spatial_toplevel_state(toplevel)->motion.wobble_vy)<.00005f)
			shady_spatial_toplevel_state(toplevel)->motion.wobble_y=shady_spatial_toplevel_state(toplevel)->motion.wobble_vy=0.f;
	} else {
		shady_spatial_toplevel_state(toplevel)->motion.wobble_x=shady_spatial_toplevel_state(toplevel)->motion.wobble_y=0.f;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vx=shady_spatial_toplevel_state(toplevel)->motion.wobble_vy=0.f;
	}

	/* Rigid tilt is intentionally independent from flexible wobble. */
	const float tilt_damping_rate = 9.f;
	float damping = 1.f - tilt_damping_rate * dt;
	if (damping < 0.f) damping = 0.f;
	shady_spatial_toplevel_state(toplevel)->motion.tilt_vx *= damping;
	shady_spatial_toplevel_state(toplevel)->motion.tilt_vy *= damping;
	shady_spatial_toplevel_state(toplevel)->motion.tilt_x += shady_spatial_toplevel_state(toplevel)->motion.tilt_vx * dt;
	shady_spatial_toplevel_state(toplevel)->motion.tilt_y += shady_spatial_toplevel_state(toplevel)->motion.tilt_vy * dt;
}

void shady_window_motion_add_impulse(struct shady_server *server,
		struct shady_toplevel *toplevel, float wobble_x, float wobble_y,
		float tilt_x, float tilt_y) {
	if (server->config.window_wobble) {
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vx += wobble_x;
		shady_spatial_toplevel_state(toplevel)->motion.wobble_vy += wobble_y;
	}
	shady_spatial_toplevel_state(toplevel)->motion.tilt_vx += tilt_x;
	shady_spatial_toplevel_state(toplevel)->motion.tilt_vy += tilt_y;
}

void shady_window_motion_begin_drag(struct shady_toplevel *toplevel,double x,double y){shady_spatial_toplevel_state(toplevel)->motion.last_move_x=x;shady_spatial_toplevel_state(toplevel)->motion.last_move_y=y;shady_spatial_toplevel_state(toplevel)->motion.wobble_dragging=true;}
void shady_window_motion_drag(struct shady_server *server,struct shady_toplevel *toplevel,double x,double y){
	double dx=x-shady_spatial_toplevel_state(toplevel)->motion.last_move_x,dy=y-shady_spatial_toplevel_state(toplevel)->motion.last_move_y;
	if(!shady_spatial_toplevel_state(toplevel)->motion.wobble_dragging){shady_window_motion_begin_drag(toplevel,x,y);return;}
	shady_window_motion_add_impulse(server,toplevel,-(float)dx*.0065f,-(float)dy*.0065f,-(float)dy*.00055f,(float)dx*.00055f);
	if (shady_spatial_toplevel_state(toplevel)->motion.tilt_vx > .55f) shady_spatial_toplevel_state(toplevel)->motion.tilt_vx = .55f;
	if (shady_spatial_toplevel_state(toplevel)->motion.tilt_vx < -.55f) shady_spatial_toplevel_state(toplevel)->motion.tilt_vx = -.55f;
	if (shady_spatial_toplevel_state(toplevel)->motion.tilt_vy > .55f) shady_spatial_toplevel_state(toplevel)->motion.tilt_vy = .55f;
	if (shady_spatial_toplevel_state(toplevel)->motion.tilt_vy < -.55f) shady_spatial_toplevel_state(toplevel)->motion.tilt_vy = -.55f;
	if (shady_spatial_toplevel_state(toplevel)->motion.wobble_vx > .45f) shady_spatial_toplevel_state(toplevel)->motion.wobble_vx = .45f;
	if (shady_spatial_toplevel_state(toplevel)->motion.wobble_vx < -.45f) shady_spatial_toplevel_state(toplevel)->motion.wobble_vx = -.45f;
	if (shady_spatial_toplevel_state(toplevel)->motion.wobble_vy > .45f) shady_spatial_toplevel_state(toplevel)->motion.wobble_vy = .45f;
	if (shady_spatial_toplevel_state(toplevel)->motion.wobble_vy < -.45f) shady_spatial_toplevel_state(toplevel)->motion.wobble_vy = -.45f;
	shady_spatial_toplevel_state(toplevel)->motion.last_move_x=x;shady_spatial_toplevel_state(toplevel)->motion.last_move_y=y;
}

void shady_window_motion_get_tilt(const struct shady_toplevel *toplevel,float *tilt_x,float *tilt_y){if(tilt_x)*tilt_x=shady_spatial_toplevel_state_const(toplevel)->motion.tilt_x;if(tilt_y)*tilt_y=shady_spatial_toplevel_state_const(toplevel)->motion.tilt_y;}
void shady_window_motion_apply_damping(struct shady_toplevel *toplevel,float factor){if(factor<0.f)factor=0.f;shady_spatial_toplevel_state(toplevel)->motion.tilt_vx*=factor;shady_spatial_toplevel_state(toplevel)->motion.tilt_vy*=factor;}
