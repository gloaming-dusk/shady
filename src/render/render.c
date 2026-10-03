#include "render.h"

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <wayland-server-core.h>

#include <wlr/render/gles2.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_texture.h>

#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>

#include <wlr/util/log.h>

#include <GLES2/gl2.h>

#include "../shady.h"
#include "gl_pipeline.h"
#include "math3d.h"
#include "pick3d.h"
#include "../experimental/runtime.h"
#include "../modules/desktop/state.h"
#include "../modules/spatial/state.h"
#include "../modules/fps/fps.h"
#include "../modules/fps/state.h"
#include "../modules/physics/state.h"
#include "../modules/window_motion/state.h"
#include "../modules/close_animation/state.h"
#include "../modules/close_animation/close_animation.h"
#include "../modules/scene_effects/scene_effects.h"
#include "../modules/environment/environment.h"

static struct shady_gl_pipeline pipeline;
static bool pipeline_ready;

static GLuint depth_rbo;
static int depth_rbo_w;
static int depth_rbo_h;
static int render_stats_enabled_cache = -1;

static bool render_stats_enabled(void) {
	if (render_stats_enabled_cache < 0) {
		const char *value = getenv("SHADY_RENDER_STATS");
		render_stats_enabled_cache =
			(value && *value && strcmp(value, "0") != 0) ? 1 : 0;
	}
	return render_stats_enabled_cache != 0;
}

static uint64_t timespec_delta_ns(const struct timespec *start,
		const struct timespec *end) {
	int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
	int64_t nanoseconds = (int64_t)end->tv_nsec - (int64_t)start->tv_nsec;
	return (uint64_t)(seconds * 1000000000ll + nanoseconds);
}

/*
 * Monotonic starting point for shader animation.
 */
static struct timespec shader_start_time;
static struct timespec close_snapshot_last_time;

struct shady_close_snapshot {
	struct wl_list link;

	struct shady_toplevel *toplevel;
	struct shady_server *server;

	GLuint texture;

	int texture_width;
	int texture_height;

	float x;
	float y;
	float width;
	float height;
	float tilt_x;
	float tilt_y;
	float z;
	float wobble_x;
	float wobble_y;

	bool has_alpha;

	bool dirty;
	bool animating;

	float progress;
};

static struct shady_close_snapshot *
find_close_snapshot(
	struct shady_toplevel *toplevel
);

static struct shady_close_snapshot *
ensure_close_snapshot(
	struct shady_toplevel *toplevel
);

static struct wl_list close_snapshots;

/*
 * Total close animation duration.
 *
 * 0.42 seconds feels quick enough for a window manager while still
 * making the effect clearly visible.
 */
#define FPS_FLOOR_Y -0.62f
#define FPS_EYE_HEIGHT 0.40f
#define FPS_MOVE_SPEED 1.25f
#define FPS_GRAVITY 3.8f
#define FPS_JUMP_SPEED 1.45f

static float shader_time_seconds(void) {
	struct timespec now;

	clock_gettime(
		CLOCK_MONOTONIC,
		&now
	);

	double seconds =
		(double)(
			now.tv_sec -
			shader_start_time.tv_sec
		);

	double nanoseconds =
		(double)(
			now.tv_nsec -
			shader_start_time.tv_nsec
		) / 1000000000.0;

	return (float)(
		seconds + nanoseconds
	);
}

static void update_close_snapshots(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float dt =
		(float)(now.tv_sec - close_snapshot_last_time.tv_sec) +
		(float)(now.tv_nsec - close_snapshot_last_time.tv_nsec) /
			1000000000.0f;
	close_snapshot_last_time = now;

	if (dt <= 0.0f) {
		return;
	}
	if (dt > 0.033f) {
		dt = 0.033f;
	}
	shady_close_animation_update_snapshots(&close_snapshots, dt);
}

static struct shady_close_snapshot *
find_close_snapshot(
	struct shady_toplevel *toplevel
) {
	struct shady_close_snapshot *snapshot;

	wl_list_for_each(
		snapshot,
		&close_snapshots,
		link
	) {
		if (
			snapshot->toplevel ==
			toplevel
		) {
			return snapshot;
		}
	}

	return NULL;
}

static struct shady_close_snapshot *
ensure_close_snapshot(
	struct shady_toplevel *toplevel
) {
	struct shady_close_snapshot *snapshot =
		find_close_snapshot(
			toplevel
		);

	if (snapshot) {
		return snapshot;
	}

	snapshot =
		calloc(
			1,
			sizeof(*snapshot)
		);

	if (!snapshot) {
		return NULL;
	}

	snapshot->toplevel =
		toplevel;

	snapshot->server =
		toplevel->server;

	snapshot->dirty =
		true;

	wl_list_insert(
		&close_snapshots,
		&snapshot->link
	);

	return snapshot;
}

