#include "window_motion.h"
#include "state.h"
#include <math.h>

void shady_window_motion_update_toplevel(struct shady_server *server,
		struct shady_toplevel *toplevel, float dt) {
	if (dt <= 0.f) return;

	/* Flexible wobble is a configurable visual effect. When disabled, clear
	 * all accumulated state so other modules cannot leave a latent impulse. */
	if (server->config.window_wobble) {
		const float spring = 42.f, damping_rate = 7.5f;
		shady_window_motion_state_for(toplevel)->wobble_vx += -shady_window_motion_state_for(toplevel)->wobble_x * spring * dt;
		shady_window_motion_state_for(toplevel)->wobble_vy += -shady_window_motion_state_for(toplevel)->wobble_y * spring * dt;
		float damping = 1.f - damping_rate * dt;
		if (damping < 0.f) damping = 0.f;
		shady_window_motion_state_for(toplevel)->wobble_vx *= damping;
		shady_window_motion_state_for(toplevel)->wobble_vy *= damping;
		shady_window_motion_state_for(toplevel)->wobble_x += shady_window_motion_state_for(toplevel)->wobble_vx * dt;
		shady_window_motion_state_for(toplevel)->wobble_y += shady_window_motion_state_for(toplevel)->wobble_vy * dt;
		if (fabsf(shady_window_motion_state_for(toplevel)->wobble_x)<.00005f && fabsf(shady_window_motion_state_for(toplevel)->wobble_vx)<.00005f)
			shady_window_motion_state_for(toplevel)->wobble_x=shady_window_motion_state_for(toplevel)->wobble_vx=0.f;
		if (fabsf(shady_window_motion_state_for(toplevel)->wobble_y)<.00005f && fabsf(shady_window_motion_state_for(toplevel)->wobble_vy)<.00005f)
			shady_window_motion_state_for(toplevel)->wobble_y=shady_window_motion_state_for(toplevel)->wobble_vy=0.f;
	} else {
		shady_window_motion_state_for(toplevel)->wobble_x=shady_window_motion_state_for(toplevel)->wobble_y=0.f;
		shady_window_motion_state_for(toplevel)->wobble_vx=shady_window_motion_state_for(toplevel)->wobble_vy=0.f;
	}

	/* Rigid tilt is intentionally independent from flexible wobble. */
	const float tilt_damping_rate = 9.f;
	float damping = 1.f - tilt_damping_rate * dt;
	if (damping < 0.f) damping = 0.f;
	shady_window_motion_state_for(toplevel)->tilt_vx *= damping;
	shady_window_motion_state_for(toplevel)->tilt_vy *= damping;
	shady_window_motion_state_for(toplevel)->tilt_x += shady_window_motion_state_for(toplevel)->tilt_vx * dt;
	shady_window_motion_state_for(toplevel)->tilt_y += shady_window_motion_state_for(toplevel)->tilt_vy * dt;
}

void shady_window_motion_add_impulse(struct shady_server *server,
		struct shady_toplevel *toplevel, float wobble_x, float wobble_y,
		float tilt_x, float tilt_y) {
	if (server->config.window_wobble) {
		shady_window_motion_state_for(toplevel)->wobble_vx += wobble_x;
		shady_window_motion_state_for(toplevel)->wobble_vy += wobble_y;
	}
	shady_window_motion_state_for(toplevel)->tilt_vx += tilt_x;
	shady_window_motion_state_for(toplevel)->tilt_vy += tilt_y;
}

void shady_window_motion_begin_drag(struct shady_toplevel *toplevel,double x,double y){shady_window_motion_state_for(toplevel)->last_move_x=x;shady_window_motion_state_for(toplevel)->last_move_y=y;shady_window_motion_state_for(toplevel)->wobble_dragging=true;}
void shady_window_motion_drag(struct shady_server *server,struct shady_toplevel *toplevel,double x,double y){
	double dx=x-shady_window_motion_state_for(toplevel)->last_move_x,dy=y-shady_window_motion_state_for(toplevel)->last_move_y;
	if(!shady_window_motion_state_for(toplevel)->wobble_dragging){shady_window_motion_begin_drag(toplevel,x,y);return;}
	shady_window_motion_add_impulse(server,toplevel,-(float)dx*.0065f,-(float)dy*.0065f,-(float)dy*.00055f,(float)dx*.00055f);
	if (shady_window_motion_state_for(toplevel)->tilt_vx > .55f) shady_window_motion_state_for(toplevel)->tilt_vx = .55f;
	if (shady_window_motion_state_for(toplevel)->tilt_vx < -.55f) shady_window_motion_state_for(toplevel)->tilt_vx = -.55f;
	if (shady_window_motion_state_for(toplevel)->tilt_vy > .55f) shady_window_motion_state_for(toplevel)->tilt_vy = .55f;
	if (shady_window_motion_state_for(toplevel)->tilt_vy < -.55f) shady_window_motion_state_for(toplevel)->tilt_vy = -.55f;
	if (shady_window_motion_state_for(toplevel)->wobble_vx > .45f) shady_window_motion_state_for(toplevel)->wobble_vx = .45f;
	if (shady_window_motion_state_for(toplevel)->wobble_vx < -.45f) shady_window_motion_state_for(toplevel)->wobble_vx = -.45f;
	if (shady_window_motion_state_for(toplevel)->wobble_vy > .45f) shady_window_motion_state_for(toplevel)->wobble_vy = .45f;
	if (shady_window_motion_state_for(toplevel)->wobble_vy < -.45f) shady_window_motion_state_for(toplevel)->wobble_vy = -.45f;
	shady_window_motion_state_for(toplevel)->last_move_x=x;shady_window_motion_state_for(toplevel)->last_move_y=y;
}

void shady_window_motion_get_tilt(const struct shady_toplevel *toplevel,float *tilt_x,float *tilt_y){const struct shady_window_motion_state*s=shady_window_motion_state_for_const(toplevel);if(tilt_x)*tilt_x=s?s->tilt_x:0.f;if(tilt_y)*tilt_y=s?s->tilt_y:0.f;}
void shady_window_motion_get_wobble(const struct shady_toplevel *toplevel,float *wobble_x,float *wobble_y){const struct shady_window_motion_state*s=shady_window_motion_state_for_const(toplevel);if(wobble_x)*wobble_x=s?s->wobble_x:0.f;if(wobble_y)*wobble_y=s?s->wobble_y:0.f;}
void shady_window_motion_reset(struct shady_toplevel *toplevel){struct shady_window_motion_state*s=shady_window_motion_state_for(toplevel);if(s)*s=(struct shady_window_motion_state){0};}
void shady_window_motion_apply_damping(struct shady_toplevel *toplevel,float factor){if(factor<0.f)factor=0.f;shady_window_motion_state_for(toplevel)->tilt_vx*=factor;shady_window_motion_state_for(toplevel)->tilt_vy*=factor;}
