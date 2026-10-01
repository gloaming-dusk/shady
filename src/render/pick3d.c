#include "pick3d.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "../shady.h"
#include "../modules/spatial/state.h"
#include "math3d.h"
#include "render.h"
#include "../modules/fps/fps.h"
#include "../modules/fps/state.h"
#include "../modules/window_motion/state.h"

struct shady_toplevel *shady_toplevel_at_3d(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface, double *sx, double *sy) {
	*surface = NULL;
	*sx = 0;
	*sy = 0;

	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (!wlr_output) {
		return NULL;
	}

	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(server->output_layout, wlr_output, &ox, &oy);
	double local_x = lx + ox; /* ox is typically -layout_x */
	double local_y = ly + oy;

	int buf_w = wlr_output->width;
	int buf_h = wlr_output->height;
	float scale = wlr_output->scale;
	float logical_w = (float)buf_w / scale;
	float logical_h = (float)buf_h / scale;

	if (logical_w <= 0.f || logical_h <= 0.f) {
		return NULL;
	}

	/* Output-local → GL NDC (+Y up). Vertex shader flips clip Y for the
	 * wlroots FBO; unprojection still uses the unflipped view·proj. */
	float ndc_x = (float)(local_x / logical_w) * 2.f - 1.f;
	float ndc_y = 1.f - (float)(local_y / logical_h) * 2.f;

	float view[16], proj[16];
	shady_render_camera_matrices(server, buf_w, buf_h, view, proj);

	struct shady_ray ray;
	shady_ray_from_ndc(&ray, ndc_x, ndc_y, view, proj);

	struct shady_toplevel *best = NULL;
	float best_t = 1e30f;
	float best_u = 0.f, best_v = 0.f;
	int best_sw = 0, best_sh = 0;

	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
		if (!surf->mapped) {
			continue;
		}

		float tw = (float)surf->current.width;
		float th = (float)surf->current.height;
		if (tw <= 0.f || th <= 0.f) {
			continue;
		}

		float layout_x = (float)(toplevel->scene_tree->node.x + ox);
		float layout_y = (float)(toplevel->scene_tree->node.y + oy);

		float model[16];
		if(shady_spatial_state(server)->runtime.camera.first_person&&!shady_fps_toplevel_state_const(toplevel)->expanded){
			float cx=(layout_x+tw*.5f-logical_w*.5f)/logical_h,cy=.5f-(layout_y+th*.5f)/logical_h;
			shady_window_cube_model(model,cx,cy,shady_spatial_toplevel_state(toplevel)->z,SHADY_FPS_CUBE_SIZE,shady_window_motion_state_for_const(toplevel)->tilt_x,shady_window_motion_state_for_const(toplevel)->tilt_y);
		}else shady_window_model(model,layout_x,layout_y,tw,th,logical_w,logical_h,shady_spatial_toplevel_state(toplevel)->z,shady_window_motion_state_for_const(toplevel)->tilt_x,shady_window_motion_state_for_const(toplevel)->tilt_y);

		float t, u, v;
		bool front_hit = false;
		if (!shady_ray_window_shell_hit(&ray, model, shady_window_motion_state_for_const(toplevel)->wobble_x,
				shady_window_motion_state_for_const(toplevel)->wobble_y, &t, &u, &v, &front_hit)) {
			continue;
		}
		if (t < best_t) {
			best_t = t;
			best_u = u;
			best_v = v;
			best = toplevel;
			best_sw = surf->current.width;
			best_sh = surf->current.height;
		}
	}

	if (!best) {
		return NULL;
	}

	double surf_x = (double)best_u * (double)best_sw;
	/* Model local v=0 is bottom; surface y=0 is top. */
	double surf_y = (1.0 - (double)best_v) * (double)best_sh;
	struct wlr_surface *root = best->xdg_toplevel->base->surface;
	struct wlr_surface *leaf = wlr_surface_surface_at(root, surf_x, surf_y, sx, sy);
	if (leaf) {
		*surface = leaf;
	} else {
		*surface = root;
		*sx = surf_x;
		*sy = surf_y;
	}
	return best;
}


struct shady_toplevel *shady_toplevel_at_camera_center_hit(
		struct shady_server *server, float *distance_out,
		float *hit_x, float *hit_y, float *hit_z) {
	if (distance_out) *distance_out = 0.f;
	struct shady_vec3 eye, forward;
	shady_camera_eye(&shady_spatial_state(server)->runtime.camera, &eye);
	shady_camera_basis(&shady_spatial_state(server)->runtime.camera, NULL, NULL, &forward);
	struct shady_ray ray = { .origin = eye, .dir = forward };

	struct wlr_output *wlr_output = NULL;
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		wlr_output = output->wlr_output;
		break;
	}
	if (!wlr_output) return NULL;

	float scale = wlr_output->scale;
	float logical_w = (float)wlr_output->width / scale;
	float logical_h = (float)wlr_output->height / scale;
	if (logical_w <= 0.f || logical_h <= 0.f) return NULL;

	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(server->output_layout, wlr_output, &ox, &oy);

	struct shady_toplevel *best = NULL;
	float best_t = 1e30f;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
		if (!surf->mapped) continue;
		float tw = (float)surf->current.width;
		float th = (float)surf->current.height;
		if (tw <= 0.f || th <= 0.f) continue;
		float model[16];
		if(shady_spatial_state(server)->runtime.camera.first_person&&!shady_fps_toplevel_state_const(toplevel)->expanded){
			float lx=(float)(toplevel->scene_tree->node.x+ox),ly=(float)(toplevel->scene_tree->node.y+oy);
			float cx=(lx+tw*.5f-logical_w*.5f)/logical_h,cy=.5f-(ly+th*.5f)/logical_h;
			shady_window_cube_model(model,cx,cy,shady_spatial_toplevel_state(toplevel)->z,SHADY_FPS_CUBE_SIZE,shady_window_motion_state_for_const(toplevel)->tilt_x,shady_window_motion_state_for_const(toplevel)->tilt_y);
		}else shady_window_model(model,(float)(toplevel->scene_tree->node.x+ox),(float)(toplevel->scene_tree->node.y+oy),tw,th,logical_w,logical_h,shady_spatial_toplevel_state(toplevel)->z,shady_window_motion_state_for_const(toplevel)->tilt_x,shady_window_motion_state_for_const(toplevel)->tilt_y);
		float t, u, v;
		bool front_hit = false;
		if (shady_ray_window_shell_hit(&ray, model, shady_window_motion_state_for_const(toplevel)->wobble_x,
				shady_window_motion_state_for_const(toplevel)->wobble_y, &t, &u, &v, &front_hit) && t < best_t) {
			best_t = t;
			best = toplevel;
		}
	}
	if (best) {
		if (distance_out) *distance_out = best_t;
		if (hit_x) *hit_x = eye.x + forward.x * best_t;
		if (hit_y) *hit_y = eye.y + forward.y * best_t;
		if (hit_z) *hit_z = eye.z + forward.z * best_t;
	}
	return best;
}

struct shady_toplevel *shady_toplevel_at_camera_center(
		struct shady_server *server, float *distance_out) {
	return shady_toplevel_at_camera_center_hit(server, distance_out, NULL, NULL, NULL);
}
