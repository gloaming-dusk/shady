#include "fps.h"
#include "../spatial/state.h"
#include "state.h"
#include <linux/input-event-codes.h>
#include <math.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "../../render/math3d.h"
#include "../../render/pick3d.h"
#include "../../render/render.h"
#include "../physics/physics.h"
#include "../physics/collision.h"
#include "../window_motion/window_motion.h"
#include "../window_motion/state.h"
#include "../../world/world.h"
#define LOOK_SENS .0032f
#define HOLD_MIN .28f
#define HOLD_MAX 2.50f
#define EYE_HEIGHT .40f
#define PLAYER_RADIUS .10f
#define STEP_HEIGHT .46f
#define MOVE_SPEED 1.25f
#define GRAVITY 3.8f
#define JUMP_SPEED 1.45f
static void clamp_pitch(struct shady_camera*c){if(c->pitch>1.4f)c->pitch=1.4f;if(c->pitch<-1.4f)c->pitch=-1.4f;}
static void center_cursor(struct shady_server*s){
	struct wlr_output*o=wlr_output_layout_output_at(s->output_layout,s->cursor->x,s->cursor->y);
	if(!o&&!wl_list_empty(&s->outputs)){struct shady_output*out=wl_container_of(s->outputs.next,out,link);o=out->wlr_output;}
	if(!o)return;
	double ox=0,oy=0;wlr_output_layout_output_coords(s->output_layout,o,&ox,&oy);
	float sc=o->scale>0?o->scale:1.f;
	wlr_cursor_warp(s->cursor,NULL,ox+(double)o->width/sc*.5,oy+(double)o->height/sc*.5);
}
bool shady_fps_toggle(struct shady_server*s){
	if(!s->config.fps_mode)return true;
	struct shady_camera*c=&shady_spatial_state(s)->runtime.camera;
	struct shady_fps_state*f=shady_fps_state_for(s);
	bool entering=!c->first_person;
	f->forward=f->back=f->left=f->right=false;
	f->jump_queued=false;
	f->held_toplevel=NULL;
	if(entering){
		struct shady_toplevel*t;wl_list_for_each(t,&s->toplevels,link){
			struct shady_fps_toplevel_state*tw=shady_fps_toplevel_state(t);
			tw->entry_saved=true;tw->entry_x=t->scene_tree->node.x;tw->entry_y=t->scene_tree->node.y;
			tw->entry_z=shady_spatial_toplevel_state(t)->z;
		}
		struct shady_vec3 eye;shady_camera_eye(c,&eye);
		f->orbit_yaw=c->yaw;f->orbit_pitch=c->pitch;f->orbit_distance=c->distance;
		f->orbit_target_x=c->target_x;f->orbit_target_y=c->target_y;f->orbit_target_z=c->target_z;
		f->orbit_saved=true;
		c->pos_x=eye.x;c->pos_y=eye.y;c->pos_z=eye.z;c->vel_y=0;c->grounded=false;
		c->first_person=true;f->input_capture=true;
		wlr_seat_pointer_clear_focus(s->seat);center_cursor(s);
	}else{
		struct shady_toplevel*t;wl_list_for_each(t,&s->toplevels,link){
			struct shady_fps_toplevel_state*tw=shady_fps_toplevel_state(t);
			if(tw->entry_saved){
				wlr_scene_node_set_position(&t->scene_tree->node,tw->entry_x,tw->entry_y);
				shady_spatial_toplevel_state(t)->z=tw->entry_z;
				tw->entry_saved=false;
				shady_physics_stop(t);shady_window_motion_reset(t);
			}
		}
		if(f->expanded_toplevel){
			shady_fps_toplevel_state(f->expanded_toplevel)->expanded=false;
			f->expanded_toplevel=NULL;
		}
		c->first_person=false;f->input_capture=false;c->vel_y=0;c->grounded=false;
		if(f->orbit_saved){
			c->yaw=f->orbit_yaw;c->pitch=f->orbit_pitch;c->distance=f->orbit_distance;
			c->target_x=f->orbit_target_x;c->target_y=f->orbit_target_y;c->target_z=f->orbit_target_z;
			f->orbit_saved=false;
		}
	}
	shady_render_schedule_all_outputs(s);return true;
}
bool shady_fps_toggle_capture(struct shady_server*s){
	if(!shady_spatial_state(s)->runtime.camera.first_person)return false;
	if(shady_fps_state_for(s)->expanded_toplevel){
		struct shady_toplevel *expanded = shady_fps_state_for(s)->expanded_toplevel;
		shady_fps_toplevel_state(expanded)->expanded=false;
		shady_fps_state_for(s)->expanded_toplevel=NULL;
		shady_fps_state_for(s)->input_capture=true;
		wlr_seat_pointer_clear_focus(s->seat);center_cursor(s);
	}else{
		struct shady_toplevel*t=shady_toplevel_at_camera_center(s,NULL);
		if(!t)return true;
		shady_fps_toplevel_state(t)->expanded=true;shady_fps_state_for(s)->expanded_toplevel=t;shady_fps_state_for(s)->input_capture=false;
		shady_physics_stop(t);focus_toplevel(t);
	}
	shady_fps_state_for(s)->forward=shady_fps_state_for(s)->back=shady_fps_state_for(s)->left=shady_fps_state_for(s)->right=false;shady_fps_state_for(s)->jump_queued=false;
	shady_render_schedule_all_outputs(s);return true;
}
bool shady_fps_handle_key(struct shady_server*s,const xkb_keysym_t*syms,int n,uint32_t state){if(!shady_spatial_state(s)->runtime.camera.first_person||!shady_fps_state_for(s)->input_capture)return false;bool p=state==WL_KEYBOARD_KEY_STATE_PRESSED,handled=false;for(int j=0;j<n;j++)switch(syms[j]){case XKB_KEY_w:case XKB_KEY_W:shady_fps_state_for(s)->forward=p;handled=true;break;case XKB_KEY_s:case XKB_KEY_S:shady_fps_state_for(s)->back=p;handled=true;break;case XKB_KEY_a:case XKB_KEY_A:shady_fps_state_for(s)->left=p;handled=true;break;case XKB_KEY_d:case XKB_KEY_D:shady_fps_state_for(s)->right=p;handled=true;break;case XKB_KEY_space:if(p)shady_fps_state_for(s)->jump_queued=true;handled=true;break;default:break;}if(handled)shady_render_schedule_all_outputs(s);return handled;}
bool shady_fps_handle_motion(struct shady_server*s,double dx,double dy){if(!shady_spatial_state(s)->runtime.camera.first_person||!shady_fps_state_for(s)->input_capture)return false;shady_spatial_state(s)->runtime.camera.yaw-=(float)dx*LOOK_SENS;shady_spatial_state(s)->runtime.camera.pitch-=(float)dy*LOOK_SENS;clamp_pitch(&shady_spatial_state(s)->runtime.camera);wlr_seat_pointer_clear_focus(s->seat);shady_render_schedule_all_outputs(s);return true;}
bool shady_fps_handle_button(struct shady_server*s,uint32_t button,uint32_t state){if(!shady_spatial_state(s)->runtime.camera.first_person||!shady_fps_state_for(s)->input_capture)return false;if(button==BTN_RIGHT&&state==WL_POINTER_BUTTON_STATE_PRESSED&&shady_fps_state_for(s)->held_toplevel){struct shady_vec3 f;shady_camera_basis(&shady_spatial_state(s)->runtime.camera,NULL,NULL,&f);struct shady_toplevel*t=shady_fps_state_for(s)->held_toplevel;float v=2.6f;shady_physics_set_velocity(t,f.x*v,f.y*v+shady_spatial_state(s)->runtime.camera.vel_y,f.z*v);shady_window_motion_add_impulse(s,t,f.x*.035f,f.y*.035f,-f.y*1.1f,f.x*.7f);shady_fps_state_for(s)->held_toplevel=NULL;shady_render_schedule_all_outputs(s);return true;}if(button==BTN_LEFT&&state==WL_POINTER_BUTTON_STATE_PRESSED){if(shady_fps_state_for(s)->held_toplevel)shady_fps_state_for(s)->held_toplevel=NULL;else{float d=0,hx=0,hy=0,hz=0;struct wlr_output*o=NULL;struct shady_toplevel*t=shady_toplevel_at_camera_center_hit_output(s,&d,&hx,&hy,&hz,&o);if(t&&d<=HOLD_MAX){struct wlr_surface*sf=t->xdg_toplevel->base->surface;if(o&&sf->current.height>0){double ox=0,oy=0;wlr_output_layout_output_coords(s->output_layout,o,&ox,&oy);float lw=(float)o->width/o->scale,lh=(float)o->height/o->scale,tw=(float)sf->current.width,th=(float)sf->current.height;float cx=((float)(t->scene_tree->node.x+ox)+tw*.5f-lw*.5f)/lh;float cy=(lh*.5f-((float)(t->scene_tree->node.y+oy)+th*.5f))/lh;shady_fps_state_for(s)->grab_offset_x=hx-cx;shady_fps_state_for(s)->grab_offset_y=hy-cy;shady_fps_state_for(s)->grab_offset_z=hz-shady_spatial_toplevel_state(t)->z;
float tilt_x=0.f,tilt_y=0.f;shady_window_motion_get_tilt(t,&tilt_x,&tilt_y);float model[16],inv[16];shady_window_model(model,(float)(t->scene_tree->node.x+ox),(float)(t->scene_tree->node.y+oy),tw,th,lw,lh,shady_spatial_toplevel_state(t)->z,tilt_x,tilt_y);if(shady_mat4_invert(inv,model)){shady_fps_state_for(s)->grab_local_x=inv[0]*hx+inv[4]*hy+inv[8]*hz+inv[12]-.5f;shady_fps_state_for(s)->grab_local_y=inv[1]*hx+inv[5]*hy+inv[9]*hz+inv[13]-.5f;shady_fps_state_for(s)->grab_local_z=inv[2]*hx+inv[6]*hy+inv[10]*hz+inv[14];}else{shady_fps_state_for(s)->grab_local_x=shady_fps_state_for(s)->grab_local_y=shady_fps_state_for(s)->grab_local_z=0;}}else{shady_fps_state_for(s)->grab_offset_x=shady_fps_state_for(s)->grab_offset_y=shady_fps_state_for(s)->grab_offset_z=0;shady_fps_state_for(s)->grab_local_x=shady_fps_state_for(s)->grab_local_y=shady_fps_state_for(s)->grab_local_z=0;}shady_fps_state_for(s)->held_toplevel=t;shady_fps_state_for(s)->hold_distance=d;if(shady_fps_state_for(s)->hold_distance<HOLD_MIN)shady_fps_state_for(s)->hold_distance=HOLD_MIN;focus_toplevel(t);}}shady_render_schedule_all_outputs(s);}return true;}
bool shady_fps_handle_axis(struct shady_server*s,struct wlr_pointer_axis_event*e){if(!shady_spatial_state(s)->runtime.camera.first_person||!shady_fps_state_for(s)->input_capture||!shady_fps_state_for(s)->held_toplevel||e->orientation!=WL_POINTER_AXIS_VERTICAL_SCROLL)return false;shady_fps_state_for(s)->hold_distance+=(float)e->delta*.0025f;if(shady_fps_state_for(s)->hold_distance<HOLD_MIN)shady_fps_state_for(s)->hold_distance=HOLD_MIN;if(shady_fps_state_for(s)->hold_distance>HOLD_MAX)shady_fps_state_for(s)->hold_distance=HOLD_MAX;shady_render_schedule_all_outputs(s);return true;}
static void sync_movement_keys(struct shady_server *s) {
	struct shady_fps_state *f = shady_fps_state_for(s);
	if (!shady_spatial_state(s)->runtime.camera.first_person || !f->input_capture) return;
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(s->seat);
	if (!keyboard) return;
	bool forward=false,back=false,left=false,right=false;
	for (size_t i=0;i<keyboard->num_keycodes;i++) {
		switch (keyboard->keycodes[i]) {
		case KEY_W: forward=true; break;
		case KEY_S: back=true; break;
		case KEY_A: left=true; break;
		case KEY_D: right=true; break;
		default: break;
		}
	}
	f->forward=forward;f->back=back;f->left=left;f->right=right;
}

