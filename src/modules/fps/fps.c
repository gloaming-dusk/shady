#include "fps.h"
#include <linux/input-event-codes.h>
#include <math.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "../../render/math3d.h"
#include "../../render/pick3d.h"
#include "../../render/render.h"
#include "../physics/physics.h"
#include "../window_motion/window_motion.h"
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
#define FOLDED_CUBE_SIZE .16f
static void clamp_pitch(struct shady_camera*c){if(c->pitch>1.4f)c->pitch=1.4f;if(c->pitch<-1.4f)c->pitch=-1.4f;}
static void center_cursor(struct shady_server*s){
	struct wlr_output*o=wlr_output_layout_output_at(s->output_layout,s->cursor->x,s->cursor->y);
	if(!o&&!wl_list_empty(&s->outputs)){struct shady_output*out=wl_container_of(s->outputs.next,out,link);o=out->wlr_output;}
	if(!o)return;
	double ox=0,oy=0;wlr_output_layout_output_coords(s->output_layout,o,&ox,&oy);
	float sc=o->scale>0?o->scale:1.f;
	wlr_cursor_warp(s->cursor,NULL,ox+(double)o->width/sc*.5,oy+(double)o->height/sc*.5);
}
bool shady_fps_toggle(struct shady_server*s){if(!s->config.fps_mode)return true;s->experimental.camera.first_person=!s->experimental.camera.first_person;s->experimental.fps.forward=s->experimental.fps.back=s->experimental.fps.left=s->experimental.fps.right=false;s->experimental.fps.jump_queued=false;s->experimental.fps.held_toplevel=NULL;s->experimental.fps.input_capture=s->experimental.camera.first_person;if(s->experimental.camera.first_person){struct shady_vec3 eye;shady_camera_eye(&s->experimental.camera,&eye);s->experimental.camera.pos_x=eye.x;s->experimental.camera.pos_y=eye.y;s->experimental.camera.pos_z=eye.z;s->experimental.camera.vel_y=0;s->experimental.camera.grounded=false;wlr_seat_pointer_clear_focus(s->seat);center_cursor(s);}shady_render_schedule_all_outputs(s);return true;}
bool shady_fps_toggle_capture(struct shady_server*s){
	if(!s->experimental.camera.first_person)return false;
	if(s->experimental.fps.expanded_toplevel){
		s->experimental.fps.expanded_toplevel->experimental.fps_expanded=false;
		s->experimental.fps.expanded_toplevel=NULL;
		s->experimental.fps.input_capture=true;
		wlr_seat_pointer_clear_focus(s->seat);center_cursor(s);
	}else{
		struct shady_toplevel*t=shady_toplevel_at_camera_center(s,NULL);
		if(!t)return true;
		t->experimental.fps_expanded=true;s->experimental.fps.expanded_toplevel=t;s->experimental.fps.input_capture=false;
		shady_physics_stop(t);focus_toplevel(t);
	}
	s->experimental.fps.forward=s->experimental.fps.back=s->experimental.fps.left=s->experimental.fps.right=false;s->experimental.fps.jump_queued=false;
	shady_render_schedule_all_outputs(s);return true;
}
bool shady_fps_handle_key(struct shady_server*s,const xkb_keysym_t*syms,int n,uint32_t state){if(!s->experimental.camera.first_person||!s->experimental.fps.input_capture)return false;bool p=state==WL_KEYBOARD_KEY_STATE_PRESSED,handled=false;for(int j=0;j<n;j++)switch(syms[j]){case XKB_KEY_w:case XKB_KEY_W:s->experimental.fps.forward=p;handled=true;break;case XKB_KEY_s:case XKB_KEY_S:s->experimental.fps.back=p;handled=true;break;case XKB_KEY_a:case XKB_KEY_A:s->experimental.fps.left=p;handled=true;break;case XKB_KEY_d:case XKB_KEY_D:s->experimental.fps.right=p;handled=true;break;case XKB_KEY_space:if(p)s->experimental.fps.jump_queued=true;handled=true;break;default:break;}return handled;}
bool shady_fps_handle_motion(struct shady_server*s,double dx,double dy){if(!s->experimental.camera.first_person||!s->experimental.fps.input_capture)return false;s->experimental.camera.yaw-=(float)dx*LOOK_SENS;s->experimental.camera.pitch-=(float)dy*LOOK_SENS;clamp_pitch(&s->experimental.camera);struct wlr_output*o=wlr_output_layout_output_at(s->output_layout,s->cursor->x,s->cursor->y);if(o){double ox=0,oy=0;wlr_output_layout_output_coords(s->output_layout,o,&ox,&oy);float sc=o->scale>0?o->scale:1;wlr_cursor_warp(s->cursor,NULL,-ox+(double)o->width/sc*.5,-oy+(double)o->height/sc*.5);}wlr_seat_pointer_clear_focus(s->seat);shady_render_schedule_all_outputs(s);return true;}
bool shady_fps_handle_button(struct shady_server*s,uint32_t button,uint32_t state){if(!s->experimental.camera.first_person||!s->experimental.fps.input_capture)return false;if(button==BTN_RIGHT&&state==WL_POINTER_BUTTON_STATE_PRESSED&&s->experimental.fps.held_toplevel){struct shady_vec3 f;shady_camera_basis(&s->experimental.camera,NULL,NULL,&f);struct shady_toplevel*t=s->experimental.fps.held_toplevel;float v=2.6f;shady_physics_set_velocity(t,f.x*v,f.y*v+s->experimental.camera.vel_y,f.z*v);shady_window_motion_add_impulse(s,t,f.x*.035f,f.y*.035f,-f.y*1.1f,f.x*.7f);s->experimental.fps.held_toplevel=NULL;shady_render_schedule_all_outputs(s);return true;}if(button==BTN_LEFT&&state==WL_POINTER_BUTTON_STATE_PRESSED){if(s->experimental.fps.held_toplevel)s->experimental.fps.held_toplevel=NULL;else{float d=0,hx=0,hy=0,hz=0;struct shady_toplevel*t=shady_toplevel_at_camera_center_hit(s,&d,&hx,&hy,&hz);if(t&&d<=HOLD_MAX){struct wlr_surface*sf=t->xdg_toplevel->base->surface;struct wlr_output*o=NULL;if(!wl_list_empty(&s->outputs)){struct shady_output*out=wl_container_of(s->outputs.next,out,link);o=out->wlr_output;}if(o&&sf->current.height>0){double ox=0,oy=0;wlr_output_layout_output_coords(s->output_layout,o,&ox,&oy);float lw=(float)o->width/o->scale,lh=(float)o->height/o->scale,tw=(float)sf->current.width,th=(float)sf->current.height;float cx=((float)(t->scene_tree->node.x+ox)+tw*.5f-lw*.5f)/lh;float cy=(lh*.5f-((float)(t->scene_tree->node.y+oy)+th*.5f))/lh;s->experimental.fps.grab_offset_x=hx-cx;s->experimental.fps.grab_offset_y=hy-cy;s->experimental.fps.grab_offset_z=hz-t->experimental.z;
float model[16],inv[16];shady_window_model(model,(float)(t->scene_tree->node.x+ox),(float)(t->scene_tree->node.y+oy),tw,th,lw,lh,t->experimental.z,t->experimental.motion.tilt_x,t->experimental.motion.tilt_y);if(shady_mat4_invert(inv,model)){s->experimental.fps.grab_local_x=inv[0]*hx+inv[4]*hy+inv[8]*hz+inv[12]-.5f;s->experimental.fps.grab_local_y=inv[1]*hx+inv[5]*hy+inv[9]*hz+inv[13]-.5f;s->experimental.fps.grab_local_z=inv[2]*hx+inv[6]*hy+inv[10]*hz+inv[14];}else{s->experimental.fps.grab_local_x=s->experimental.fps.grab_local_y=s->experimental.fps.grab_local_z=0;}}else{s->experimental.fps.grab_offset_x=s->experimental.fps.grab_offset_y=s->experimental.fps.grab_offset_z=0;s->experimental.fps.grab_local_x=s->experimental.fps.grab_local_y=s->experimental.fps.grab_local_z=0;}s->experimental.fps.held_toplevel=t;s->experimental.fps.hold_distance=d;if(s->experimental.fps.hold_distance<HOLD_MIN)s->experimental.fps.hold_distance=HOLD_MIN;focus_toplevel(t);}}shady_render_schedule_all_outputs(s);}return true;}
bool shady_fps_handle_axis(struct shady_server*s,struct wlr_pointer_axis_event*e){if(!s->experimental.camera.first_person||!s->experimental.fps.input_capture||!s->experimental.fps.held_toplevel||e->orientation!=WL_POINTER_AXIS_VERTICAL_SCROLL)return false;s->experimental.fps.hold_distance+=(float)e->delta*.0025f;if(s->experimental.fps.hold_distance<HOLD_MIN)s->experimental.fps.hold_distance=HOLD_MIN;if(s->experimental.fps.hold_distance>HOLD_MAX)s->experimental.fps.hold_distance=HOLD_MAX;shady_render_schedule_all_outputs(s);return true;}
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
	struct shady_camera*c=&s->experimental.camera;if(!c->first_person)return;
	const struct shady_world *world = &s->experimental.world;
	float sy=sinf(c->yaw),cy=cosf(c->yaw),fx=-sy,fz=-cy,rx=cy,rz=-sy,mx=0,mz=0;
	if(s->experimental.fps.forward){mx+=fx;mz+=fz;}if(s->experimental.fps.back){mx-=fx;mz-=fz;}if(s->experimental.fps.right){mx+=rx;mz+=rz;}if(s->experimental.fps.left){mx-=rx;mz-=rz;}
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
	if(s->experimental.fps.jump_queued&&c->grounded){c->vel_y=JUMP_SPEED;c->grounded=false;}s->experimental.fps.jump_queued=false;
	float previous_feet=c->pos_y-EYE_HEIGHT;c->vel_y-=GRAVITY*dt;c->pos_y+=c->vel_y*dt;float next_feet=c->pos_y-EYE_HEIGHT;
	struct shady_box_collider feet={c->pos_x-PLAYER_RADIUS,c->pos_x+PLAYER_RADIUS,next_feet,next_feet,c->pos_z-PLAYER_RADIUS,c->pos_z+PLAYER_RADIUS};
	bool landed=false;float support_y=0.f;
	for(size_t i=0;i<world->collider_count;i++){const struct shady_box_collider*b=&world->colliders[i];float y=b->max_y;
		if(c->vel_y<=0.f&&previous_feet>=y&&next_feet<=y&&shady_box_overlap_xz(b,&feet)&&(!landed||y>support_y)){support_y=y;landed=true;}}
	if(landed){c->pos_y=support_y+EYE_HEIGHT;c->vel_y=0.f;c->grounded=true;}else c->grounded=false;
}
void shady_fps_update_held_window(struct shady_server*s,float lw,float lh){
	struct shady_toplevel*t=s->experimental.fps.held_toplevel;if(!s->experimental.camera.first_person||!t)return;
	struct wlr_surface*surface=t->xdg_toplevel->base->surface;
	if(!surface->mapped||lh<=0){s->experimental.fps.held_toplevel=NULL;return;}
	struct shady_vec3 eye,f;shady_camera_eye(&s->experimental.camera,&eye);shady_camera_basis(&s->experimental.camera,NULL,NULL,&f);
	float d=s->experimental.fps.hold_distance,tw=(float)surface->current.width,th=(float)surface->current.height;
	float target[3]={eye.x+f.x*d,eye.y+f.y*d,eye.z+f.z*d};
	float current[3]={
		((float)t->scene_tree->node.x+tw*.5f-lw*.5f)/lh,
		.5f-((float)t->scene_tree->node.y+th*.5f)/lh,
		t->experimental.z
	};
	/* A held folded window is still the same authoritative cube. Camera
	 * rotation requests a target position; world collision clips that motion. */
	shady_physics_move_cube(&s->experimental.world,current,target,SHADY_FPS_CUBE_SIZE*.5f);
	int x=(int)(current[0]*lh+lw*.5f-tw*.5f);
	int y=(int)((.5f-current[1])*lh-th*.5f);
	wlr_scene_node_set_position(&t->scene_tree->node,x,y);t->experimental.z=current[2];
	/* Keep the visual cube axis-aligned while held so its rendered body and
	 * collision/debug box cannot diverge as the camera rotates. */
	t->experimental.motion.tilt_x=t->experimental.motion.tilt_y=0.f;t->experimental.motion.tilt_vx=t->experimental.motion.tilt_vy=0.f;
}
void shady_fps_toplevel_gone(struct shady_server*s,struct shady_toplevel*t){
	if(s->experimental.fps.held_toplevel==t)s->experimental.fps.held_toplevel=NULL;
	if(s->experimental.fps.expanded_toplevel==t)s->experimental.fps.expanded_toplevel=NULL;
}

bool shady_fps_is_holding(const struct shady_server*s,const struct shady_toplevel*t){return s->experimental.fps.held_toplevel==t;}

bool shady_fps_is_expanded(const struct shady_server*s,const struct shady_toplevel*t){return s->experimental.camera.first_person&&t&&t->experimental.fps_expanded;}