bool shady_render_init(
	struct wlr_renderer *renderer
) {
	pipeline_ready =
		shady_gl_pipeline_init(
			&pipeline,
			renderer
		);
	if (pipeline_ready) shady_environment_init(&pipeline);

	depth_rbo = 0;
	depth_rbo_w = 0;
	depth_rbo_h = 0;

	clock_gettime(
		CLOCK_MONOTONIC,
		&shader_start_time
	);

	close_snapshot_last_time = shader_start_time;

	wl_list_init(
		&close_snapshots
	);

	return pipeline_ready;
}

void shady_render_fini(void) {
	if (depth_rbo) {
		glDeleteRenderbuffers(
			1,
			&depth_rbo
		);

		depth_rbo = 0;
	}

	if (pipeline_ready) {
		shady_environment_fini();
		shady_gl_pipeline_fini(
			&pipeline
		);

		pipeline_ready = false;
	}
}

void shady_render_camera_matrices(
	struct shady_server *server,
	int buf_w,
	int buf_h,
	float view[16],
	float proj[16]
) {
	shady_camera_view(
		&shady_spatial_state(server)->runtime.camera,
		view
	);

	float aspect =
		(buf_h > 0)
			? ((float)buf_w / (float)buf_h)
			: 1.f;

	shady_mat4_perspective(
		proj,
		SHADY_CAMERA_FOV_Y,
		aspect,
		SHADY_CAMERA_NEAR,
		SHADY_CAMERA_FAR
	);
}

static bool close_snapshot_needs_frame(void) {
	struct shady_close_snapshot *snapshot;
	wl_list_for_each(snapshot, &close_snapshots, link) {
		if (snapshot->animating) return true;
	}
	return false;
}

enum shady_continuous_reason {
	SHADY_CONTINUOUS_NONE = 0,
	SHADY_CONTINUOUS_CAMERA,
	SHADY_CONTINUOUS_MOTION,
	SHADY_CONTINUOUS_EFFECT,
	SHADY_CONTINUOUS_PHYSICS,
	SHADY_CONTINUOUS_CLOSE,
	SHADY_CONTINUOUS_SNAPSHOT,
};

static enum shady_continuous_reason spatial_continuous_reason(
		struct shady_server *server) {
	const struct shady_spatial_state *spatial = shady_spatial_state_const(server);
	if (!spatial) return SHADY_CONTINUOUS_NONE;

	const struct shady_fps_state *fps = shady_fps_state_for_const(server);
	if (spatial->runtime.camera.first_person) {
		const struct shady_camera *camera = &spatial->runtime.camera;
		if (fps->forward || fps->back || fps->left || fps->right ||
				fps->jump_queued || fabsf(camera->vel_y) > 0.00005f ||
				!camera->grounded) {
			return SHADY_CONTINUOUS_CAMERA;
		}
	}

	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (!toplevel->scene_tree || !toplevel->scene_tree->node.enabled ||
				!toplevel->xdg_toplevel->base->surface->mapped) continue;

		if (toplevel->water_amplitude > 0.00005f)
			return SHADY_CONTINUOUS_EFFECT;

		const struct shady_window_motion_state *motion =
			shady_window_motion_state_for_const(toplevel);
		if (fabsf(motion->wobble_x) > 0.00005f ||
				fabsf(motion->wobble_y) > 0.00005f ||
				fabsf(motion->wobble_vx) > 0.00005f ||
				fabsf(motion->wobble_vy) > 0.00005f ||
				fabsf(motion->tilt_vx) > 0.00005f ||
				fabsf(motion->tilt_vy) > 0.00005f) {
			return SHADY_CONTINUOUS_MOTION;
		}

		const struct shady_window_physics_state *physics =
			shady_physics_toplevel_state_const(toplevel);
		if (physics && (fabsf(physics->vx) > 0.00005f ||
				fabsf(physics->vy) > 0.00005f ||
				fabsf(physics->vz) > 0.00005f)) {
			return SHADY_CONTINUOUS_PHYSICS;
		}

		enum shady_close_state close_state =
			shady_close_animation_state(toplevel);
		if (close_state == SHADY_CLOSE_CRUMPLING ||
				close_state == SHADY_CLOSE_WAITING ||
				close_state == SHADY_CLOSE_RESTORING) {
			return SHADY_CONTINUOUS_CLOSE;
		}
	}

	return close_snapshot_needs_frame() ?
		SHADY_CONTINUOUS_SNAPSHOT : SHADY_CONTINUOUS_NONE;
}

void shady_render_schedule_output(struct shady_output *output) {
	if (!output || !output->wlr_output) return;
	output->frame_schedule_requests++;
	if (!output->wlr_output->enabled) {
		output->frame_scheduled = false;
		return;
	}
	if (output->frame_scheduled) {
		output->frame_schedule_coalesced++;
		return;
	}
	output->frame_scheduled = true;
	wlr_output_schedule_frame(output->wlr_output);
}

