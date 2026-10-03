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

struct shady_toplevel *shady_titlebar_at_3d(struct shady_server *server,
		double lx, double ly) {
	struct wlr_output *wlr_output =
		wlr_output_layout_output_at(server->output_layout, lx, ly);
	if (!wlr_output || wlr_output->scale <= 0.f) return NULL;

	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(server->output_layout, wlr_output, &ox, &oy);
	double local_x = lx + ox;
	double local_y = ly + oy;
	float logical_w = (float)wlr_output->width / wlr_output->scale;
	float logical_h = (float)wlr_output->height / wlr_output->scale;
	if (logical_w <= 0.f || logical_h <= 0.f) return NULL;

	float ndc_x = (float)(local_x / logical_w) * 2.f - 1.f;
	float ndc_y = 1.f - (float)(local_y / logical_h) * 2.f;
	float view[16], proj[16];
	shady_render_camera_matrices(server, wlr_output->width, wlr_output->height,
		view, proj);
	struct shady_ray ray;
	shady_ray_from_ndc(&ray, ndc_x, ndc_y, view, proj);

	struct shady_toplevel *best = NULL;
	float best_t = 1e30f;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
		if (!surf || !surf->mapped || !toplevel->scene_tree->node.enabled ||
				toplevel->fullscreen ||
				!server->config.window_titlebar || toplevel->titlebar_height <= 0)
			continue;
		if (shady_spatial_state(server)->runtime.camera.first_person &&
				!shady_fps_toplevel_state_const(toplevel)->expanded)
			continue;

		float tw = (float)surf->current.width;
		float th = (float)surf->current.height;
		if (tw <= 0.f || th <= 0.f) continue;
		float layout_x = (float)(toplevel->scene_tree->node.x + ox);
		float layout_y = (float)(toplevel->scene_tree->node.y + oy);
		float title_h = (float)toplevel->titlebar_height;

		if (toplevel->maximized &&
				!shady_spatial_state(server)->runtime.camera.first_person) {
			double global_title_x = (double)toplevel->scene_tree->node.x;
			double global_title_y = (double)toplevel->scene_tree->node.y - title_h;
			if (lx >= global_title_x && lx < global_title_x + tw &&
					ly >= global_title_y && ly < global_title_y + title_h)
				return toplevel;
			continue;
		}

		float frame_h = th + title_h;
		float client_fraction = frame_h > 0.f ? th / frame_h : 1.f;
		float model[16];
		shady_window_model(model, layout_x, layout_y - title_h, tw, frame_h,
			logical_w, logical_h, shady_spatial_toplevel_state(toplevel)->z,
			shady_window_motion_state_for_const(toplevel)->tilt_x,
			shady_window_motion_state_for_const(toplevel)->tilt_y);

		float t, u, v;
		bool front_hit = false;
		if (!shady_ray_window_shell_hit(&ray, model,
				shady_window_motion_state_for_const(toplevel)->wobble_x,
				shady_window_motion_state_for_const(toplevel)->wobble_y,
				&t, &u, &v, &front_hit) || !front_hit ||
				v < client_fraction)
			continue;
		if (t < best_t) {
			best_t = t;
			best = toplevel;
		}
	}
	return best;
}

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
	/* Outside FPS mode, fullscreen/maximized windows live in output-local 2D
	 * screen space. In FPS mode they become spatial cubes like every other
	 * toplevel, so skip this direct 2D picking path. */
	struct shady_toplevel *screen_toplevel;
	wl_list_for_each(screen_toplevel, &server->toplevels, link) {
		if (shady_spatial_state(server)->runtime.camera.first_person ||
				(!screen_toplevel->fullscreen && !screen_toplevel->maximized) || !screen_toplevel->scene_tree ||
				!screen_toplevel->scene_tree->node.enabled) continue;
		struct wlr_surface *root = screen_toplevel->xdg_toplevel->base->surface;
		if (!root || !root->mapped) continue;
		double root_x = lx - screen_toplevel->scene_tree->node.x;
		double root_y = ly - screen_toplevel->scene_tree->node.y;
		if (root_x < 0 || root_y < 0 || root_x >= root->current.width ||
				root_y >= root->current.height) continue;
		struct wlr_surface *leaf = wlr_surface_surface_at(root, root_x, root_y, sx, sy);
		if (leaf) *surface = leaf;
		else { *surface = root; *sx = root_x; *sy = root_y; }
		return screen_toplevel;
	}

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
		if (!surf->mapped || !toplevel->scene_tree->node.enabled) {
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
		float client_fraction = 1.f;
		float cx=(layout_x+tw*.5f-logical_w*.5f)/logical_h;
		float cy=.5f-(layout_y+th*.5f)/logical_h;
		struct shady_representation_context representation_context = {
			.struct_size = sizeof(representation_context),
			.logical_width = logical_w, .logical_height = logical_h,
			.window_width = tw, .window_height = th,
			.center_x = cx, .center_y = cy,
			.center_z = shady_spatial_toplevel_state(toplevel)->z,
			.tilt_x = shady_window_motion_state_for_const(toplevel)->tilt_x,
			.tilt_y = shady_window_motion_state_for_const(toplevel)->tilt_y,
			.first_person = shady_spatial_state(server)->runtime.camera.first_person,
			.folded = shady_spatial_state(server)->runtime.camera.first_person &&
				!shady_fps_toplevel_state_const(toplevel)->expanded,
			.held = shady_fps_is_holding(server, toplevel),
			.focused = !wl_list_empty(&server->toplevels) &&
				server->toplevels.next == &toplevel->link,
		};
		struct shady_window_representation representation_base = {0};
		bool has_representation_base = shady_toplevel_representation_base(
			toplevel, &representation_base);
		struct shady_representation_model representation_model;
		bool folded_representation = representation_context.folded &&
			has_representation_base && shady_toplevel_representation_model(toplevel,
				&representation_context, &representation_model);
		struct shady_representation_mesh representation_mesh = {0};
		bool mesh_representation = folded_representation &&
			representation_base.kind == SHADY_WINDOW_REPRESENTATION_MESH &&
			shady_toplevel_representation_mesh(toplevel,
				&representation_context, &representation_mesh);
		if (folded_representation) {
			shady_window_box_model(model,
				representation_model.center_x, representation_model.center_y,
				representation_model.center_z,
				representation_model.width, representation_model.height,
				representation_model.depth,
				representation_model.tilt_x, representation_model.tilt_y);
		}else{
			float title_h = (!toplevel->fullscreen && server->config.window_titlebar &&
				toplevel->titlebar_height > 0) ? (float)toplevel->titlebar_height : 0.f;
			float frame_h = th + title_h;
			client_fraction = frame_h > 0.f ? th / frame_h : 1.f;
			shady_window_model(model,layout_x,layout_y-title_h,tw,frame_h,logical_w,logical_h,
				shady_spatial_toplevel_state(toplevel)->z,
				shady_window_motion_state_for_const(toplevel)->tilt_x,
				shady_window_motion_state_for_const(toplevel)->tilt_y);
		}

		float t, u, v;
		bool front_hit = true;
		bool hit = mesh_representation
			? shady_ray_mesh_hit(&ray, model,
				(const float *)representation_mesh.vertices,
				representation_mesh.vertex_count,
				representation_mesh.indices,
				representation_mesh.index_count, &t, &u, &v)
			: shady_ray_window_shell_hit(&ray, model,
				shady_window_motion_state_for_const(toplevel)->wobble_x,
				shady_window_motion_state_for_const(toplevel)->wobble_y,
				&t, &u, &v, &front_hit);
		if (!hit) continue;
		if (client_fraction < 1.f) {
			if (v > client_fraction) continue;
			v /= client_fraction;
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


struct shady_toplevel *shady_toplevel_at_camera_center_hit_output(
		struct shady_server *server, float *distance_out,
		float *hit_x, float *hit_y, float *hit_z,
		struct wlr_output **output_out) {
	if (distance_out) *distance_out = 0.f;
	if (output_out) *output_out = NULL;

	struct shady_vec3 eye, forward;
	shady_camera_eye(&shady_spatial_state(server)->runtime.camera, &eye);
	shady_camera_basis(&shady_spatial_state(server)->runtime.camera, NULL, NULL, &forward);
	struct shady_ray ray = { .origin = eye, .dir = forward };

	struct shady_toplevel *best = NULL;
	struct wlr_output *best_output = NULL;
	float best_t = 1e30f;

	/* Each output currently renders the shared scene through its own local
	 * viewport transform. Mirror that projection here instead of assuming the
	 * first output in the list is authoritative. */
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output *wlr_output = output->wlr_output;
		if (!wlr_output || !wlr_output->enabled || wlr_output->scale <= 0.f)
			continue;

		float logical_w = (float)wlr_output->width / wlr_output->scale;
		float logical_h = (float)wlr_output->height / wlr_output->scale;
		if (logical_w <= 0.f || logical_h <= 0.f) continue;

		double ox = 0, oy = 0;
		wlr_output_layout_output_coords(server->output_layout, wlr_output, &ox, &oy);

		struct shady_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->toplevels, link) {
			struct wlr_surface *surf = toplevel->xdg_toplevel->base->surface;
			if (!surf->mapped || !toplevel->scene_tree->node.enabled) continue;
			float tw = (float)surf->current.width;
			float th = (float)surf->current.height;
			if (tw <= 0.f || th <= 0.f) continue;

			float model[16];
			float lx = (float)(toplevel->scene_tree->node.x + ox);
			float ly = (float)(toplevel->scene_tree->node.y + oy);
			float cx = (lx + tw * .5f - logical_w * .5f) / logical_h;
			float cy = .5f - (ly + th * .5f) / logical_h;
			struct shady_representation_context representation_context = {
				.struct_size = sizeof(representation_context),
				.logical_width = logical_w, .logical_height = logical_h,
				.window_width = tw, .window_height = th,
				.center_x = cx, .center_y = cy,
				.center_z = shady_spatial_toplevel_state(toplevel)->z,
				.tilt_x = shady_window_motion_state_for_const(toplevel)->tilt_x,
				.tilt_y = shady_window_motion_state_for_const(toplevel)->tilt_y,
				.first_person = shady_spatial_state(server)->runtime.camera.first_person,
				.folded = shady_spatial_state(server)->runtime.camera.first_person &&
					!shady_fps_toplevel_state_const(toplevel)->expanded,
				.held = shady_fps_is_holding(server, toplevel),
				.focused = !wl_list_empty(&server->toplevels) &&
					server->toplevels.next == &toplevel->link,
			};
			struct shady_window_representation representation_base = {0};
			bool has_representation_base = shady_toplevel_representation_base(
				toplevel, &representation_base);
			struct shady_representation_model representation_model;
			bool folded_representation = representation_context.folded &&
				has_representation_base && shady_toplevel_representation_model(toplevel,
					&representation_context, &representation_model);
			struct shady_representation_mesh representation_mesh = {0};
			bool mesh_representation = folded_representation &&
				representation_base.kind == SHADY_WINDOW_REPRESENTATION_MESH &&
				shady_toplevel_representation_mesh(toplevel,
					&representation_context, &representation_mesh);
			if (folded_representation) {
				shady_window_box_model(model,
					representation_model.center_x, representation_model.center_y,
					representation_model.center_z,
					representation_model.width, representation_model.height,
					representation_model.depth,
					representation_model.tilt_x, representation_model.tilt_y);
			} else {
				shady_window_model(model,
					(float)(toplevel->scene_tree->node.x + ox),
					(float)(toplevel->scene_tree->node.y + oy),
					tw, th, logical_w, logical_h,
					shady_spatial_toplevel_state(toplevel)->z,
					shady_window_motion_state_for_const(toplevel)->tilt_x,
					shady_window_motion_state_for_const(toplevel)->tilt_y);
			}

			float t, u, v;
			bool front_hit = true;
			bool hit = mesh_representation
				? shady_ray_mesh_hit(&ray, model,
					(const float *)representation_mesh.vertices,
					representation_mesh.vertex_count,
					representation_mesh.indices,
					representation_mesh.index_count, &t, &u, &v)
				: shady_ray_window_shell_hit(&ray, model,
					shady_window_motion_state_for_const(toplevel)->wobble_x,
					shady_window_motion_state_for_const(toplevel)->wobble_y,
					&t, &u, &v, &front_hit);
			if (hit && t < best_t) {
				best_t = t;
				best = toplevel;
				best_output = wlr_output;
			}
		}
	}

	if (best) {
		if (distance_out) *distance_out = best_t;
		if (hit_x) *hit_x = eye.x + forward.x * best_t;
		if (hit_y) *hit_y = eye.y + forward.y * best_t;
		if (hit_z) *hit_z = eye.z + forward.z * best_t;
		if (output_out) *output_out = best_output;
	}
	return best;
}

struct shady_toplevel *shady_toplevel_at_camera_center_hit(
		struct shady_server *server, float *distance_out,
		float *hit_x, float *hit_y, float *hit_z) {
	return shady_toplevel_at_camera_center_hit_output(server, distance_out,
		hit_x, hit_y, hit_z, NULL);
}

struct shady_toplevel *shady_toplevel_at_camera_center(
		struct shady_server *server, float *distance_out) {
	return shady_toplevel_at_camera_center_hit(server, distance_out, NULL, NULL, NULL);
}
