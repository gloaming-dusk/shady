#include "scene_effects.h"
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "../../render/gl_pipeline.h"
#include "../../render/math3d.h"
#include "../../world/floor.h"

void shady_scene_effects_draw_floor(struct shady_server *server,
		struct shady_gl_pipeline *pipeline,const float vp[16]) {
	if (server->config.floor) { struct shady_floor floor=shady_world_floor(); shady_gl_pipeline_draw_floor(pipeline,vp,&floor); }
}

void shady_scene_effects_draw_shadows(struct shady_server *server,
		struct shady_gl_pipeline *pipeline,const float vp[16],
		float logical_w,float logical_h,double ox,double oy) {
	if (!server->config.shadows) return;
	struct shady_floor floor=shady_world_floor();
	struct shady_toplevel *t;
	wl_list_for_each_reverse(t,&server->toplevels,link) {
		struct wlr_surface *surface=t->xdg_toplevel->base->surface;
		if(!surface->mapped || t->experimental.close.progress>=.02f) continue;
		float tw=(float)surface->current.width,th=(float)surface->current.height;
		if(tw<=0.f||th<=0.f)continue;
		float model[16];
		shady_window_model(model,(float)(t->scene_tree->node.x+ox),
			(float)(t->scene_tree->node.y+oy),tw,th,logical_w,logical_h,
			t->experimental.z,t->experimental.motion.tilt_x,t->experimental.motion.tilt_y);
		shady_gl_pipeline_draw_shadow(pipeline,vp,model,t->experimental.motion.wobble_x,t->experimental.motion.wobble_y,t->experimental.z,&floor);
	}
}

void shady_scene_effects_draw_sides(struct shady_server *server,
		struct shady_gl_pipeline *pipeline,const float mvp[16],
		const float model[16],float wobble_x,float wobble_y,float close_progress) {
	if (server->config.window_sides && close_progress<.02f)
		shady_gl_pipeline_draw_sides(pipeline,mvp,model,wobble_x,wobble_y);
}