void shady_render_schedule_all_outputs(
	struct shady_server *server
) {
	struct shady_output *output;

	wl_list_for_each(
		output,
		&server->outputs,
		link
	) {
		shady_render_schedule_output(output);
	}
}

static void ensure_depth_rbo(
	int w,
	int h
) {
	if (
		depth_rbo &&
		depth_rbo_w == w &&
		depth_rbo_h == h
	) {
		return;
	}

	if (depth_rbo) {
		glDeleteRenderbuffers(
			1,
			&depth_rbo
		);

		depth_rbo = 0;
	}

	glGenRenderbuffers(
		1,
		&depth_rbo
	);

	glBindRenderbuffer(
		GL_RENDERBUFFER,
		depth_rbo
	);

	glRenderbufferStorage(
		GL_RENDERBUFFER,
		GL_DEPTH_COMPONENT16,
		w,
		h
	);

	glBindRenderbuffer(
		GL_RENDERBUFFER,
		0
	);

	depth_rbo_w = w;
	depth_rbo_h = h;
}

struct shady_overlay_render_data {
	struct wlr_render_pass *pass;
	struct wlr_output *output;
	double ox, oy;
	float scale;
	struct timespec now;
};

static void render_scene_overlay_buffer(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data) {
	struct shady_overlay_render_data *ctx = data;
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface || !scene_surface->surface->mapped) {
		return;
	}
	struct wlr_texture *texture =
		wlr_surface_get_texture(scene_surface->surface);
	if (!texture) {
		return;
	}

	int width = buffer->dst_width > 0
		? buffer->dst_width : scene_surface->surface->current.width;
	int height = buffer->dst_height > 0
		? buffer->dst_height : scene_surface->surface->current.height;
	if (width <= 0 || height <= 0) {
		return;
	}

	float alpha = buffer->opacity;
	struct wlr_render_texture_options options = {
		.texture = texture,
		.src_box = buffer->src_box,
		.dst_box = {
			.x = (int)lround(((double)sx - ctx->ox) * ctx->scale),
			.y = (int)lround(((double)sy - ctx->oy) * ctx->scale),
			.width = (int)lround((double)width * ctx->scale),
			.height = (int)lround((double)height * ctx->scale),
		},
		.alpha = &alpha,
		.transform = buffer->transform,
		.filter_mode = buffer->filter_mode,
	};
	wlr_render_pass_add_texture(ctx->pass, &options);
	wlr_scene_surface_send_frame_done(scene_surface, &ctx->now);
}

struct shady_spatial_surface_render_data {
	struct shady_toplevel *toplevel;
	struct wlr_surface *root_surface;
	const float *vp;
	float logical_w, logical_h;
	double ox, oy;
	float time_seconds;
	bool screen_space;
};

static void shady_screen_space_model(float out[16], float x, float y,
		float width, float height, float output_w, float output_h) {
	shady_mat4_identity(out);
	if (output_w <= 0.f || output_h <= 0.f) return;
	out[0] = 2.f * width / output_w;
	out[5] = 2.f * height / output_h;
	out[12] = 2.f * x / output_w - 1.f;
	out[13] = 2.f * y / output_h - 1.f;
}

static void render_spatial_subsurface_buffer(struct wlr_scene_buffer *buffer,
		int sx, int sy, void *data) {
	struct shady_spatial_surface_render_data *ctx = data;
	struct wlr_scene_surface *scene_surface =
		wlr_scene_surface_try_from_buffer(buffer);
	if (!scene_surface || !scene_surface->surface->mapped ||
			scene_surface->surface == ctx->root_surface ||
			wlr_surface_get_root_surface(scene_surface->surface) != ctx->root_surface) {
		return;
	}
	struct wlr_texture *texture = wlr_surface_get_texture(scene_surface->surface);
	if (!texture || !wlr_texture_is_gles2(texture)) {
		return;
	}
	struct wlr_gles2_texture_attribs attribs;
	wlr_gles2_texture_get_attribs(texture, &attribs);

	float width = (float)scene_surface->surface->current.width;
	float height = (float)scene_surface->surface->current.height;
	if (width <= 0.f || height <= 0.f) {
		return;
	}

	float model[16], mvp[16];
	float wobble_x = shady_window_motion_state_for_const(ctx->toplevel)->wobble_x;
	float wobble_y = shady_window_motion_state_for_const(ctx->toplevel)->wobble_y;
	if (ctx->screen_space) {
		shady_mat4_identity(model);
		shady_screen_space_model(mvp,
			(float)sx + (float)ctx->ox,
			(float)sy + (float)ctx->oy,
			width, height, ctx->logical_w, ctx->logical_h);
		wobble_x = 0.f;
		wobble_y = 0.f;
	} else {
		shady_window_model(model,
			(float)sx + (float)ctx->ox,
			(float)sy + (float)ctx->oy,
			width, height,
			ctx->logical_w, ctx->logical_h,
			shady_spatial_toplevel_state(ctx->toplevel)->z,
			shady_window_motion_state_for_const(ctx->toplevel)->tilt_x,
			shady_window_motion_state_for_const(ctx->toplevel)->tilt_y);
		shady_mat4_multiply(mvp, ctx->vp, model);
	}
	struct shady_server *server = ctx->toplevel->server;
	bool focused = !wl_list_empty(&server->toplevels) &&
		server->toplevels.next == &ctx->toplevel->link;
	const float tint[4] = {
		server->config.window_tint[0] * (focused ? 0.92f : 1.0f),
		server->config.window_tint[1] * (focused ? 1.06f : 1.0f),
		server->config.window_tint[2] * (focused ? 1.16f : 1.0f),
		1.0f,
	};

	float water[4] = {
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_amplitude,
		ctx->toplevel->water_frequency,
		ctx->toplevel->water_speed,
		ctx->toplevel->water_phase,
	};
	shady_gl_pipeline_draw_window(&pipeline,
		attribs.target, attribs.tex, attribs.has_alpha,
		mvp, model, ctx->time_seconds,
		wobble_x,
		wobble_y,
		water,
		shady_close_state_for_const(ctx->toplevel)->progress,
		tint, server->config.window_effect_strength,
		server->config.window_brightness * (focused ? 1.08f : 1.0f));
}

