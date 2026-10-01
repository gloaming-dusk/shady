#include "physics.h"
#include "../spatial/state.h"
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

static float dot3(const float a[3],const float b[3]){
	return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
static bool sat_axis(const float v[3][3],const float h[3],const float axis[3]){
	float l2=dot3(axis,axis);if(l2<1e-12f)return true;
	float p0=dot3(v[0],axis),p1=dot3(v[1],axis),p2=dot3(v[2],axis);
	float mn=fminf(p0,fminf(p1,p2)),mx=fmaxf(p0,fmaxf(p1,p2));
	float r=h[0]*fabsf(axis[0])+h[1]*fabsf(axis[1])+h[2]*fabsf(axis[2]);
	return !(mn>r||mx<-r);
}
static bool triangle_cube_overlap(const struct shady_triangle_collider*t,const float c[3],const float h[3]){
	/* Exact triangle-vs-AABB SAT: 3 box axes, triangle normal, and the
	 * 9 cross products between triangle edges and box axes. */
	float v[3][3];for(int i=0;i<3;i++)for(int a=0;a<3;a++)v[i][a]=t->v[i][a]-c[a];
	for(int a=0;a<3;a++){
		float mn=fminf(v[0][a],fminf(v[1][a],v[2][a]));
		float mx=fmaxf(v[0][a],fmaxf(v[1][a],v[2][a]));
		if(mn>h[a]||mx<-h[a])return false;
	}
	float e[3][3];for(int i=0;i<3;i++)for(int a=0;a<3;a++)e[i][a]=v[(i+1)%3][a]-v[i][a];
	float n[3]={e[0][1]*e[1][2]-e[0][2]*e[1][1],
	            e[0][2]*e[1][0]-e[0][0]*e[1][2],
	            e[0][0]*e[1][1]-e[0][1]*e[1][0]};
	if(!sat_axis(v,h,n))return false;
	const float box_axis[3][3]={{1,0,0},{0,1,0},{0,0,1}};
	for(int i=0;i<3;i++)for(int j=0;j<3;j++){
		float axis[3]={
			e[i][1]*box_axis[j][2]-e[i][2]*box_axis[j][1],
			e[i][2]*box_axis[j][0]-e[i][0]*box_axis[j][2],
			e[i][0]*box_axis[j][1]-e[i][1]*box_axis[j][0]
		};
		if(!sat_axis(v,h,axis))return false;
	}
	return true;
}
static bool world_has_triangle_contact(const struct shady_world*w,const float c[3],const float h[3]){
	for(size_t i=0;i<w->triangle_count;i++){
		const struct shady_triangle_collider*t=&w->triangles[i];
		bool broad=true;for(int a=0;a<3;a++)if(t->max[a]<c[a]-h[a]||t->min[a]>c[a]+h[a]){broad=false;break;}
		if(broad&&triangle_cube_overlap(t,c,h))return true;
	}
	return false;
}

/* Axis-separated cube movement. Built-in box colliders retain their swept
 * contact behavior. Authored OBJ group boxes are broad-phase/debug only;
 * their actual faces are tested below with triangle SAT. */
static bool sweep_cube_axis(const struct shady_world *world,float center[3],
		const float half[3],int axis,float delta,float *velocity,float restitution){
	if(fabsf(delta)<1e-8f)return false;
	int a=(axis+1)%3,b=(axis+2)%3;float start=center[axis],next=start+delta,best=next;bool hit=false;
	/* Slot zero is Shady's built-in floor. OBJ boxes follow it and must not
	 * become solid volumes, otherwise slopes/empty space turn into walls. */
	size_t box_count=world->triangle_count?1:world->collider_count;
	for(size_t i=0;i<box_count;i++){
		const struct shady_box_collider*c=&world->colliders[i];
		const float mn[3]={c->min_x,c->min_y,c->min_z},mx[3]={c->max_x,c->max_y,c->max_z};
		if(center[a]+half[a]<=mn[a]||center[a]-half[a]>=mx[a]||
		   center[b]+half[b]<=mn[b]||center[b]-half[b]>=mx[b])continue;
		if(delta>0.f&&start+half[axis]<=mn[axis]&&next+half[axis]>=mn[axis]){
			float q=mn[axis]-half[axis];if(!hit||q<best){best=q;hit=true;}
		}else if(delta<0.f&&start-half[axis]>=mx[axis]&&next-half[axis]<=mx[axis]){
			float q=mx[axis]+half[axis];if(!hit||q>best){best=q;hit=true;}
		}
	}
	if(!hit&&world->triangle_count){
		float probe[3]={center[0],center[1],center[2]};probe[axis]=next;
		if(world_has_triangle_contact(world,probe,half)){best=start;hit=true;}
	}
	center[axis]=hit?best:next;
	if(hit)*velocity=-*velocity*restitution;
	return hit;
}

void shady_physics_move_cube(const struct shady_world*world,float center[3],
		const float target[3],float half_size){
	float delta[3]={target[0]-center[0],target[1]-center[1],target[2]-center[2]};
	float max_move=fmaxf(fabsf(delta[0]),fmaxf(fabsf(delta[1]),fabsf(delta[2])));
	float max_step=half_size*.5f;
	int steps=(int)ceilf(max_move/max_step);if(steps<1)steps=1;if(steps>64)steps=64;
	float step[3]={delta[0]/steps,delta[1]/steps,delta[2]/steps};
	const float half[3]={half_size,half_size,half_size};
	float velocity=0.f;
	for(int i=0;i<steps;i++){
		sweep_cube_axis(world,center,half,1,step[1],&velocity,0.f);
		sweep_cube_axis(world,center,half,0,step[0],&velocity,0.f);
		sweep_cube_axis(world,center,half,2,step[2],&velocity,0.f);
	}
}

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
	shady_spatial_state(server)->runtime.physics.gravity_enabled=server->config.physics_enabled && server->config.window_gravity;
}
void shady_physics_toggle_gravity(struct shady_server *server) {
	if (!server->config.physics_enabled) return;
	shady_spatial_state(server)->runtime.physics.gravity_enabled=!shady_spatial_state(server)->runtime.physics.gravity_enabled;
	struct shady_toplevel *t;
	wl_list_for_each(t,&server->toplevels,link) shady_spatial_toplevel_state(t)->physics.vy=0.f;
	shady_render_schedule_all_outputs(server);
}
void shady_physics_update(struct shady_server *server,float dt,float logical_w,float logical_h) {
	if(!server->config.physics_enabled || !shady_spatial_state(server)->runtime.physics.gravity_enabled || !shady_spatial_state(server)->runtime.camera.first_person || dt<=0.f || logical_w<=0.f || logical_h<=0.f) return;
	const float restitution=.22f, friction_rate=7.f, angular_kick=.22f;
	struct shady_toplevel *t;
	wl_list_for_each(t,&server->toplevels,link) {
		if(shady_fps_is_holding(server,t)||shady_fps_is_expanded(server,t)){shady_physics_stop(t);continue;}
		struct wlr_surface *surface=t->xdg_toplevel->base->surface;
		if(!surface->mapped)continue;
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
		shady_spatial_toplevel_state(t)->physics.vy-=WINDOW_GRAVITY*dt;

		/* Substep fast diagonal throws. A single axis-separated sweep can miss
		 * an edge when another coordinate enters a collider during the same
		 * frame (for example wall + floor). Keep each substep below a quarter
		 * cube so every face gets a chance to become the active contact. */
		float center[3]={center_x,center_y,shady_spatial_toplevel_state(t)->z};
		const float half[3]={half_x,half_h,half_z};
		float max_move=fmaxf(fabsf(shady_spatial_toplevel_state(t)->physics.vx*dt),
			fmaxf(fabsf(shady_spatial_toplevel_state(t)->physics.vy*dt),fabsf(shady_spatial_toplevel_state(t)->physics.vz*dt)));
		float max_step=cube_size*.25f;
		int steps=(int)ceilf(max_move/max_step);
		if(steps<1)steps=1;
		if(steps>16)steps=16;
		float step_dt=dt/(float)steps;
		bool hit_x=false,hit_y=false,hit_z=false;
		for(int step=0;step<steps;step++){
			hit_y|=sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,1,
				shady_spatial_toplevel_state(t)->physics.vy*step_dt,&shady_spatial_toplevel_state(t)->physics.vy,restitution);
			hit_x|=sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,0,
				shady_spatial_toplevel_state(t)->physics.vx*step_dt,&shady_spatial_toplevel_state(t)->physics.vx,restitution);
			hit_z|=sweep_cube_axis(&shady_spatial_state(server)->runtime.world,center,half,2,
				shady_spatial_toplevel_state(t)->physics.vz*step_dt,&shady_spatial_toplevel_state(t)->physics.vz,restitution);
		}
		center_x=center[0];center_y=center[1];shady_spatial_toplevel_state(t)->z=center[2];

		if(hit_x)shady_window_motion_add_impulse(server,t,
			shady_spatial_toplevel_state(t)->physics.vx>=0.f?.018f:-.018f,0.f,0.f,
			shady_spatial_toplevel_state(t)->physics.vx>=0.f?angular_kick:-angular_kick);
		if(hit_z)shady_window_motion_add_impulse(server,t,0.f,
			shady_spatial_toplevel_state(t)->physics.vz>=0.f?.018f:-.018f,
			shady_spatial_toplevel_state(t)->physics.vz>=0.f?angular_kick:-angular_kick,0.f);
		(void)hit_y;

		struct shady_box_collider body={
			center_x-half_x,center_x+half_x,
			center_y-half_h,center_y+half_h,
			shady_spatial_toplevel_state(t)->z-half_z,shady_spatial_toplevel_state(t)->z+half_z
		};
		float support_y=0.f; bool supported=false;
		/* Keep resting windows attached to a support despite tiny frame-to-frame
		 * body/tilt changes. For larger gaps still require an actual downward
		 * crossing so elevated colliders cannot pull a window upward. */
		if (shady_spatial_toplevel_state(t)->physics.vy <= 0.f) {
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
			float impact=-shady_spatial_toplevel_state(t)->physics.vy;center_y=floor_center;
			if(impact>.12f){
				shady_spatial_toplevel_state(t)->physics.vy=impact*restitution;
				/* Folded cubes have no tilt-dependent collision body. Keep the
				 * landing kick deterministic and let window motion animate it. */
				float side=shady_spatial_toplevel_state(t)->physics.vx>=0.f?1.f:-1.f;
				shady_window_motion_add_impulse(server,t,side*impact*.018f,
					impact*.035f,side*impact*angular_kick,0.f);
			}
			else shady_spatial_toplevel_state(t)->physics.vy=0.f;
			float friction=1.f-friction_rate*dt;if(friction<0.f)friction=0.f;shady_window_motion_apply_damping(t,friction);shady_spatial_toplevel_state(t)->physics.vx*=friction;shady_spatial_toplevel_state(t)->physics.vz*=friction;
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
	shady_spatial_toplevel_state(t)->z=-.65f;shady_physics_stop(t);shady_spatial_toplevel_state(t)->motion.tilt_x=shady_spatial_toplevel_state(t)->motion.tilt_y=shady_spatial_toplevel_state(t)->motion.tilt_vx=shady_spatial_toplevel_state(t)->motion.tilt_vy=0.f;
}
void shady_physics_respawn_all(struct shady_server *server){struct shady_toplevel*t;wl_list_for_each(t,&server->toplevels,link)shady_physics_respawn_window(server,t);shady_render_schedule_all_outputs(server);}

void shady_physics_set_velocity(struct shady_toplevel *toplevel,float vx,float vy,float vz){shady_spatial_toplevel_state(toplevel)->physics.vx=vx;shady_spatial_toplevel_state(toplevel)->physics.vy=vy;shady_spatial_toplevel_state(toplevel)->physics.vz=vz;}
void shady_physics_stop(struct shady_toplevel *toplevel){shady_physics_set_velocity(toplevel,0.f,0.f,0.f);}
