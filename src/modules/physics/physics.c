#include "physics.h"
#include "collision.h"
#include "../spatial/state.h"
#include "state.h"
#include <math.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output.h>
#include "../../render/render.h"
#include "../window_motion/window_motion.h"
#include "../fps/fps.h"
#include "../../world/floor.h"
#include "../../world/collider.h"
#include "../../world/world.h"
#define WINDOW_GRAVITY 2.8f
#define WINDOW_RESPAWN_Y -3.0f
#define WINDOW_RESPAWN_Z_LIMIT 12.0f
#define WINDOW_GROUND_IMPACT_TANGENT_RETAIN .58f
#define WINDOW_REST_FRICTION_RATE 7.0f
#define WINDOW_REST_SPEED_EPSILON .025f

bool shady_physics_window_body(const struct shady_toplevel *t,float logical_w,float logical_h,struct shady_window_body *body){
	if(!t||!body||logical_h<=0.f)return false;
	struct wlr_surface*s=t->xdg_toplevel->base->surface;
	float tw=(float)s->current.width,th=(float)s->current.height;if(tw<=0.f||th<=0.f)return false;
	float ww=tw/logical_h,wh=th/logical_h,tx=0.f,ty=0.f;shady_window_motion_get_tilt(t,&tx,&ty);
	float sx=sinf(tx),cx=cosf(tx),sy=sinf(ty);
	body->center[0]=((float)t->scene_tree->node.x+tw*.5f-logical_w*.5f)/logical_h;
	body->center[1]=.5f-((float)t->scene_tree->node.y+th*.5f)/logical_h;
	body->center[2]=shady_spatial_toplevel_state_const(t)->z;body->tilt_x=tx;body->tilt_y=ty;
	body->half[0]=fabsf(cosf(ty))*ww*.5f;
	body->half[1]=fabsf(cx)*wh*.5f+fabsf(sx*sy)*ww*.5f;
	body->half[2]=fabsf(sy)*ww*.5f+fabsf(sx)*wh*.5f;
	if(body->half[0]<.006f) body->half[0]=.006f;
	if(body->half[1]<.012f) body->half[1]=.012f;
	if(body->half[2]<.006f) body->half[2]=.006f;
	return true;
}