static void render_spatial_overlays(struct shady_server *server,
		struct wlr_render_pass *pass, struct wlr_output *output,
		double ox, double oy, float scale) {
	struct shady_overlay_render_data ctx = {
		.pass = pass,
		.output = output,
		.ox = ox,
		.oy = oy,
		.scale = scale,
	};
	clock_gettime(CLOCK_MONOTONIC, &ctx.now);

	/* Layer-shell stays in normal 2D screen space in spatial mode. */
	struct shady_layer_surface *layer;
	wl_list_for_each(layer, &shady_desktop_state(server)->layer_surfaces, link) {
		if (layer->layer_surface->output &&
				layer->layer_surface->output != output) {
			continue;
		}
		wlr_scene_node_for_each_buffer(&layer->scene_layer->tree->node,
			render_scene_overlay_buffer, &ctx);
	}

	/* XDG popups remain readable/interactive while their parent is spatial. */
	struct shady_popup *popup;
	wl_list_for_each(popup, &server->popups, link) {
		wlr_scene_node_for_each_buffer(&popup->scene_tree->node,
			render_scene_overlay_buffer, &ctx);
	}
}

static void send_frame_done_surface(
	struct wlr_surface *surface,
	int sx,
	int sy,
	void *data
) {
	(void)sx;
	(void)sy;

	wlr_surface_send_frame_done(
		surface,
		data
	);
}


