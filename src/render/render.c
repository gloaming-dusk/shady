#include "render.h"

#include <time.h>
#include <math.h>
#include <stdlib.h>

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
#include "../modules/fps/fps.h"
#include "../modules/close_animation/close_animation.h"
#include "../modules/scene_effects/scene_effects.h"
#include "../modules/environment/environment.h"

static struct shady_gl_pipeline pipeline;
static bool pipeline_ready;

static GLuint depth_rbo;
static int depth_rbo_w;
static int depth_rbo_h;

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
		&server->experimental.camera,
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

void shady_render_schedule_all_outputs(
	struct shady_server *server
) {
	struct shady_output *output;

	wl_list_for_each(
		output,
		&server->outputs,
		link
	) {
		wlr_output_schedule_frame(
			output->wlr_output
		);
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
	wl_list_for_each(layer, &server->layer_surfaces, link) {
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

	if (!pipeline_ready) {
		return;
	}

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
	if (!server->config.spatial_mode || server->session_locked) {
		if (!wlr_scene_output_commit(scene_output, NULL)) {
			wlr_log(WLR_ERROR, "failed to commit desktop scene output");
		}
		return;
	}

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
		0.055f,
		0.060f,
		0.085f,
		1.0f
	);

	glClearDepthf(1.0f);

	glClear(
		GL_COLOR_BUFFER_BIT |
		GL_DEPTH_BUFFER_BIT
	);

	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);

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

	/*
	 * Advance the spatial desktop through one compositor-owned clock. This
	 * prevents physics/FPS state from being stepped independently per output.
	 */
	bool simulation_advanced = shady_experimental_update(
		server, logical_w, logical_h);

	/* Camera physics may have changed the view, so rebuild matrices. */
	if (server->experimental.camera.first_person && simulation_advanced) {
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

		if (!surface->mapped) {
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
			toplevel->experimental.close.state ==
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
					snapshot->tilt_x = toplevel->experimental.motion.tilt_x;
					snapshot->tilt_y = toplevel->experimental.motion.tilt_y;
					snapshot->z = toplevel->experimental.z;
					snapshot->wobble_x = toplevel->experimental.motion.wobble_x;
					snapshot->wobble_y = toplevel->experimental.motion.wobble_y;
					snapshot->has_alpha = attribs.has_alpha;
					snapshot->dirty = false;
				}
			}
		}

		if(server->experimental.camera.first_person&&!toplevel->experimental.fps_expanded){
			float cx=(layout_x+tw*.5f-logical_w*.5f)/logical_h;
			float cy=.5f-(layout_y+th*.5f)/logical_h;
			shady_window_cube_model(model,cx,cy,toplevel->experimental.z,SHADY_FPS_CUBE_SIZE,
				toplevel->experimental.motion.tilt_x,toplevel->experimental.motion.tilt_y);
		}else{
			shady_window_model(
			model,
			layout_x,
			layout_y,
			tw,
			th,
			logical_w,
			logical_h,
			toplevel->experimental.z,
			toplevel->experimental.motion.tilt_x,
			toplevel->experimental.motion.tilt_y
		);
		}

		shady_mat4_multiply(
			mvp,
			vp,
			model
		);

		shady_scene_effects_draw_sides(server, &pipeline, mvp, model,
			toplevel->experimental.motion.wobble_x, toplevel->experimental.motion.wobble_y, toplevel->experimental.close.progress);

		shady_gl_pipeline_draw_window(
			&pipeline,
			attribs.target,
			attribs.tex,
			attribs.has_alpha,
			mvp,
			model,
			time_seconds,
			toplevel->experimental.motion.wobble_x,
			toplevel->experimental.motion.wobble_y,
			toplevel->experimental.close.progress
		);
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
			snapshot->progress
		);
	}


	if (server->experimental.camera.first_person) {
		float cross_distance = 0.f;
		bool cross_target = shady_toplevel_at_camera_center(server, &cross_distance) != NULL;
		shady_gl_pipeline_draw_crosshair(&pipeline, cross_target,
			shady_fps_is_holding(server, NULL) ? false : server->experimental.fps.held_toplevel != NULL);
	}

	if (server->experimental.debug_ray) {
		/* The default floor/platform occupy the first two slots. Authored OBJ
		 * collision groups are orange so they are easy to distinguish. */
		for (size_t i=0;i<server->experimental.world.collider_count;i++)
			shady_gl_pipeline_draw_debug_box(&pipeline,vp,
				&server->experimental.world.colliders[i],i>=1);
		/* Yellow edges are the actual authored collision_* faces. Orange boxes
		 * are only their coarse broad-phase bounds. */
		for(size_t i=0;i<server->experimental.world.triangle_count;i++)
			shady_gl_pipeline_draw_debug_triangle(&pipeline,vp,
				&server->experimental.world.triangles[i]);
		struct shady_toplevel *debug_t;
		wl_list_for_each(debug_t,&server->toplevels,link){
			struct wlr_surface *ds=debug_t->xdg_toplevel->base->surface;
			if(!ds->mapped)continue;
			float dw=(float)ds->current.width,dh=(float)ds->current.height;
			if(dw<=0.f||dh<=0.f)continue;
			if(server->experimental.camera.first_person&&!debug_t->experimental.fps_expanded){
				/* Debug the authoritative physics cube, not only the textured
				 * window face. This shows all 12 edges / six collision faces. */
				float dx=(float)debug_t->scene_tree->node.x+ox;
				float dy=(float)debug_t->scene_tree->node.y+oy;
				float dcx=(dx+dw*.5f-logical_w*.5f)/logical_h;
				float dcy=.5f-(dy+dh*.5f)/logical_h;
				float h=SHADY_FPS_CUBE_SIZE*.5f;
				struct shady_box_collider cube={
					dcx-h,dcx+h,dcy-h,dcy+h,
					debug_t->experimental.z-h,debug_t->experimental.z+h
				};
				shady_gl_pipeline_draw_debug_box(&pipeline,vp,&cube,false);
			}else{
				float dm[16];
				shady_window_model(dm,(float)debug_t->scene_tree->node.x+ox,
					(float)debug_t->scene_tree->node.y+oy,dw,dh,logical_w,logical_h,
					debug_t->experimental.z,debug_t->experimental.motion.tilt_x,debug_t->experimental.motion.tilt_y);
				shady_gl_pipeline_draw_debug_window_body(&pipeline,vp,dm);
			}
		}
	}

	if (server->experimental.debug_ray && server->experimental.camera.first_person) {
		struct shady_vec3 eye, forward;
		shady_camera_eye(&server->experimental.camera, &eye);
		shady_camera_basis(&server->experimental.camera, NULL, NULL, &forward);
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
	render_spatial_overlays(server, pass, wlr_output, ox, oy, scale);

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

		if (!surface->mapped) {
			continue;
		}

		wlr_surface_for_each_surface(
			surface,
			send_frame_done_surface,
			&now
		);
	}

	(void)scene_output;

	/*
	 * u_time changes continuously, so request
	 * another frame even when clients are idle.
	 */
	wlr_output_schedule_frame(
		wlr_output
	);
}



void shady_render_toplevel_commit(
	struct shady_toplevel *toplevel
) {
	if (
		toplevel->experimental.close.state !=
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

	shady_render_schedule_all_outputs(
		toplevel->server
	);
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
		toplevel->experimental.close.state !=
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