void shady_physics_init(struct shady_server *server) {
	shady_physics_state_for(server)->gravity_enabled=server->config.physics_enabled && server->config.window_gravity;
}
void shady_physics_toggle_gravity(struct shady_server *server) {
	if (!server->config.physics_enabled) return;
	shady_physics_state_for(server)->gravity_enabled=!shady_physics_state_for(server)->gravity_enabled;
	struct shady_toplevel *t;
	wl_list_for_each(t,&server->toplevels,link) shady_physics_toplevel_state(t)->vy=0.f;
	shady_render_schedule_all_outputs(server);
}
void shady_physics_update(struct shady_server *server,float dt,float logical_w,float logical_h) {
	if(!server->config.physics_enabled || !shady_physics_state_for(server)->gravity_enabled || !shady_spatial_state(server)->runtime.camera.first_person || dt<=0.f || logical_w<=0.f || logical_h<=0.f) return;
	const float restitution=.22f, angular_kick=.22f;
	struct shady_toplevel *t;
	wl_list_for_each(t,&server->toplevels,link) {
		if(shady_fps_is_holding(server,t)||shady_fps_is_expanded(server,t)){shady_physics_stop(t);continue;}
		struct wlr_surface *surface=t->xdg_toplevel->base->surface;
		if(!surface->mapped || !t->scene_tree->node.enabled)continue;
		float tw=(float)surface->current.width,th=(float)surface->current.height;
		if(tw<=0.f||th<=0.f)continue;
		/* Folded FPS windows are authoritative cubes. Collision deliberately
		 * ignores visual tilt so a wobble cannot shrink the support footprint or
		 * move the bottom face through the floor. */
		const float cube_size=SHADY_FPS_CUBE_SIZE;
		float center_x=((float)t->scene_tree->node.x+tw*.5f-logical_w*.5f)/logical_h;
		float center_y=.5f-((float)t->scene_tree->node.y+th*.5f)/logical_h;
		if(center_y < WINDOW_RESPAWN_Y || fabsf(shady_spatial_toplevel_state(t)->z) > WINDOW_RESPAWN_Z_LIMIT){
			shady_physics_respawn_window(server,t);continue;
		}
		float half_x=cube_size*.5f,half_h=cube_size*.5f,half_z=cube_size*.5f;
		float previous_bottom=center_y-half_h;
		shady_physics_toplevel_state(t)->vy-=WINDOW_GRAVITY*dt;
		bool falling_before_sweep=shady_physics_toplevel_state(t)->vy<=0.f;

		/* Substep fast diagonal throws. A single axis-separated sweep can miss
		 * an edge when another coordinate enters a collider during the same
		 * frame (for example wall + floor). Keep each substep below a quarter
		 * cube so every face gets a chance to become the active contact. */
		float center[3]={center_x,center_y,shady_spatial_toplevel_state(t)->z};
		const float half[3]={half_x,half_h,half_z};
		float max_move=fmaxf(fabsf(shady_physics_toplevel_state(t)->vx*dt),
			fmaxf(fabsf(shady_physics_toplevel_state(t)->vy*dt),fabsf(shady_physics_toplevel_state(t)->vz*dt)));
		float max_step=cube_size*.25f;
		int steps=(int)ceilf(max_move/max_step);
		if(steps<1)steps=1;
		if(steps>16)steps=16;
		float step_dt=dt/(float)steps;
		bool hit_x=false,hit_y=false,hit_z=false;
		for(int step=0;step<steps;step++){
			hit_y|=shady_physics_sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,1,
				shady_physics_toplevel_state(t)->vy*step_dt,&shady_physics_toplevel_state(t)->vy,restitution);
			hit_x|=shady_physics_sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,0,
				shady_physics_toplevel_state(t)->vx*step_dt,&shady_physics_toplevel_state(t)->vx,restitution);
			hit_z|=shady_physics_sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,2,
				shady_physics_toplevel_state(t)->vz*step_dt,&shady_physics_toplevel_state(t)->vz,restitution);
		}
		center_x=center[0];center_y=center[1];shady_spatial_toplevel_state(t)->z=center[2];

		if(hit_x)shady_window_motion_add_impulse(server,t,
			shady_physics_toplevel_state(t)->vx>=0.f?.018f:-.018f,0.f,0.f,
			shady_physics_toplevel_state(t)->vx>=0.f?angular_kick:-angular_kick);
		if(hit_z)shady_window_motion_add_impulse(server,t,0.f,
			shady_physics_toplevel_state(t)->vz>=0.f?.018f:-.018f,
			shady_physics_toplevel_state(t)->vz>=0.f?angular_kick:-angular_kick,0.f);
		/* A downward Y sweep is already a real ground contact even though the
		 * restitution response has flipped vy positive by this point. Apply an
		 * impact friction impulse here; otherwise a thrown cube spends most of
		 * its time in tiny bounces and keeps almost all of its horizontal speed. */
		bool ground_impact=hit_y&&falling_before_sweep;
		if(ground_impact){
			shady_physics_toplevel_state(t)->vx*=WINDOW_GROUND_IMPACT_TANGENT_RETAIN;
			shady_physics_toplevel_state(t)->vz*=WINDOW_GROUND_IMPACT_TANGENT_RETAIN;
		}

		struct shady_box_collider body={
			center_x-half_x,center_x+half_x,
			center_y-half_h,center_y+half_h,
			shady_spatial_toplevel_state(t)->z-half_z,shady_spatial_toplevel_state(t)->z+half_z
		};
		float support_y=0.f; bool supported=false;
		/* Keep resting windows attached to a support despite tiny frame-to-frame
		 * body/tilt changes. For larger gaps still require an actual downward
		 * crossing so elevated colliders cannot pull a window upward. */
		if (shady_physics_toplevel_state(t)->vy <= 0.f) {
			const float contact_slop=.025f;
			for (size_t i=0;i<shady_spatial_state(server)->runtime.world.collider_count;i++) {
				const struct shady_box_collider *b=&shady_spatial_state(server)->runtime.world.colliders[i];
				float y=b->max_y;
				if(!shady_box_overlap_xz(b,&body))continue;
				bool crossed=previous_bottom>=y && body.min_y<=y;
				bool resting=previous_bottom>=y-contact_slop &&
					previous_bottom<=y+contact_slop && body.min_y<=y+contact_slop;
				if((crossed||resting)&&(!supported||y>support_y)){support_y=y;supported=true;}
			}
		}
		float floor_center=support_y+half_h;
		if(supported){
			float impact=-shady_physics_toplevel_state(t)->vy;center_y=floor_center;
			if(impact>.12f){
				shady_physics_toplevel_state(t)->vy=impact*restitution;
				/* Folded cubes have no tilt-dependent collision body. Keep the
				 * landing kick deterministic and let window motion animate it. */
				float side=shady_physics_toplevel_state(t)->vx>=0.f?1.f:-1.f;
				shady_window_motion_add_impulse(server,t,side*impact*.018f,
					impact*.035f,side*impact*angular_kick,0.f);
			}
			else shady_physics_toplevel_state(t)->vy=0.f;
			float friction=1.f-WINDOW_REST_FRICTION_RATE*dt;
			if(friction<0.f)friction=0.f;
			shady_window_motion_apply_damping(t,friction);
			shady_physics_toplevel_state(t)->vx*=friction;
			shady_physics_toplevel_state(t)->vz*=friction;
		}
		if(ground_impact||supported){
			if(fabsf(shady_physics_toplevel_state(t)->vx)<WINDOW_REST_SPEED_EPSILON)
				shady_physics_toplevel_state(t)->vx=0.f;
			if(fabsf(shady_physics_toplevel_state(t)->vz)<WINDOW_REST_SPEED_EPSILON)
				shady_physics_toplevel_state(t)->vz=0.f;
		}
		int x=(int)(center_x*logical_h+logical_w*.5f-tw*.5f);int y=(int)((.5f-center_y)*logical_h-th*.5f);
		wlr_scene_node_set_position(&t->scene_tree->node,x,y);
	}
}