void shady_render_output_frame(
	struct shady_output *output
) {
	struct shady_server *server =
		output->server;

	struct wlr_output *wlr_output =
		output->wlr_output;

	struct wlr_scene_output *scene_output =
		wlr_scene_get_scene_output(
			server->scene,
			wlr_output
		);

	if (!scene_output) {
		return;
	}

	/* Safe/desktop mode uses wlroots' scene renderer directly. No custom 3D
	 * transforms, simulation, effects, or perpetual animation frame loop. */
	if (!server->config.spatial_mode || shady_desktop_state(server)->session_locked) {
		if (!wlr_scene_output_commit(scene_output, NULL)) {
			wlr_log(WLR_ERROR, "failed to commit desktop scene output");
		}
		return;
	}

	if (!pipeline_ready) {
		return;
	}

	bool profile = render_stats_enabled();
	struct timespec profile_frame_start = {0};
	struct timespec profile_effects_end = {0};
	struct timespec profile_windows_start = {0};
	struct timespec profile_windows_end = {0};
	struct timespec profile_overlay_start = {0};
	struct timespec profile_overlay_end = {0};
	struct timespec profile_submit_start = {0};
	struct timespec profile_submit_end = {0};
	if (profile) clock_gettime(CLOCK_MONOTONIC, &profile_frame_start);

	struct wlr_output_state state;

	wlr_output_state_init(
		&state
	);

	struct wlr_render_pass *pass =
		wlr_output_begin_render_pass(
			wlr_output,
			&state,
			NULL
		);

	if (!pass) {
		wlr_output_state_finish(
			&state
		);

		return;
	}

	int buf_w =
		wlr_output->width;

	int buf_h =
		wlr_output->height;

	float scale =
		wlr_output->scale;

	float logical_w =
		(float)buf_w / scale;

	float logical_h =
		(float)buf_h / scale;

	/*
	 * Attach a depth RBO to wlroots'
	 * color-only FBO for 3D occlusion.
	 */
	GLint fbo = 0;

	glGetIntegerv(
		GL_FRAMEBUFFER_BINDING,
		&fbo
	);

	ensure_depth_rbo(
		buf_w,
		buf_h
	);

	glBindFramebuffer(
		GL_FRAMEBUFFER,
		(GLuint)fbo
	);


	glFramebufferRenderbuffer(
		GL_FRAMEBUFFER,
		GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER,
		depth_rbo
	);

	glViewport(
		0,
		0,
		buf_w,
		buf_h
	);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	/*
	 * Slightly darker background makes
	 * the neon edges more visible.
	 */
	glClearColor(
		server->config.background_color[0],
		server->config.background_color[1],
		server->config.background_color[2],
		1.0f
	);

	glClearDepthf(1.0f);

	glClear(
		GL_COLOR_BUFFER_BIT |
		GL_DEPTH_BUFFER_BIT
	);

	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);

	shady_gl_pipeline_draw_background(&pipeline,
		server->config.background_top,
		server->config.background_horizon,
		server->config.background_bottom);

	float view[16];
	float proj[16];
	float vp[16];

	shady_render_camera_matrices(
		server,
		buf_w,
		buf_h,
		view,
		proj
	);

	shady_mat4_multiply(
		vp,
		proj,
		view
	);

	shady_environment_draw(server, &pipeline, view, proj);

	/*
	 * Draw a world-space reference plane before windows. Because it shares
	 * the depth buffer and camera VP matrix, orbiting immediately reveals
	 * perspective and per-window Z separation.
	 */
	shady_scene_effects_draw_floor(server, &pipeline, vp);
	shady_environment_draw_mesh(server, vp);

	double ox = 0;
	double oy = 0;

	wlr_output_layout_output_coords(
		server->output_layout,
		wlr_output,
		&ox,
		&oy
	);

	/*
	 * Same time value for every window
	 * rendered in this frame.
	 */
	float time_seconds =
		shader_time_seconds();
	const float window_tint[4] = {
		server->config.window_tint[0], server->config.window_tint[1],
		server->config.window_tint[2], 1.0f,
	};

	/*
	 * Advance the spatial desktop through one compositor-owned clock. This
	 * prevents physics/FPS state from being stepped independently per output.
	 */
	bool simulation_advanced = shady_experimental_update(
		server, logical_w, logical_h);

	/* Camera physics may have changed the view, so rebuild matrices. */
	if (shady_spatial_state(server)->runtime.camera.first_person && simulation_advanced) {
		shady_render_camera_matrices(server, buf_w, buf_h, view, proj);
		shady_mat4_multiply(vp, proj, view);
	}

	/* Snapshot texture lifetime remains a renderer concern. */
	update_close_snapshots();

	/*
	 * Project every mapped window onto the horizontal floor before drawing
	 * the windows themselves. The shadow shader uses the same deformation
	 * mesh and directional light as the window lighting.
	 */
	struct shady_toplevel *toplevel;
	shady_scene_effects_draw_shadows(server, &pipeline, vp,
		logical_w, logical_h, ox, oy);
	if (profile) {
		clock_gettime(CLOCK_MONOTONIC, &profile_effects_end);
		profile_windows_start = profile_effects_end;
	}


	wl_list_for_each_reverse(
		toplevel,
		&server->toplevels,
		link
	) {
		struct wlr_surface *surface =
			toplevel
				->xdg_toplevel
				->base
				->surface;

		if (!surface->mapped || !toplevel->scene_tree->node.enabled) {
			continue;
		}

		struct wlr_texture *texture =
			wlr_surface_get_texture(
				surface
			);

		if (
			!texture ||
			!wlr_texture_is_gles2(texture)
		) {
			continue;
		}

		struct wlr_gles2_texture_attribs attribs;

		wlr_gles2_texture_get_attribs(
			texture,
			&attribs
		);

		float tw =
			(float)surface->current.width;

		float th =
			(float)surface->current.height;

		if (tw <= 0.f || th <= 0.f) {
			tw =
				(float)texture->width /
				scale;

			th =
				(float)texture->height /
				scale;
		}

		float layout_x =
			(float)(
				toplevel
					->scene_tree
					->node
					.x +
				ox
			);

		float layout_y =
			(float)(
				toplevel
					->scene_tree
					->node
					.y +
				oy
			);

		float model[16];
		float mvp[16];

		if (
			shady_close_state_for_const(toplevel)->state ==
			SHADY_CLOSE_ARMED
		) {
			struct shady_close_snapshot *snapshot =
				ensure_close_snapshot(
					toplevel
				);

			if (
				snapshot &&
				snapshot->dirty
			) {
				GLuint new_texture = 0;

				if (
					shady_gl_pipeline_copy_texture(
						&pipeline,
						attribs.target,
						attribs.tex,
						texture->width,
						texture->height,
						&new_texture
					)
				) {
					if (snapshot->texture) {
						glDeleteTextures(
							1,
							&snapshot->texture
						);
					}

					snapshot->texture = new_texture;
					snapshot->texture_width = texture->width;
					snapshot->texture_height = texture->height;
					snapshot->x =
						(float)toplevel->scene_tree->node.x;
					snapshot->y =
						(float)toplevel->scene_tree->node.y;
					snapshot->width = tw;
					snapshot->height = th;
					snapshot->tilt_x = shady_window_motion_state_for_const(toplevel)->tilt_x;
					snapshot->tilt_y = shady_window_motion_state_for_const(toplevel)->tilt_y;
					snapshot->z = shady_spatial_toplevel_state(toplevel)->z;
					snapshot->wobble_x = shady_window_motion_state_for_const(toplevel)->wobble_x;
					snapshot->wobble_y = shady_window_motion_state_for_const(toplevel)->wobble_y;
					snapshot->has_alpha = attribs.has_alpha;
					snapshot->dirty = false;
				}
			}
		}

		bool screen_space = !shady_spatial_state(server)->runtime.camera.first_person &&
			(toplevel->fullscreen || toplevel->maximized);
		float wobble_x = shady_window_motion_state_for_const(toplevel)->wobble_x;
		float wobble_y = shady_window_motion_state_for_const(toplevel)->wobble_y;
		if (screen_space) {
			shady_mat4_identity(model);
			shady_screen_space_model(mvp, layout_x, layout_y, tw, th,
				logical_w, logical_h);
			wobble_x = 0.f;
			wobble_y = 0.f;
		} else {
			if(shady_spatial_state(server)->runtime.camera.first_person&&!shady_fps_toplevel_state_const(toplevel)->expanded){
				float cx=(layout_x+tw*.5f-logical_w*.5f)/logical_h;
				float cy=.5f-(layout_y+th*.5f)/logical_h;
				shady_window_cube_model(model,cx,cy,shady_spatial_toplevel_state(toplevel)->z,SHADY_FPS_CUBE_SIZE,
					shady_window_motion_state_for_const(toplevel)->tilt_x,shady_window_motion_state_for_const(toplevel)->tilt_y);
			}else{
				shady_window_model(
				model,
				layout_x,
				layout_y,
				tw,
				th,
				logical_w,
				logical_h,
				shady_spatial_toplevel_state(toplevel)->z,
				shady_window_motion_state_for_const(toplevel)->tilt_x,
				shady_window_motion_state_for_const(toplevel)->tilt_y
			);
			}
			shady_mat4_multiply(mvp, vp, model);
		}

		if (!screen_space) {
			shady_scene_effects_draw_sides(server, &pipeline, mvp, model,
				wobble_x, wobble_y, shady_close_state_for_const(toplevel)->progress);
		}

		if (screen_space) glDisable(GL_DEPTH_TEST);
		bool focused = !wl_list_empty(&server->toplevels) &&
			server->toplevels.next == &toplevel->link;
		float focused_tint[4] = {
			window_tint[0] * (focused ? 0.92f : 1.0f),
			window_tint[1] * (focused ? 1.06f : 1.0f),
			window_tint[2] * (focused ? 1.16f : 1.0f),
			1.0f,
		};
		float water[4] = {
			toplevel->fullscreen ? 0.f : toplevel->water_amplitude,
			toplevel->water_frequency,
			toplevel->water_speed,
			toplevel->water_phase,
		};
		shady_gl_pipeline_draw_window(
			&pipeline,
			attribs.target,
			attribs.tex,
			attribs.has_alpha,
			mvp,
			model,
			time_seconds,
			wobble_x,
			wobble_y,
			water,
			shady_close_state_for_const(toplevel)->progress,
			focused_tint,
			server->config.window_effect_strength,
			server->config.window_brightness * (focused ? 1.08f : 1.0f)
		);

		/* Render wl_subsurface children from the same scene subtree instead of
		 * silently dropping them in spatial mode. XDG popups are intentionally
		 * excluded here and composed later as 2D overlays. */
		struct shady_spatial_surface_render_data surface_ctx = {
			.toplevel = toplevel,
			.root_surface = surface,
			.vp = vp,
			.logical_w = logical_w,
			.logical_h = logical_h,
			.ox = ox,
			.oy = oy,
			.time_seconds = time_seconds,
			.screen_space = screen_space,
		};
		wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node,
			render_spatial_subsurface_buffer, &surface_ctx);
		if (screen_space) glEnable(GL_DEPTH_TEST);
	}

	struct shady_close_snapshot *snapshot;

	wl_list_for_each(
		snapshot,
		&close_snapshots,
		link
	) {
		if (
			!snapshot->animating ||
			!snapshot->texture ||
			snapshot->server != server
		) {
			continue;
		}

		float layout_x =
			snapshot->x +
			(float)ox;

		float layout_y =
			snapshot->y +
			(float)oy;

		float model[16];
		float mvp[16];

		shady_window_model(
			model,
			layout_x,
			layout_y,
			snapshot->width,
			snapshot->height,
			logical_w,
			logical_h,
			snapshot->z,
			snapshot->tilt_x,
			snapshot->tilt_y
		);

		shady_mat4_multiply(
			mvp,
			vp,
			model
		);

		shady_scene_effects_draw_sides(server, &pipeline, mvp, model,
			snapshot->wobble_x, snapshot->wobble_y, snapshot->progress);

		const float no_water[4] = {0.f, 1.f, 0.f, 0.f};
		shady_gl_pipeline_draw_window(
			&pipeline,
			GL_TEXTURE_2D,
			snapshot->texture,
			snapshot->has_alpha,
			mvp,
			model,
			time_seconds,
			0.0f,
			0.0f,
			no_water,
			snapshot->progress,
			window_tint,
			server->config.window_effect_strength,
			server->config.window_brightness
		);
	}
	if (profile) clock_gettime(CLOCK_MONOTONIC, &profile_windows_end);


	if (shady_spatial_state(server)->runtime.camera.first_person) {
		float cross_distance = 0.f;
		bool cross_target = shady_toplevel_at_camera_center(server, &cross_distance) != NULL;
		shady_gl_pipeline_draw_crosshair(&pipeline, cross_target,
			shady_fps_is_holding(server, NULL) ? false : shady_fps_state_for_const(server)->held_toplevel != NULL);
	}

	if (shady_spatial_state(server)->runtime.debug_ray) {
		/* The default floor/platform occupy the first two slots. Authored OBJ
		 * collision groups are orange so they are easy to distinguish. */
		for (size_t i=0;i<shady_spatial_state(server)->runtime.world.collider_count;i++)
			shady_gl_pipeline_draw_debug_box(&pipeline,vp,
				&shady_spatial_state(server)->runtime.world.colliders[i],i>=1);
		/* Yellow edges are the actual authored collision_* faces. Orange boxes
		 * are only their coarse broad-phase bounds. */
		for(size_t i=0;i<shady_spatial_state(server)->runtime.world.triangle_count;i++)
			shady_gl_pipeline_draw_debug_triangle(&pipeline,vp,
				&shady_spatial_state(server)->runtime.world.triangles[i]);
		struct shady_toplevel *debug_t;
		wl_list_for_each(debug_t,&server->toplevels,link){
			struct wlr_surface *ds=debug_t->xdg_toplevel->base->surface;
			if(!ds->mapped||!debug_t->scene_tree->node.enabled)continue;
			float dw=(float)ds->current.width,dh=(float)ds->current.height;
			if(dw<=0.f||dh<=0.f)continue;
			if(shady_spatial_state(server)->runtime.camera.first_person&&!shady_fps_toplevel_state_const(debug_t)->expanded){
				/* Debug the authoritative physics cube, not only the textured
				 * window face. This shows all 12 edges / six collision faces. */
				float dx=(float)debug_t->scene_tree->node.x+ox;
				float dy=(float)debug_t->scene_tree->node.y+oy;
				float dcx=(dx+dw*.5f-logical_w*.5f)/logical_h;
				float dcy=.5f-(dy+dh*.5f)/logical_h;
				float h=SHADY_FPS_CUBE_SIZE*.5f;
				struct shady_box_collider cube={
					dcx-h,dcx+h,dcy-h,dcy+h,
					shady_spatial_toplevel_state(debug_t)->z-h,shady_spatial_toplevel_state(debug_t)->z+h
				};
				shady_gl_pipeline_draw_debug_box(&pipeline,vp,&cube,false);
			}else{
				float dm[16];
				shady_window_model(dm,(float)debug_t->scene_tree->node.x+ox,
					(float)debug_t->scene_tree->node.y+oy,dw,dh,logical_w,logical_h,
					shady_spatial_toplevel_state(debug_t)->z,shady_window_motion_state_for_const(debug_t)->tilt_x,shady_window_motion_state_for_const(debug_t)->tilt_y);
				shady_gl_pipeline_draw_debug_window_body(&pipeline,vp,dm);
			}
		}
	}

	if (shady_spatial_state(server)->runtime.debug_ray && shady_spatial_state(server)->runtime.camera.first_person) {
		struct shady_vec3 eye, forward;
		shady_camera_eye(&shady_spatial_state(server)->runtime.camera, &eye);
		shady_camera_basis(&shady_spatial_state(server)->runtime.camera, NULL, NULL, &forward);
		float distance = 0.f;
		bool hit = shady_toplevel_at_camera_center(server, &distance) != NULL;
		if (!hit) distance = 4.0f;
		float origin[3] = { eye.x, eye.y, eye.z };
		float end[3] = {
			eye.x + forward.x * distance,
			eye.y + forward.y * distance,
			eye.z + forward.z * distance
		};
		shady_gl_pipeline_draw_debug_ray(&pipeline, vp, origin, end, hit);
	}

	glFramebufferRenderbuffer(
		GL_FRAMEBUFFER,
		GL_DEPTH_ATTACHMENT,
		GL_RENDERBUFFER,
		0
	);

	glDisable(GL_DEPTH_TEST);

	/* Compose protocol-driven 2D surfaces after the spatial pass. */
	if (profile) clock_gettime(CLOCK_MONOTONIC, &profile_overlay_start);
	render_spatial_overlays(server, pass, wlr_output, ox, oy, scale);
	if (profile) {
		clock_gettime(CLOCK_MONOTONIC, &profile_overlay_end);
		profile_submit_start = profile_overlay_end;
	}

	if (!wlr_render_pass_submit(pass)) {
		wlr_log(
			WLR_ERROR,
			"failed to submit render pass"
		);
	}

	wlr_output_commit_state(
		wlr_output,
		&state
	);

	wlr_output_state_finish(
		&state
	);

	if (profile) {
		clock_gettime(CLOCK_MONOTONIC, &profile_submit_end);
		output->profile_samples++;
		output->profile_effects_ns += timespec_delta_ns(
			&profile_frame_start, &profile_effects_end);
		output->profile_windows_ns += timespec_delta_ns(
			&profile_windows_start, &profile_windows_end);
		output->profile_overlay_ns += timespec_delta_ns(
			&profile_overlay_start, &profile_overlay_end);
		output->profile_submit_ns += timespec_delta_ns(
			&profile_submit_start, &profile_submit_end);
		output->profile_frame_ns += timespec_delta_ns(
			&profile_frame_start, &profile_submit_end);
	}

	struct timespec now;

	clock_gettime(
		CLOCK_MONOTONIC,
		&now
	);

	wl_list_for_each(
		toplevel,
		&server->toplevels,
		link
	) {
		struct wlr_surface *surface =
			toplevel
				->xdg_toplevel
				->base
				->surface;

		if (!surface->mapped || !toplevel->scene_tree->node.enabled) {
			continue;
		}

		wlr_surface_for_each_surface(
			surface,
			send_frame_done_surface,
			&now
		);
	}

	(void)scene_output;

	/* Keep the compositor fully idle when nothing is changing. Client commits,
	 * input, config changes and module actions explicitly wake rendering. Only
	 * time-dependent simulation/animation keeps the frame loop alive. */
	enum shady_continuous_reason continuous = spatial_continuous_reason(server);
	if (continuous != SHADY_CONTINUOUS_NONE) {
		switch (continuous) {
		case SHADY_CONTINUOUS_CAMERA: output->continuous_camera_frames++; break;
		case SHADY_CONTINUOUS_MOTION: output->continuous_motion_frames++; break;
		case SHADY_CONTINUOUS_EFFECT: output->continuous_effect_frames++; break;
		case SHADY_CONTINUOUS_PHYSICS: output->continuous_physics_frames++; break;
		case SHADY_CONTINUOUS_CLOSE: output->continuous_close_frames++; break;
		case SHADY_CONTINUOUS_SNAPSHOT: output->continuous_snapshot_frames++; break;
		default: break;
		}
		shady_render_schedule_output(output);
	}
}