static bool player_hits_solid(const struct shady_world*w,float x,float eye_y,float z){
	float feet=eye_y-EYE_HEIGHT, head=eye_y+.05f;
	struct shady_box_collider body={x-PLAYER_RADIUS,x+PLAYER_RADIUS,feet+.01f,head,z-PLAYER_RADIUS,z+PLAYER_RADIUS};
	for(size_t i=0;i<w->collider_count;i++){
		const struct shady_box_collider*b=&w->colliders[i];
		if(body.max_x>b->min_x&&body.min_x<b->max_x&&body.max_z>b->min_z&&body.min_z<b->max_z&&
				body.max_y>b->min_y&&body.min_y<b->max_y)return true;
	}
	return false;
}
static bool player_step_y(const struct shady_world*w,float x,float feet_y,float z,float*out){
	bool found=false;float best=feet_y;
	struct shady_box_collider foot={x-PLAYER_RADIUS,x+PLAYER_RADIUS,feet_y,feet_y,z-PLAYER_RADIUS,z+PLAYER_RADIUS};
	for(size_t i=0;i<w->collider_count;i++){
		const struct shady_box_collider*b=&w->colliders[i];float rise=b->max_y-feet_y;
		if(rise>.001f&&rise<=STEP_HEIGHT&&shady_box_overlap_xz(b,&foot)&&(!found||b->max_y>best)){best=b->max_y;found=true;}
	}
	if (found && out) *out = best;
	return found;
}
void shady_fps_update(struct shady_server*s,float dt){
	struct shady_camera*c=&shady_spatial_state(s)->runtime.camera;if(!c->first_person)return;
	sync_movement_keys(s);
	const struct shady_world *world = &shady_spatial_state(s)->runtime.world;
	float sy=sinf(c->yaw),cy=cosf(c->yaw),fx=-sy,fz=-cy,rx=cy,rz=-sy,mx=0,mz=0;
	if(shady_fps_state_for(s)->forward){mx+=fx;mz+=fz;}if(shady_fps_state_for(s)->back){mx-=fx;mz-=fz;}if(shady_fps_state_for(s)->right){mx+=rx;mz+=rz;}if(shady_fps_state_for(s)->left){mx-=rx;mz-=rz;}
	float ml=sqrtf(mx*mx+mz*mz);
	if(ml>.001f){
		float dx=mx/ml*MOVE_SPEED*dt,dz=mz/ml*MOVE_SPEED*dt,feet=c->pos_y-EYE_HEIGHT,step;
		/* Resolve horizontal movement one axis at a time. This prevents entering
		 * box sides and naturally slides along them. A grounded player may
		 * replace a blocked move with a small step onto the collider. */
		float nx=c->pos_x+dx;
		if(!player_hits_solid(world,nx,c->pos_y,c->pos_z))c->pos_x=nx;
		else if(c->grounded&&player_step_y(world,nx,feet,c->pos_z,&step)){c->pos_y=step+EYE_HEIGHT;c->pos_x=nx;c->vel_y=0.f;}
		float nz=c->pos_z+dz;feet=c->pos_y-EYE_HEIGHT;
		if(!player_hits_solid(world,c->pos_x,c->pos_y,nz))c->pos_z=nz;
		else if(c->grounded&&player_step_y(world,c->pos_x,feet,nz,&step)){c->pos_y=step+EYE_HEIGHT;c->pos_z=nz;c->vel_y=0.f;}
	}
	if(shady_fps_state_for(s)->jump_queued&&c->grounded){c->vel_y=JUMP_SPEED;c->grounded=false;}shady_fps_state_for(s)->jump_queued=false;
	float previous_feet=c->pos_y-EYE_HEIGHT;c->vel_y-=GRAVITY*dt;c->pos_y+=c->vel_y*dt;float next_feet=c->pos_y-EYE_HEIGHT;
	struct shady_box_collider feet={c->pos_x-PLAYER_RADIUS,c->pos_x+PLAYER_RADIUS,next_feet,next_feet,c->pos_z-PLAYER_RADIUS,c->pos_z+PLAYER_RADIUS};
	bool landed=false;float support_y=0.f;
	for(size_t i=0;i<world->collider_count;i++){const struct shady_box_collider*b=&world->colliders[i];float y=b->max_y;
		if(c->vel_y<=0.f&&previous_feet>=y&&next_feet<=y&&shady_box_overlap_xz(b,&feet)&&(!landed||y>support_y)){support_y=y;landed=true;}}
	if(landed){c->pos_y=support_y+EYE_HEIGHT;c->vel_y=0.f;c->grounded=true;}else c->grounded=false;
}
void shady_fps_update_held_window(struct shady_server*s,float lw,float lh){
	struct shady_toplevel*t=shady_fps_state_for(s)->held_toplevel;if(!shady_spatial_state(s)->runtime.camera.first_person||!t)return;
	struct wlr_surface*surface=t->xdg_toplevel->base->surface;
	if(!surface->mapped||lh<=0){shady_fps_state_for(s)->held_toplevel=NULL;return;}
	struct shady_vec3 eye,f;shady_camera_eye(&shady_spatial_state(s)->runtime.camera,&eye);shady_camera_basis(&shady_spatial_state(s)->runtime.camera,NULL,NULL,&f);
	float d=shady_fps_state_for(s)->hold_distance,tw=(float)surface->current.width,th=(float)surface->current.height;
	float target[3]={eye.x+f.x*d,eye.y+f.y*d,eye.z+f.z*d};
	float current[3]={
		((float)t->scene_tree->node.x+tw*.5f-lw*.5f)/lh,
		.5f-((float)t->scene_tree->node.y+th*.5f)/lh,
		shady_spatial_toplevel_state(t)->z
	};
	/* Held windows resolve the same provider model/collision contract used by
	 * rendering, picking and free physics. Preserve callback offsets while the
	 * compositor moves the underlying window center toward the hold target. */
	float half[3] = {tw/lh*.5f, th/lh*.5f, .006f};
	float model_offset[3] = {0.f, 0.f, 0.f};
	float collision_offset[3] = {0.f, 0.f, 0.f};
	float convex_vertices[256][3];
	size_t convex_vertex_count = 0;
	const uint16_t *convex_indices = NULL;
	size_t convex_index_count = 0;
	bool has_convex_hull = false;
	struct shady_representation_context representation_context = {
		.struct_size = sizeof(representation_context),
		.logical_width = lw, .logical_height = lh,
		.window_width = tw, .window_height = th,
		.center_x = current[0], .center_y = current[1], .center_z = current[2],
		.tilt_x = shady_window_motion_state_for_const(t)->tilt_x,
		.tilt_y = shady_window_motion_state_for_const(t)->tilt_y,
		.first_person = true, .folded = true, .held = true,
		.focused = !wl_list_empty(&s->toplevels) && s->toplevels.next == &t->link,
	};
	struct shady_representation_model representation_model;
	if (shady_toplevel_representation_model(t, &representation_context,
			&representation_model)) {
		struct shady_collision_box collision;
		shady_toplevel_representation_collision(t, &representation_context,
			&representation_model, &collision);
		half[0] = collision.half[0]; half[1] = collision.half[1];
		half[2] = collision.half[2];
		model_offset[0] = representation_model.center_x - current[0];
		model_offset[1] = representation_model.center_y - current[1];
		model_offset[2] = representation_model.center_z - current[2];
		struct shady_collision_hull hull = {0};
		if (shady_toplevel_representation_collision_hull(t,
				&representation_context, &hull) && hull.vertex_count <= 256) {
			float transform[16];
			shady_window_box_model(transform,
				representation_model.center_x, representation_model.center_y,
				representation_model.center_z,
				representation_model.width, representation_model.height,
				representation_model.depth,
				representation_model.tilt_x, representation_model.tilt_y);
			float minv[3] = {INFINITY, INFINITY, INFINITY};
			float maxv[3] = {-INFINITY, -INFINITY, -INFINITY};
			for (size_t i = 0; i < hull.vertex_count; ++i) {
				const struct shady_collision_vertex *v = &hull.vertices[i];
				float world[3] = {
					transform[0] * v->x + transform[4] * v->y + transform[8] * v->z + transform[12],
					transform[1] * v->x + transform[5] * v->y + transform[9] * v->z + transform[13],
					transform[2] * v->x + transform[6] * v->y + transform[10] * v->z + transform[14],
				};
				for (int axis = 0; axis < 3; ++axis) {
					convex_vertices[i][axis] = world[axis];
					if (world[axis] < minv[axis]) minv[axis] = world[axis];
					if (world[axis] > maxv[axis]) maxv[axis] = world[axis];
				}
			}
			for (int axis = 0; axis < 3; ++axis) {
				collision.center[axis] = (minv[axis] + maxv[axis]) * .5f;
				collision.half[axis] = fmaxf((maxv[axis] - minv[axis]) * .5f, .001f);
				for (size_t i = 0; i < hull.vertex_count; ++i)
					convex_vertices[i][axis] -= collision.center[axis];
			}
			half[0] = collision.half[0];
			half[1] = collision.half[1];
			half[2] = collision.half[2];
			convex_vertex_count = hull.vertex_count;
			convex_indices = hull.indices;
			convex_index_count = hull.index_count;
			has_convex_hull = true;
		}
		collision_offset[0] = collision.center[0] - representation_model.center_x;
		collision_offset[1] = collision.center[1] - representation_model.center_y;
		collision_offset[2] = collision.center[2] - representation_model.center_z;
		current[0] = collision.center[0]; current[1] = collision.center[1];
		current[2] = collision.center[2];
		target[0] += model_offset[0] + collision_offset[0];
		target[1] += model_offset[1] + collision_offset[1];
		target[2] += model_offset[2] + collision_offset[2];
	}
	if (has_convex_hull)
		shady_physics_move_convex(&shady_spatial_state(s)->runtime.world,
			current, target, half, convex_vertices, convex_vertex_count,
			convex_indices, convex_index_count);
	else
		shady_physics_move_box(&shady_spatial_state(s)->runtime.world,current,target,half);
	float base_x=current[0]-collision_offset[0]-model_offset[0];
	float base_y=current[1]-collision_offset[1]-model_offset[1];
	float base_z=current[2]-collision_offset[2]-model_offset[2];
	int x=(int)(base_x*lh+lw*.5f-tw*.5f);
	int y=(int)((.5f-base_y)*lh-th*.5f);
	wlr_scene_node_set_position(&t->scene_tree->node,x,y);shady_spatial_toplevel_state(t)->z=base_z;
	/* Keep the visual representation axis-aligned while held so rendering and
	 * collision/debug geometry cannot diverge as the camera rotates. */
	shady_window_motion_reset(t);
}
void shady_fps_toplevel_gone(struct shady_server*s,struct shady_toplevel*t){
	if(shady_fps_state_for(s)->held_toplevel==t)shady_fps_state_for(s)->held_toplevel=NULL;
	if(shady_fps_state_for(s)->expanded_toplevel==t)shady_fps_state_for(s)->expanded_toplevel=NULL;
}

bool shady_fps_is_holding(const struct shady_server*s,const struct shady_toplevel*t){const struct shady_fps_state*f=shady_fps_state_for_const(s);return f&&f->held_toplevel==t;}
bool shady_fps_has_held_window(const struct shady_server*s){const struct shady_fps_state*f=shady_fps_state_for_const(s);return f&&f->held_toplevel!=NULL;}
bool shady_fps_is_expanded(const struct shady_server*s,const struct shady_toplevel*t){const struct shady_fps_toplevel_state*w=t?shady_fps_toplevel_state_const(t):NULL;return shady_spatial_state_const(s)->runtime.camera.first_person&&w&&w->expanded;}