void shady_physics_respawn_window(struct shady_server *server,struct shady_toplevel *t){
	if(!t)return;
	struct wlr_surface *sf=t->xdg_toplevel->base->surface;
	if(!sf||sf->current.height<=0)return;
	float lw=(float)sf->current.width,lh=(float)sf->current.height;
	if(!wl_list_empty(&server->outputs)){
		struct shady_output *out=wl_container_of(server->outputs.next,out,link);
		if(out->wlr_output&&out->wlr_output->scale>0.f){lw=(float)out->wlr_output->width/out->wlr_output->scale;lh=(float)out->wlr_output->height/out->wlr_output->scale;}
	}
	wlr_scene_node_set_position(&t->scene_tree->node,(int)(lw*.5f-sf->current.width*.5f),(int)(lh*.35f-sf->current.height*.5f));
	shady_spatial_toplevel_state(t)->z=-.65f;shady_physics_stop(t);shady_window_motion_reset(t);
}
void shady_physics_respawn_all(struct shady_server *server){struct shady_toplevel*t;wl_list_for_each(t,&server->toplevels,link)shady_physics_respawn_window(server,t);shady_render_schedule_all_outputs(server);}

void shady_physics_set_velocity(struct shady_toplevel *toplevel,float vx,float vy,float vz){
	if(!toplevel)return;
	struct shady_window_physics_state *state=shady_physics_toplevel_state(toplevel);
	if(!state)return;
	state->vx=vx;state->vy=vy;state->vz=vz;
}
void shady_physics_stop(struct shady_toplevel *toplevel){shady_physics_set_velocity(toplevel,0.f,0.f,0.f);}