void shady_render_toplevel_commit(
	struct shady_toplevel *toplevel
) {
	/* A client buffer commit is itself a render wake-up. The old perpetual
	 * frame loop hid this requirement by redrawing even when no damage existed. */
	shady_render_schedule_all_outputs(toplevel->server);

	if (
		shady_close_state_for_const(toplevel)->state !=
		SHADY_CLOSE_ARMED
	) {
		return;
	}

	struct shady_close_snapshot *snapshot =
		ensure_close_snapshot(
			toplevel
		);

	if (!snapshot) {
		return;
	}

	snapshot->dirty =
		true;
}

void shady_render_toplevel_unmap(
	struct shady_toplevel *toplevel
) {
	struct shady_close_snapshot *snapshot =
		find_close_snapshot(
			toplevel
		);

	if (
		!snapshot ||
		!snapshot->texture ||
		shady_close_state_for_const(toplevel)->state !=
			SHADY_CLOSE_ARMED
	) {
		return;
	}

	/*
	 * The client surface is going away.
	 *
	 * From this point onward the ghost owns everything it needs
	 * and must never dereference the toplevel again.
	 */
	snapshot->toplevel =
		NULL;

	snapshot->dirty =
		false;

	snapshot->animating =
		true;

	snapshot->progress =
		0.0f;

	shady_render_schedule_all_outputs(
		snapshot->server
	);
}

void shady_render_toplevel_destroy(
	struct shady_toplevel *toplevel
) {
	struct shady_close_snapshot *snapshot =
		find_close_snapshot(
			toplevel
	);

	if (!snapshot) {
		return;
	}

	/*
	 * If unmap already converted it into a ghost,
	 * find_close_snapshot() cannot find it because its toplevel
	 * pointer is NULL.
	 *
	 * Otherwise detach it so no dangling pointer remains.
	 */
	snapshot->toplevel =
		NULL;

	if (snapshot->texture) {
		snapshot->animating =
			true;

		snapshot->progress =
			0.0f;

		shady_render_schedule_all_outputs(
			snapshot->server
		);
	} else {
		wl_list_remove(
			&snapshot->link
		);

		free(
			snapshot
		);
	}
}