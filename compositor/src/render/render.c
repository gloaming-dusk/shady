#include "render.h"

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <wayland-server-core.h>

#include <wlr/render/egl.h>
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

#include <EGL/egl.h>
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
#include "../modules/close_animation/state.h"
#include "../modules/close_animation/close_animation.h"
#include "../modules/scene_effects/scene_effects.h"
#include "../modules/environment/environment.h"

static struct shady_gl_pipeline pipeline;
static bool pipeline_ready;
static struct wlr_renderer *plugin_renderer;
/* llvmpipe and friends rasterize on worker threads and put no fence on the
 * buffer, so a page flip can scan out a frame they are still drawing. */
static bool renderer_is_software;

#define SHADY_PLUGIN_SHADER_MAX 64
#define SHADY_PLUGIN_HOOK_MAX 64
struct plugin_shader_slot {
	bool used;
	shady_shader_program id;
	void *owner;
	GLuint program;
};
struct plugin_hook_slot {
	bool used;
	shady_render_hook_id id;
	void *owner;
	uint32_t stage;
	shady_render_callback callback;
	void *user_data;
};
static struct plugin_shader_slot plugin_shaders[SHADY_PLUGIN_SHADER_MAX];
static struct plugin_hook_slot plugin_hooks[SHADY_PLUGIN_HOOK_MAX];
static uint64_t next_plugin_shader_id = 1;
static uint64_t next_plugin_hook_id = 1;
static GLuint plugin_fullscreen_vbo;
/* Snapshot of the output for shader_draw_fullscreen_scene; resized on demand. */
static GLuint plugin_scene_texture;
static GLint plugin_scene_width, plugin_scene_height;
/* True only while render hooks run, i.e. while the output FBO is bound. */
static bool plugin_hooks_running;

static GLuint depth_rbo;
static int depth_rbo_w;
static int depth_rbo_h;
static int render_stats_enabled_cache = -1;

static bool plugin_make_current(void) {
	if (!plugin_renderer || !wlr_renderer_is_gles2(plugin_renderer)) return false;
	struct wlr_egl *egl = wlr_gles2_renderer_get_egl(plugin_renderer);
	if (!egl) return false;
	return eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE,
		EGL_NO_SURFACE, wlr_egl_get_context(egl)) == EGL_TRUE;
}

static char *plugin_read_text_file(const char *path) {
	if (!path || !*path) return NULL;
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	long len = ftell(f);
	if (len < 0) { fclose(f); return NULL; }
	rewind(f);
	char *buf = malloc((size_t)len + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t n = fread(buf, 1, (size_t)len, f);
	fclose(f);
	if (n != (size_t)len) { free(buf); return NULL; }
	buf[len] = '\0';
	return buf;
}

static GLuint plugin_compile_shader(GLenum type, const char *src, const char *label) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &src, NULL);
	glCompileShader(shader);
	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[2048] = {0};
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		wlr_log(WLR_ERROR, "plugin shader compile failed (%s): %s", label, log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint plugin_link_shader_files(const char *vert_path, const char *frag_path) {
	char *vert = plugin_read_text_file(vert_path);
	char *frag = plugin_read_text_file(frag_path);
	if (!vert || !frag) {
		wlr_log(WLR_ERROR, "plugin shader: failed to read %s / %s",
			vert_path ? vert_path : "(null)", frag_path ? frag_path : "(null)");
		free(vert); free(frag);
		return 0;
	}
	GLuint vs = plugin_compile_shader(GL_VERTEX_SHADER, vert, vert_path);
	GLuint fs = plugin_compile_shader(GL_FRAGMENT_SHADER, frag, frag_path);
	free(vert); free(frag);
	if (!vs || !fs) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return 0;
	}
	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glBindAttribLocation(prog, 0, "a_pos");
	glBindAttribLocation(prog, 2, "a_uv");
	glLinkProgram(prog);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = GL_FALSE;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[2048] = {0};
		glGetProgramInfoLog(prog, sizeof(log), NULL, log);
		wlr_log(WLR_ERROR, "plugin shader link failed: %s", log);
		glDeleteProgram(prog);
		return 0;
	}
	return prog;
}

static struct plugin_shader_slot *plugin_shader_find(shady_shader_program id, void *owner) {
	for (size_t i = 0; i < SHADY_PLUGIN_SHADER_MAX; i++)
		if (plugin_shaders[i].used && plugin_shaders[i].id == id &&
				(!owner || plugin_shaders[i].owner == owner)) return &plugin_shaders[i];
	return NULL;
}

shady_shader_program shady_render_plugin_shader_create(struct shady_server *server,
		void *owner, const char *vertex_path, const char *fragment_path) {
	(void)server;
	if (!owner || !plugin_make_current()) return 0;
	size_t slot = SHADY_PLUGIN_SHADER_MAX;
	for (size_t i = 0; i < SHADY_PLUGIN_SHADER_MAX; i++)
		if (!plugin_shaders[i].used) { slot = i; break; }
	if (slot == SHADY_PLUGIN_SHADER_MAX) return 0;
	GLuint prog = plugin_link_shader_files(vertex_path, fragment_path);
	if (!prog) return 0;
	shady_shader_program id = next_plugin_shader_id++;
	if (!id) id = next_plugin_shader_id++;
	plugin_shaders[slot] = (struct plugin_shader_slot){
		.used = true, .id = id, .owner = owner, .program = prog
	};
	return id;
}

bool shady_render_plugin_shader_valid(struct shady_server *server, void *owner,
		shady_shader_program program) {
	(void)server;
	return plugin_shader_find(program, owner) != NULL;
}

bool shady_render_plugin_shader_destroy(struct shady_server *server, void *owner,
		shady_shader_program program) {
	struct plugin_shader_slot *slot = plugin_shader_find(program, owner);
	if (!slot || !plugin_make_current()) return false;
	if (server) {
		struct shady_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
			if (toplevel->plugin_shader_owner == owner &&
					toplevel->plugin_shader_program == program) {
				toplevel->plugin_shader_program = 0;
				toplevel->plugin_shader_owner = NULL;
			}
		}
	}
	glDeleteProgram(slot->program);
	memset(slot, 0, sizeof(*slot));
	return true;
}

static GLint plugin_uniform_location(void *owner, shady_shader_program program,
		const char *name, GLuint *gl_program) {
	struct plugin_shader_slot *slot = plugin_shader_find(program, owner);
	if (!slot || !name || !*name || !plugin_make_current()) return -1;
	if (gl_program) *gl_program = slot->program;
	return glGetUniformLocation(slot->program, name);
}

bool shady_render_plugin_shader_uniform_float(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float value) {
	(void)server;
	GLuint gl_program = 0;
	GLint loc = plugin_uniform_location(owner, program, name, &gl_program);
	if (loc < 0) return false;
	GLint previous = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
	glUseProgram(gl_program);
	glUniform1f(loc, value);
	glUseProgram((GLuint)previous);
	return true;
}
bool shady_render_plugin_shader_uniform_int(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, int value) {
	(void)server;
	GLuint gl_program = 0;
	GLint loc = plugin_uniform_location(owner, program, name, &gl_program);
	if (loc < 0) return false;
	GLint previous = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
	glUseProgram(gl_program);
	glUniform1i(loc, value);
	glUseProgram((GLuint)previous);
	return true;
}
bool shady_render_plugin_shader_uniform_vec2(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float x, float y) {
	(void)server;
	GLuint gl_program = 0;
	GLint loc = plugin_uniform_location(owner, program, name, &gl_program);
	if (loc < 0) return false;
	GLint previous = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
	glUseProgram(gl_program);
	glUniform2f(loc, x, y);
	glUseProgram((GLuint)previous);
	return true;
}
bool shady_render_plugin_shader_uniform_vec4(struct shady_server *server, void *owner,
		shady_shader_program program, const char *name, float x, float y, float z, float w) {
	(void)server;
	GLuint gl_program = 0;
	GLint loc = plugin_uniform_location(owner, program, name, &gl_program);
	if (loc < 0) return false;
	GLint previous = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &previous);
	glUseProgram(gl_program);
	glUniform4f(loc, x, y, z, w);
	glUseProgram((GLuint)previous);
	return true;
}

static bool plugin_shader_source_texture(struct shady_server *server,
		struct shady_toplevel *target, GLenum *texture_target, GLuint *texture_id,
		int *width, int *height) {
	if (!server || !target || !target->plugin_shader_source) return false;
	struct shady_toplevel *source = NULL;
	struct shady_toplevel *candidate;
	wl_list_for_each(candidate, &server->all_toplevels, all_link) {
		if (candidate == target->plugin_shader_source) {
			source = candidate;
			break;
		}
	}
	if (!source || !source->xdg_toplevel || !source->scene_tree ||
			!source->scene_tree->node.enabled) return false;
	struct wlr_surface *surface = source->xdg_toplevel->base->surface;
	if (!surface || !surface->mapped) return false;
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (!texture || !wlr_texture_is_gles2(texture)) return false;
	struct wlr_gles2_texture_attribs attribs;
	wlr_gles2_texture_get_attribs(texture, &attribs);
	if (texture_target) *texture_target = attribs.target;
	if (texture_id) *texture_id = attribs.tex;
	if (width) *width = texture->width;
	if (height) *height = texture->height;
	return true;
}

static bool plugin_shader_draw_window(struct shady_server *server,
		shady_shader_program shader_program, void *shader_owner,
		GLenum source_target, GLuint source_texture,
		int texture_width, int texture_height, bool has_alpha,
		GLenum portal_target, GLuint portal_texture,
		int portal_width, int portal_height,
		const float mvp[16], const float model[16], const float frame_rect[4],
		float time_seconds, float output_width, float output_height,
		float window_width, float window_height, float wobble_x, float wobble_y,
		const float water[4], const float water_surface[4],
		const float border_color[4], const float border_width[2],
		float close_progress, const float close_effect[4], const float tint[4],
		float effect_strength, float brightness,
		const float *mesh_vertices, size_t mesh_vertex_count,
		const uint16_t *mesh_indices, size_t mesh_index_count,
		const float params[SHADY_WINDOW_SHADER_PARAMS * 4]) {
	(void)server;
	if (!shader_program || !shader_owner) return false;
	struct plugin_shader_slot *slot = plugin_shader_find(shader_program, shader_owner);
	if (!slot || !plugin_make_current()) return false;

	GLuint sampled_texture = source_texture;
	GLuint copied_texture = 0;
	GLuint sampled_portal_texture = portal_texture;
	GLuint copied_portal_texture = 0;
	if (source_target != GL_TEXTURE_2D) {
		if (!shady_gl_pipeline_copy_texture(&pipeline, source_target, source_texture,
				texture_width, texture_height, &copied_texture))
			return false;
		sampled_texture = copied_texture;
	}
	bool portal_available = portal_texture != 0 && portal_width > 0 && portal_height > 0;
	if (portal_available && portal_target != GL_TEXTURE_2D) {
		if (!shady_gl_pipeline_copy_texture(&pipeline, portal_target, portal_texture,
				portal_width, portal_height, &copied_portal_texture)) {
			if (copied_texture) glDeleteTextures(1, &copied_texture);
			return false;
		}
		sampled_portal_texture = copied_portal_texture;
	}

	GLint old_program = 0, old_buffer = 0, old_texture = 0,
		old_portal_texture = 0, old_active_texture = 0;
	GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
	GLboolean blend = glIsEnabled(GL_BLEND);
	GLboolean depth_mask = GL_TRUE;
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_portal_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);

	glUseProgram(slot->program);
	GLint loc = glGetUniformLocation(slot->program, "u_mvp");
	if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, mvp);
	loc = glGetUniformLocation(slot->program, "u_model");
	if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, model);
	loc = glGetUniformLocation(slot->program, "u_frame_rect");
	if (loc >= 0) glUniform4fv(loc, 1, frame_rect);
	loc = glGetUniformLocation(slot->program, "u_time");
	if (loc >= 0) glUniform1f(loc, time_seconds);
	loc = glGetUniformLocation(slot->program, "u_resolution");
	if (loc >= 0) glUniform2f(loc, output_width, output_height);
	loc = glGetUniformLocation(slot->program, "u_window_size");
	if (loc >= 0) glUniform2f(loc, window_width, window_height);
	loc = glGetUniformLocation(slot->program, "u_has_alpha");
	if (loc >= 0) glUniform1f(loc, has_alpha ? 1.f : 0.f);
	loc = glGetUniformLocation(slot->program, "u_wobble");
	if (loc >= 0) glUniform2f(loc, wobble_x, wobble_y);
	loc = glGetUniformLocation(slot->program, "u_water");
	if (loc >= 0) glUniform4fv(loc, 1, water);
	loc = glGetUniformLocation(slot->program, "u_water_surface");
	if (loc >= 0) glUniform4fv(loc, 1, water_surface);
	loc = glGetUniformLocation(slot->program, "u_border_color");
	if (loc >= 0) glUniform4fv(loc, 1, border_color);
	loc = glGetUniformLocation(slot->program, "u_border_width");
	if (loc >= 0) glUniform2fv(loc, 1, border_width);
	loc = glGetUniformLocation(slot->program, "u_close_progress");
	if (loc >= 0) glUniform1f(loc, close_progress);
	loc = glGetUniformLocation(slot->program, "u_close_effect");
	if (loc >= 0) glUniform4fv(loc, 1, close_effect);
	loc = glGetUniformLocation(slot->program, "u_tint");
	if (loc >= 0) glUniform4fv(loc, 1, tint);
	loc = glGetUniformLocation(slot->program, "u_effect_strength");
	if (loc >= 0) glUniform1f(loc, effect_strength);
	loc = glGetUniformLocation(slot->program, "u_brightness");
	if (loc >= 0) glUniform1f(loc, brightness);
	loc = glGetUniformLocation(slot->program, "u_light_dir");
	if (loc >= 0) glUniform3f(loc, -0.45f, 0.72f, 0.53f);
	loc = glGetUniformLocation(slot->program, "u_tex");
	if (loc >= 0) glUniform1i(loc, 0);
	loc = glGetUniformLocation(slot->program, "u_portal_tex");
	if (loc >= 0) glUniform1i(loc, 1);
	loc = glGetUniformLocation(slot->program, "u_portal_available");
	if (loc >= 0) glUniform1f(loc, portal_available ? 1.f : 0.f);
	loc = glGetUniformLocation(slot->program, "u_portal_size");
	if (loc >= 0) glUniform2f(loc, (float)portal_width, (float)portal_height);
	/* GLES2 accepts either spelling for the first element of a uniform array. */
	loc = glGetUniformLocation(slot->program, "u_params");
	if (loc < 0) loc = glGetUniformLocation(slot->program, "u_params[0]");
	if (loc >= 0) {
		static const float no_params[SHADY_WINDOW_SHADER_PARAMS * 4];
		glUniform4fv(loc, SHADY_WINDOW_SHADER_PARAMS, params ? params : no_params);
	}
	/* Optional rounded-frame contract (see docs/SHADER_API.md). Mesh
	 * representations bring their own UV layout, so they opt out. */
	bool rounded_frame = !mesh_vertices &&
		pipeline.frame_px[0] > 0.f && pipeline.frame_px[1] > 0.f;
	loc = glGetUniformLocation(slot->program, "u_frame_px");
	if (loc >= 0) glUniform2f(loc,
		rounded_frame ? pipeline.frame_px[0] : 0.f,
		rounded_frame ? pipeline.frame_px[1] : 0.f);
	loc = glGetUniformLocation(slot->program, "u_frame_shape");
	if (loc >= 0) glUniform2f(loc, pipeline.frame_radius, pipeline.frame_border);
	bool frame_clips = rounded_frame && pipeline.frame_radius > 0.f &&
		glGetUniformLocation(slot->program, "u_frame_px") >= 0;

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sampled_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	if (portal_available) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, sampled_portal_texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glActiveTexture(GL_TEXTURE0);
	}

	if (has_alpha) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE);
	} else if (frame_clips) {
		/* Opaque client, anti-aliased rounded corners: blend the fringe. */
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	GLuint draw_vbo = pipeline.mesh_vbo;
	GLsizei draw_count = pipeline.mesh_vertex_count;
	bool use_mesh_uv = false;
	if (mesh_vertices && mesh_vertex_count >= 3 &&
			shady_gl_pipeline_prepare_dynamic_mesh(&pipeline, mesh_vertices,
				mesh_vertex_count, mesh_indices, mesh_index_count)) {
		draw_vbo = pipeline.dynamic_mesh_vbo;
		draw_count = pipeline.dynamic_mesh_vertex_count;
		use_mesh_uv = true;
	}
	loc = glGetUniformLocation(slot->program, "u_use_vertex_uv");
	if (loc >= 0) glUniform1f(loc, use_mesh_uv ? 1.f : 0.f);
	glBindBuffer(GL_ARRAY_BUFFER, draw_vbo);
	GLsizei draw_stride = (use_mesh_uv ? 5 : 3) * sizeof(GLfloat);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, draw_stride, (void *)0);
	glEnableVertexAttribArray(0);
	if (use_mesh_uv) {
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, draw_stride,
			(void *)(3 * sizeof(GLfloat)));
		glEnableVertexAttribArray(2);
	}
	glDrawArrays(GL_TRIANGLES, 0, draw_count);
	glDisableVertexAttribArray(0);
	if (use_mesh_uv) glDisableVertexAttribArray(2);

	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buffer);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_portal_texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	glActiveTexture((GLenum)old_active_texture);
	glUseProgram((GLuint)old_program);
	glDepthMask(depth_mask);
	if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (copied_texture) glDeleteTextures(1, &copied_texture);
	if (copied_portal_texture) glDeleteTextures(1, &copied_portal_texture);
	return true;
}

/* Draw a fullscreen triangle with `slot`. When scene_texture is non-zero it is
 * bound to unit 0 as u_scene, with its size in u_scene_size. */
static bool plugin_draw_fullscreen(struct plugin_shader_slot *slot,
		GLuint scene_texture, GLint scene_width, GLint scene_height) {
	if (!plugin_fullscreen_vbo) {
		static const float verts[] = {-1.f,-1.f, 3.f,-1.f, -1.f,3.f};
		glGenBuffers(1, &plugin_fullscreen_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, plugin_fullscreen_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
	}
	GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
	GLboolean blend = glIsEnabled(GL_BLEND);
	GLint old_program = 0, old_buffer = 0;
	GLint blend_src_rgb = 0, blend_dst_rgb = 0, blend_src_alpha = 0, blend_dst_alpha = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
	GLint old_active_texture = 0, old_texture = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(slot->program);
	if (scene_texture) {
		glBindTexture(GL_TEXTURE_2D, scene_texture);
		GLint loc = glGetUniformLocation(slot->program, "u_scene");
		if (loc >= 0) glUniform1i(loc, 0);
		loc = glGetUniformLocation(slot->program, "u_scene_size");
		if (loc >= 0) glUniform2f(loc, (float)scene_width, (float)scene_height);
	}
	glBindBuffer(GL_ARRAY_BUFFER, plugin_fullscreen_vbo);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buffer);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	glActiveTexture((GLenum)old_active_texture);
	glUseProgram((GLuint)old_program);
	glBlendFuncSeparate((GLenum)blend_src_rgb, (GLenum)blend_dst_rgb,
		(GLenum)blend_src_alpha, (GLenum)blend_dst_alpha);
	if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	return true;
}

bool shady_render_plugin_shader_draw_fullscreen(struct shady_server *server, void *owner,
		shady_shader_program program) {
	(void)server;
	struct plugin_shader_slot *slot = plugin_shader_find(program, owner);
	if (!slot || !plugin_make_current()) return false;
	return plugin_draw_fullscreen(slot, 0, 0, 0);
}

/* Copy the bound output framebuffer into plugin_scene_texture. GL_RGB is a
 * valid copy target for both alpha and alpha-less output formats. */
static bool plugin_capture_scene(GLint *width, GLint *height) {
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	if (viewport[2] <= 0 || viewport[3] <= 0) return false;
	GLint old_active_texture = 0, old_texture = 0;
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
	if (!plugin_scene_texture) glGenTextures(1, &plugin_scene_texture);
	glBindTexture(GL_TEXTURE_2D, plugin_scene_texture);
	if (plugin_scene_width != viewport[2] || plugin_scene_height != viewport[3]) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, viewport[2], viewport[3], 0,
			GL_RGB, GL_UNSIGNED_BYTE, NULL);
		plugin_scene_width = viewport[2];
		plugin_scene_height = viewport[3];
	}
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1],
		viewport[2], viewport[3]);
	bool ok = glGetError() == GL_NO_ERROR;
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	glActiveTexture((GLenum)old_active_texture);
	*width = viewport[2];
	*height = viewport[3];
	return ok;
}

bool shady_render_plugin_shader_draw_fullscreen_scene(struct shady_server *server,
		void *owner, shady_shader_program program) {
	(void)server;
	if (!plugin_hooks_running) return false;
	struct plugin_shader_slot *slot = plugin_shader_find(program, owner);
	if (!slot || !plugin_make_current()) return false;
	/* Drop stale errors so the copy check below sees only its own. Bounded:
	 * a lost context reports an error on every call. */
	for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; i++) {}
	GLint width = 0, height = 0;
	if (!plugin_capture_scene(&width, &height)) return false;
	return plugin_draw_fullscreen(slot, plugin_scene_texture, width, height);
}

shady_render_hook_id shady_render_plugin_hook_add(struct shady_server *server, void *owner,
		uint32_t stage, shady_render_callback callback, void *user_data) {
	(void)server;
	if (!owner || !callback || stage > SHADY_RENDER_STAGE_OVERLAY) return 0;
	for (size_t i = 0; i < SHADY_PLUGIN_HOOK_MAX; i++) if (!plugin_hooks[i].used) {
		shady_render_hook_id id = next_plugin_hook_id++;
		if (!id) id = next_plugin_hook_id++;
		plugin_hooks[i] = (struct plugin_hook_slot){
			.used = true, .id = id, .owner = owner, .stage = stage,
			.callback = callback, .user_data = user_data
		};
		return id;
	}
	return 0;
}

bool shady_render_plugin_hook_remove(struct shady_server *server, void *owner,
		shady_render_hook_id hook) {
	(void)server;
	for (size_t i = 0; i < SHADY_PLUGIN_HOOK_MAX; i++)
		if (plugin_hooks[i].used && plugin_hooks[i].id == hook &&
				plugin_hooks[i].owner == owner) {
			memset(&plugin_hooks[i], 0, sizeof(plugin_hooks[i]));
			return true;
		}
	return false;
}

void shady_render_plugin_cleanup_owner(struct shady_server *server, void *owner) {
	if (!owner) return;
	if (server) {
		struct shady_toplevel *toplevel;
		wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
			if (toplevel->plugin_shader_owner == owner) {
				toplevel->plugin_shader_program = 0;
				toplevel->plugin_shader_owner = NULL;
			}
			if (toplevel->plugin_shader_source_owner == owner) {
				toplevel->plugin_shader_source = NULL;
				toplevel->plugin_shader_source_owner = NULL;
			}
		}
	}
	if (plugin_make_current()) for (size_t i = 0; i < SHADY_PLUGIN_SHADER_MAX; i++)
		if (plugin_shaders[i].used && plugin_shaders[i].owner == owner) {
			glDeleteProgram(plugin_shaders[i].program);
			memset(&plugin_shaders[i], 0, sizeof(plugin_shaders[i]));
		}
	for (size_t i = 0; i < SHADY_PLUGIN_HOOK_MAX; i++)
		if (plugin_hooks[i].used && plugin_hooks[i].owner == owner)
			memset(&plugin_hooks[i], 0, sizeof(plugin_hooks[i]));
}

static void plugin_render_hooks_run(struct shady_server *server, uint32_t stage,
		struct shady_output *output, int width, int height,
		float logical_width, float logical_height, float time_seconds) {
	struct shady_render_context context = {
		.struct_size = sizeof(context), .stage = stage, .output = (shady_output)output,
		.width = width, .height = height, .logical_width = logical_width,
		.logical_height = logical_height, .time_seconds = time_seconds,
	};
	float view[16], proj[16];
	shady_render_camera_matrices(server, width, height, view, proj);
	/* look_at rows: right, up, -forward; translation is -R * eye. */
	for (int i = 0; i < 3; i++) {
		context.camera_right[i] = view[i * 4 + 0];
		context.camera_up[i] = view[i * 4 + 1];
		context.camera_forward[i] = -view[i * 4 + 2];
	}
	for (int i = 0; i < 3; i++) {
		context.camera_position[i] = -(view[i * 4 + 0] * view[12] +
			view[i * 4 + 1] * view[13] + view[i * 4 + 2] * view[14]);
	}
	context.tan_half_fov_y = tanf(SHADY_CAMERA_FOV_Y * 0.5f);
	context.aspect = height > 0 ? (float)width / (float)height : 1.f;
	plugin_hooks_running = true;
	for (size_t i = 0; i < SHADY_PLUGIN_HOOK_MAX; i++)
		if (plugin_hooks[i].used && plugin_hooks[i].stage == stage)
			plugin_hooks[i].callback((shady_host)server, &context, plugin_hooks[i].user_data);
	plugin_hooks_running = false;
}

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
	uint32_t close_style;
	float close_duration;
	float close_strength;
	float close_direction_x;
	float close_direction_y;
	shady_shader_program plugin_shader_program;
	void *plugin_shader_owner;
	float plugin_shader_params[SHADY_WINDOW_SHADER_PARAMS * 4];
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
	plugin_renderer = renderer;
	memset(plugin_shaders, 0, sizeof(plugin_shaders));
	memset(plugin_hooks, 0, sizeof(plugin_hooks));
	next_plugin_shader_id = 1;
	next_plugin_hook_id = 1;
	pipeline_ready =
		shady_gl_pipeline_init(
			&pipeline,
			renderer
		);
	if (pipeline_ready) shady_environment_gl_init();
	if (pipeline_ready && plugin_make_current()) {
		const char *name = (const char *)glGetString(GL_RENDERER);
		renderer_is_software = name && (strstr(name, "llvmpipe") ||
			strstr(name, "softpipe") || strstr(name, "swrast"));
		if (renderer_is_software)
			wlr_log(WLR_INFO, "software renderer (%s): finishing each frame before commit",
				name);
	}

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

static void layer_effects_fini(void);
static void layer_transforms_fini(void);

void shady_render_fini(void) {
	layer_effects_fini();
	layer_transforms_fini();
	if (plugin_make_current()) {
		for (size_t i = 0; i < SHADY_PLUGIN_SHADER_MAX; i++) {
			if (plugin_shaders[i].used) glDeleteProgram(plugin_shaders[i].program);
		}
		if (plugin_fullscreen_vbo) glDeleteBuffers(1, &plugin_fullscreen_vbo);
		if (plugin_scene_texture) glDeleteTextures(1, &plugin_scene_texture);
	}
	memset(plugin_shaders, 0, sizeof(plugin_shaders));
	memset(plugin_hooks, 0, sizeof(plugin_hooks));
	plugin_fullscreen_vbo = 0;
	plugin_scene_texture = 0;
	plugin_scene_width = plugin_scene_height = 0;
	plugin_renderer = NULL;
	if (depth_rbo) {
		glDeleteRenderbuffers(
			1,
			&depth_rbo
		);

		depth_rbo = 0;
	}

	if (pipeline_ready) {
		shady_environment_gl_fini();
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

		if (toplevel->motion.animating) return SHADY_CONTINUOUS_MOTION;

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

/* ---- layer-shell backdrop effects ------------------------------------- */

/*
 * Before each buffer of a matching layer surface is drawn, the output
 * region under it (plus a margin for blur taps) is copied to a texture and
 * drawn through the effect's shader, masked by the buffer's own alpha so
 * rounded or shaped panels get glass only where they draw. The surface is
 * then composited on top as usual.
 *
 * Window coordinates here are the output buffer's pixels, top-down: the
 * spatial pass renders with a flipped projection, so image row 0 is GL
 * row 0 (the same convention shader_draw_fullscreen_scene relies on).
 */

#define LAYER_EFFECT_MAX 16

struct layer_effect {
	bool used;
	char name_space[64];
	struct shady_layer_effect_desc desc;
	char shader_path[512];
	/* [0] samples the mask as sampler2D, [1] as samplerExternalOES. */
	GLuint program[2];
	bool failed[2];
	bool uses_time;
};

static struct layer_effect layer_effects[LAYER_EFFECT_MAX];
static GLuint layer_capture_texture;
static GLint layer_capture_width, layer_capture_height;
static bool layer_effects_animating;

static const char *layer_vertex_source =
	"attribute vec4 a_clip;\n"
	"attribute vec2 a_local;\n"
	"varying vec2 v_local;\n"
	"void main() { v_local = a_local; gl_Position = a_clip; }\n";

/* Declared for every effect shader; custom shaders use these helpers and
 * must not redeclare the uniforms. */
static const char *layer_prelude =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D u_scene;\n"
	"uniform vec4 u_capture;\n"   /* the copied rect, buffer px */
	"uniform vec2 u_capture_texture;\n" /* size of the texture it sits in */
	"uniform vec4 u_region;\n"    /* the buffer's bounding box on screen, buffer px */
	"uniform vec4 u_mask_rect;\n" /* mask uv origin and extent */
	"uniform float u_scale;\n"
	"uniform float u_time;\n"
	"uniform vec2 u_size;\n"      /* the region in logical px */
	"vec4 scene(vec2 pixel) {\n"
	"    vec2 p = clamp(pixel, u_capture.xy + 0.5, u_capture.xy + u_capture.zw - 0.5);\n"
	"    return texture2D(u_scene, (p - u_capture.xy) / u_capture_texture);\n"
	"}\n"
	"varying vec2 v_local;\n"
	"vec2 local() { return v_local; }\n";

static const char *layer_mask_2d =
	"uniform sampler2D u_mask;\n"
	"float mask() { return texture2D(u_mask, u_mask_rect.xy + local() * u_mask_rect.zw).a; }\n";

static const char *layer_mask_external =
	"uniform samplerExternalOES u_mask;\n"
	"float mask() { return texture2D(u_mask, u_mask_rect.xy + local() * u_mask_rect.zw).a; }\n";

/* Frosted glass: a gaussian-weighted golden-angle disc of samples, then
 * saturation and a premultiplied tint. */
static const char *layer_builtin_source =
	"uniform float u_blur;\n"
	"uniform float u_saturation;\n"
	"uniform vec4 u_tint;\n"
	"vec4 effect() {\n"
	"    vec2 p = gl_FragCoord.xy;\n"
	"    float radius = u_blur * u_scale;\n"
	"    if (radius < 0.5) return scene(p);\n"
	"    float sigma = radius * 0.5;\n"
	"    vec4 sum = vec4(0.0);\n"
	"    float total = 0.0;\n"
	"    for (int i = 0; i < 48; i++) {\n"
	"        float fi = float(i) + 0.5;\n"
	"        float r = sqrt(fi / 48.0) * radius;\n"
	"        float a = fi * 2.39996323;\n"
	"        float w = exp(-(r * r) / (2.0 * sigma * sigma));\n"
	"        sum += scene(p + vec2(cos(a), sin(a)) * r) * w;\n"
	"        total += w;\n"
	"    }\n"
	"    vec4 c = sum / total;\n"
	"    float l = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));\n"
	"    c.rgb = mix(vec3(l), c.rgb, u_saturation);\n"
	"    c.rgb = c.rgb * (1.0 - u_tint.a) + u_tint.rgb;\n"
	"    return vec4(c.rgb, 1.0);\n"
	"}\n";

static const char *layer_main =
	"void main() {\n"
	"    float m = clamp(mask() * 40.0, 0.0, 1.0);\n"
	"    vec4 c = effect();\n"
	"    gl_FragColor = vec4(c.rgb * m, m);\n"
	"}\n";

static void layer_effect_release(struct layer_effect *effect) {
	if ((effect->program[0] || effect->program[1]) && plugin_make_current()) {
		for (int i = 0; i < 2; i++)
			if (effect->program[i]) glDeleteProgram(effect->program[i]);
	}
	effect->program[0] = effect->program[1] = 0;
	effect->failed[0] = effect->failed[1] = false;
}

/* Compile the effect for a mask texture target; cached, failures too. */
static GLuint layer_effect_program(struct layer_effect *effect, bool external) {
	int v = external ? 1 : 0;
	if (effect->program[v] || effect->failed[v]) return effect->program[v];
	char *custom = NULL;
	if (effect->shader_path[0]) {
		custom = plugin_read_text_file(effect->shader_path);
		if (!custom) {
			wlr_log(WLR_ERROR, "layer effect %s: cannot read %s",
				effect->name_space, effect->shader_path);
			effect->failed[v] = true;
			return 0;
		}
	}
	const char *head = external
		? "#extension GL_OES_EGL_image_external : require\n" : "";
	const char *parts[] = {
		head, layer_prelude, external ? layer_mask_external : layer_mask_2d,
		custom ? custom : layer_builtin_source, layer_main,
	};
	size_t length = 1;
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) length += strlen(parts[i]);
	char *source = malloc(length);
	if (!source) {
		free(custom);
		effect->failed[v] = true;
		return 0;
	}
	source[0] = '\0';
	for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); i++) strcat(source, parts[i]);
	free(custom);

	GLuint vs = plugin_compile_shader(GL_VERTEX_SHADER, layer_vertex_source, "layer effect");
	GLuint fs = plugin_compile_shader(GL_FRAGMENT_SHADER, source,
		effect->shader_path[0] ? effect->shader_path : "layer effect (built-in)");
	free(source);
	GLuint program = vs && fs ? glCreateProgram() : 0;
	if (program) {
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glBindAttribLocation(program, 0, "a_clip");
		glBindAttribLocation(program, 1, "a_local");
		glLinkProgram(program);
		GLint ok = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			wlr_log(WLR_ERROR, "layer effect %s: link failed", effect->name_space);
			glDeleteProgram(program);
			program = 0;
		}
	}
	if (vs) glDeleteShader(vs);
	if (fs) glDeleteShader(fs);
	if (!program) {
		effect->failed[v] = true;
		return 0;
	}
	effect->program[v] = program;
	effect->uses_time = effect->uses_time || glGetUniformLocation(program, "u_time") >= 0;
	return program;
}

bool shady_render_set_layer_effect(struct shady_server *server, const char *name_space,
		const struct shady_layer_effect_desc *desc) {
	if (!name_space || !*name_space || strlen(name_space) >= sizeof(layer_effects[0].name_space))
		return false;
	struct layer_effect *slot = NULL, *free_slot = NULL;
	for (size_t i = 0; i < LAYER_EFFECT_MAX; i++) {
		if (layer_effects[i].used && !strcmp(layer_effects[i].name_space, name_space))
			slot = &layer_effects[i];
		else if (!layer_effects[i].used && !free_slot)
			free_slot = &layer_effects[i];
	}
	if (slot) {
		layer_effect_release(slot);
		memset(slot, 0, sizeof(*slot));
	}
	if (desc) {
		if (!slot) slot = free_slot;
		if (!slot) return false;
		if (desc->shader && strlen(desc->shader) >= sizeof(slot->shader_path)) return false;
		slot->used = true;
		snprintf(slot->name_space, sizeof(slot->name_space), "%s", name_space);
		slot->desc = *desc;
		slot->desc.shader = NULL;
		if (desc->shader) snprintf(slot->shader_path, sizeof(slot->shader_path), "%s", desc->shader);
		/* Compile now so a broken shader is reported to the caller. */
		if (plugin_make_current() && !layer_effect_program(slot, false)) {
			memset(slot, 0, sizeof(*slot));
			shady_render_schedule_all_outputs(server);
			return false;
		}
	}
	shady_render_schedule_all_outputs(server);
	return true;
}

static void layer_effects_fini(void) {
	for (size_t i = 0; i < LAYER_EFFECT_MAX; i++) layer_effect_release(&layer_effects[i]);
	memset(layer_effects, 0, sizeof(layer_effects));
	if (layer_capture_texture && plugin_make_current()) glDeleteTextures(1, &layer_capture_texture);
	layer_capture_texture = 0;
	layer_capture_width = layer_capture_height = 0;
}

static struct layer_effect *layer_effect_for(const char *name_space) {
	for (size_t i = 0; name_space && i < LAYER_EFFECT_MAX; i++)
		if (layer_effects[i].used && !strcmp(layer_effects[i].name_space, name_space))
			return &layer_effects[i];
	return NULL;
}

/* Copy rect (buffer px, top-down) of the bound framebuffer, clamped to the
 * viewport, into layer_capture_texture. */
static bool layer_capture(GLint x, GLint y, GLint w, GLint h, GLint out[4]) {
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	GLint x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
	GLint x1 = x + w > viewport[2] ? viewport[2] : x + w;
	GLint y1 = y + h > viewport[3] ? viewport[3] : y + h;
	if (x1 <= x0 || y1 <= y0) return false;
	w = x1 - x0;
	h = y1 - y0;
	if (!layer_capture_texture) glGenTextures(1, &layer_capture_texture);
	glBindTexture(GL_TEXTURE_2D, layer_capture_texture);
	if (w > layer_capture_width || h > layer_capture_height) {
		layer_capture_width = w > layer_capture_width ? w : layer_capture_width;
		layer_capture_height = h > layer_capture_height ? h : layer_capture_height;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, layer_capture_width, layer_capture_height, 0,
			GL_RGB, GL_UNSIGNED_BYTE, NULL);
	}
	glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0] + x0, viewport[1] + y0, w, h);
	/* The texture may be larger than the copy: describe the copy in its
	 * own pixels, and how much of the texture it covers. */
	out[0] = x0;
	out[1] = y0;
	out[2] = w;
	out[3] = h;
	return true;
}

/* A buffer's quad on screen: clip-space corners (top-left, top-right,
 * bottom-left, bottom-right; w > 1 when receding) and the screen box they
 * cover, in buffer px top-down. */
struct layer_quad {
	GLfloat clip[16];
	GLint box[4];
};

static void layer_quad_finish_box(struct layer_quad *q, const double xs[4], const double ys[4]) {
	double x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
	for (int i = 1; i < 4; i++) {
		if (xs[i] < x0) x0 = xs[i];
		if (xs[i] > x1) x1 = xs[i];
		if (ys[i] < y0) y0 = ys[i];
		if (ys[i] > y1) y1 = ys[i];
	}
	q->box[0] = (GLint)floor(x0);
	q->box[1] = (GLint)floor(y0);
	q->box[2] = (GLint)ceil(x1) - q->box[0];
	q->box[3] = (GLint)ceil(y1) - q->box[1];
}

/* An untransformed buffer at dst (buffer px) in a viewport of vw x vh. */
static void layer_quad_from_rect(struct layer_quad *q, const struct wlr_box *dst, int vw, int vh) {
	const double xs[4] = { dst->x, dst->x + dst->width, dst->x, dst->x + dst->width };
	const double ys[4] = { dst->y, dst->y, dst->y + dst->height, dst->y + dst->height };
	for (int i = 0; i < 4; i++) {
		q->clip[i * 4 + 0] = (GLfloat)(2.0 * xs[i] / vw - 1.0);
		q->clip[i * 4 + 1] = (GLfloat)(2.0 * ys[i] / vh - 1.0);
		q->clip[i * 4 + 2] = 0.f;
		q->clip[i * 4 + 3] = 1.f;
	}
	layer_quad_finish_box(q, xs, ys);
}

/* Draw a buffer's backdrop over `quad` (logical size w x h), masked by the
 * buffer's alpha: capture the screen under the quad's box plus the blur
 * margin, then run the effect over the quad itself. */
static void draw_layer_backdrop(struct layer_effect *effect, struct wlr_texture *texture,
		const struct wlr_fbox *src_box, const struct layer_quad *quad,
		double logical_w, double logical_h, float scale, float time) {
	struct wlr_gles2_texture_attribs attribs;
	wlr_gles2_texture_get_attribs(texture, &attribs);
	bool external = attribs.target != GL_TEXTURE_2D;
	GLuint program = layer_effect_program(effect, external);
	if (!program) return;

	GLint old_program = 0, old_buffer = 0, old_active = 0, old_tex0 = 0, old_tex1 = 0;
	GLint blend_src_rgb = 0, blend_dst_rgb = 0, blend_src_alpha = 0, blend_dst_alpha = 0;
	GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex0);
	glActiveTexture(GL_TEXTURE1);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_tex1);

	/* Blur taps reach past the quad; capture a margin around it. */
	GLint margin = (GLint)ceilf(effect->desc.blur * scale) + 2;
	GLint capture[4];
	const GLint *box = quad->box;
	glActiveTexture(GL_TEXTURE0);
	if (!layer_capture(box[0] - margin, box[1] - margin, box[2] + 2 * margin,
			box[3] + 2 * margin, capture))
		goto restore;

	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "u_scene"), 0);
	glUniform4f(glGetUniformLocation(program, "u_capture"), (float)capture[0], (float)capture[1],
		(float)capture[2], (float)capture[3]);
	glUniform2f(glGetUniformLocation(program, "u_capture_texture"),
		(float)layer_capture_width, (float)layer_capture_height);
	glUniform4f(glGetUniformLocation(program, "u_region"), (float)box[0], (float)box[1],
		(float)box[2], (float)box[3]);
	float tex_w = (float)texture->width, tex_h = (float)texture->height;
	bool has_src = src_box && src_box->width > 0 && src_box->height > 0 && tex_w > 0 && tex_h > 0;
	glUniform4f(glGetUniformLocation(program, "u_mask_rect"),
		has_src ? (float)src_box->x / tex_w : 0.f, has_src ? (float)src_box->y / tex_h : 0.f,
		has_src ? (float)src_box->width / tex_w : 1.f, has_src ? (float)src_box->height / tex_h : 1.f);
	glUniform1f(glGetUniformLocation(program, "u_scale"), scale);
	glUniform1f(glGetUniformLocation(program, "u_time"), time);
	glUniform2f(glGetUniformLocation(program, "u_size"), (float)logical_w, (float)logical_h);
	glUniform1f(glGetUniformLocation(program, "u_blur"), effect->desc.blur);
	glUniform1f(glGetUniformLocation(program, "u_saturation"), effect->desc.saturation);
	glUniform4fv(glGetUniformLocation(program, "u_tint"), 1, effect->desc.tint);
	for (size_t i = 0; i < effect->desc.uniform_count; i++) {
		const struct shady_layer_effect_uniform *u = &effect->desc.uniforms[i];
		GLint loc = glGetUniformLocation(program, u->name);
		if (loc < 0) continue;
		if (u->size == 1) glUniform1fv(loc, 1, u->value);
		else if (u->size == 2) glUniform2fv(loc, 1, u->value);
		else if (u->size == 3) glUniform3fv(loc, 1, u->value);
		else glUniform4fv(loc, 1, u->value);
	}
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(attribs.target, attribs.tex);
	glUniform1i(glGetUniformLocation(program, "u_mask"), 1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, layer_capture_texture);

	static const GLfloat locals[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, quad->clip);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, locals);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	if (effect->uses_time) layer_effects_animating = true;

restore:
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex1);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_tex0);
	glActiveTexture((GLenum)old_active);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buffer);
	glUseProgram((GLuint)old_program);
	if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
	glBlendFuncSeparate((GLenum)blend_src_rgb, (GLenum)blend_dst_rgb,
		(GLenum)blend_src_alpha, (GLenum)blend_dst_alpha);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

/* ---- layer-shell 3D placement ----------------------------------------- */

/*
 * A transformed layer surface lies on a plane through a pivot point,
 * rotated by roll (z), tilt (x) and yaw (y) and pushed `depth` away, and is
 * seen through a perspective whose vanishing point is the output centre:
 *
 *   screen = O + (P.xy - O) * f / (f + P.z)
 *
 * All of this is in the output's logical pixels, attached to the screen
 * rather than to the 3D camera, so a tilted bar stays put while the world
 * moves. Drawing uses homogeneous clip coordinates (w = (f + z) / f) for
 * perspective-correct texturing; input inverts the same mapping.
 */

#define LAYER_TRANSFORM_MAX 16

struct layer_transform {
	bool used;
	char name_space[64];
	struct shady_layer_transform_desc desc;
};

struct layer_plane {
	double ox, oy; /* vanishing point */
	double f;
	double px, py, pz; /* pivot */
	double a[3], b[3]; /* the plane's x and y axes */
};

static struct layer_transform layer_transforms[LAYER_TRANSFORM_MAX];
static GLuint layer_transform_program[2];
static bool layer_transform_failed[2];

static struct layer_transform *layer_transform_for(const char *name_space) {
	for (size_t i = 0; name_space && i < LAYER_TRANSFORM_MAX; i++)
		if (layer_transforms[i].used && !strcmp(layer_transforms[i].name_space, name_space))
			return &layer_transforms[i];
	return NULL;
}

bool shady_render_set_layer_transform(struct shady_server *server, const char *name_space,
		const struct shady_layer_transform_desc *desc) {
	if (!name_space || !*name_space || strlen(name_space) >= sizeof(layer_transforms[0].name_space))
		return false;
	struct layer_transform *slot = layer_transform_for(name_space);
	if (!desc) {
		if (slot) memset(slot, 0, sizeof(*slot));
		shady_render_schedule_all_outputs(server);
		return true;
	}
	for (size_t i = 0; !slot && i < LAYER_TRANSFORM_MAX; i++)
		if (!layer_transforms[i].used) slot = &layer_transforms[i];
	if (!slot) return false;
	slot->used = true;
	snprintf(slot->name_space, sizeof(slot->name_space), "%s", name_space);
	slot->desc = *desc;
	shady_render_schedule_all_outputs(server);
	return true;
}

/* box: the layer surface in output logical px; out_w/out_h: output size. */
static void layer_plane_setup(struct layer_plane *pl, const struct shady_layer_transform_desc *t,
		const struct wlr_box *box, uint32_t anchor, double out_w, double out_h) {
	pl->ox = out_w / 2.0;
	pl->oy = out_h / 2.0;
	pl->f = t->perspective > 1.f ? t->perspective : 1.2 * out_h;
	pl->px = box->x + box->width / 2.0;
	pl->py = box->y + box->height / 2.0;
	pl->pz = t->depth;
	if (!t->pivot_center) {
		bool top = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
		bool bottom = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
		bool left = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
		bool right = anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		/* Hinge on the edge the surface is attached to. */
		if (top && !bottom) pl->py = box->y;
		else if (bottom && !top) pl->py = box->y + box->height;
		else if (left && !right) pl->px = box->x;
		else if (right && !left) pl->px = box->x + box->width;
	}
	const double radians = 3.14159265358979323846 / 180.0;
	double r = t->roll * radians, x = t->tilt * radians, y = t->yaw * radians;
	/* Rotate the unit axes: roll, then tilt, then yaw. */
	const double units[2][3] = { { 1, 0, 0 }, { 0, 1, 0 } };
	for (int i = 0; i < 2; i++) {
		double vx = units[i][0] * cos(r) - units[i][1] * sin(r);
		double vy = units[i][0] * sin(r) + units[i][1] * cos(r);
		double vz = 0;
		double ty = vy * cos(x) - vz * sin(x), tz = vy * sin(x) + vz * cos(x);
		vy = ty;
		vz = tz;
		double yx = vx * cos(y) + vz * sin(y), yz = -vx * sin(y) + vz * cos(y);
		double *out = i == 0 ? pl->a : pl->b;
		out[0] = yx;
		out[1] = vy;
		out[2] = yz;
	}
}

/* Project the plane point at output position (x, y) (untransformed). */
static bool layer_plane_project(const struct layer_plane *pl, double x, double y,
		double *sx, double *sy, double *w) {
	double u = x - pl->px, v = y - pl->py;
	double X = pl->px + u * pl->a[0] + v * pl->b[0];
	double Y = pl->py + u * pl->a[1] + v * pl->b[1];
	double Z = pl->pz + u * pl->a[2] + v * pl->b[2];
	double denom = pl->f + Z;
	if (denom < pl->f * 0.05) return false; /* at or behind the eye */
	*w = denom / pl->f;
	*sx = pl->ox + (X - pl->ox) / *w;
	*sy = pl->oy + (Y - pl->oy) / *w;
	return true;
}

/* The untransformed output position that projects to screen (sx, sy). */
static bool layer_plane_unproject(const struct layer_plane *pl, double sx, double sy,
		double *x, double *y) {
	double dx = sx - pl->ox, dy = sy - pl->oy, f = pl->f;
	double m00 = f * pl->a[0] - dx * pl->a[2], m01 = f * pl->b[0] - dx * pl->b[2];
	double m10 = f * pl->a[1] - dy * pl->a[2], m11 = f * pl->b[1] - dy * pl->b[2];
	double r0 = dx * (f + pl->pz) - f * (pl->px - pl->ox);
	double r1 = dy * (f + pl->pz) - f * (pl->py - pl->oy);
	double det = m00 * m11 - m01 * m10;
	if (fabs(det) < 1e-9) return false;
	double u = (r0 * m11 - m01 * r1) / det;
	double v = (m00 * r1 - r0 * m10) / det;
	if (pl->f + pl->pz + u * pl->a[2] + v * pl->b[2] < pl->f * 0.05) return false;
	*x = pl->px + u;
	*y = pl->py + v;
	return true;
}

/* A buffer at (x, y, w, h) in output logical px, projected onto the plane,
 * as a quad in a viewport of vw x vh buffer px. */
static bool layer_quad_from_plane(struct layer_quad *q, const struct layer_plane *pl,
		double x, double y, double w, double h, float scale, int vw, int vh) {
	const double corners[4][2] = { { x, y }, { x + w, y }, { x, y + h }, { x + w, y + h } };
	double xs[4], ys[4];
	for (int i = 0; i < 4; i++) {
		double sx, sy, cw;
		if (!layer_plane_project(pl, corners[i][0], corners[i][1], &sx, &sy, &cw)) return false;
		xs[i] = sx * scale;
		ys[i] = sy * scale;
		/* Buffer px, top-down, to NDC, scaled by w for perspective. */
		q->clip[i * 4 + 0] = (GLfloat)((2.0 * xs[i] / vw - 1.0) * cw);
		q->clip[i * 4 + 1] = (GLfloat)((2.0 * ys[i] / vh - 1.0) * cw);
		q->clip[i * 4 + 2] = 0.f;
		q->clip[i * 4 + 3] = (GLfloat)cw;
	}
	layer_quad_finish_box(q, xs, ys);
	return true;
}

static GLuint layer_transform_get_program(bool external) {
	int v = external ? 1 : 0;
	if (layer_transform_program[v] || layer_transform_failed[v]) return layer_transform_program[v];
	static const char *vs_src =
		"attribute vec4 a_clip;\n"
		"attribute vec2 a_uv;\n"
		"varying vec2 v_uv;\n"
		"void main() { v_uv = a_uv; gl_Position = a_clip; }\n";
	char fs_src[512];
	snprintf(fs_src, sizeof(fs_src),
		"%s"
		"precision mediump float;\n"
		"uniform %s u_tex;\n"
		"uniform float u_alpha;\n"
		"varying vec2 v_uv;\n"
		"void main() { gl_FragColor = texture2D(u_tex, v_uv) * u_alpha; }\n",
		external ? "#extension GL_OES_EGL_image_external : require\n" : "",
		external ? "samplerExternalOES" : "sampler2D");
	GLuint vs = plugin_compile_shader(GL_VERTEX_SHADER, vs_src, "layer transform");
	GLuint fs = plugin_compile_shader(GL_FRAGMENT_SHADER, fs_src, "layer transform");
	GLuint program = vs && fs ? glCreateProgram() : 0;
	if (program) {
		glAttachShader(program, vs);
		glAttachShader(program, fs);
		glBindAttribLocation(program, 0, "a_clip");
		glBindAttribLocation(program, 1, "a_uv");
		glLinkProgram(program);
		GLint ok = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &ok);
		if (!ok) {
			glDeleteProgram(program);
			program = 0;
		}
	}
	if (vs) glDeleteShader(vs);
	if (fs) glDeleteShader(fs);
	layer_transform_program[v] = program;
	layer_transform_failed[v] = !program;
	return program;
}

static void layer_transforms_fini(void) {
	if (plugin_make_current())
		for (int i = 0; i < 2; i++)
			if (layer_transform_program[i]) glDeleteProgram(layer_transform_program[i]);
	memset(layer_transform_program, 0, sizeof(layer_transform_program));
	memset(layer_transform_failed, 0, sizeof(layer_transform_failed));
	memset(layer_transforms, 0, sizeof(layer_transforms));
}

/* Draw one buffer (dst in output logical px) on the plane. */
static void draw_transformed_buffer(const struct layer_plane *pl, struct wlr_texture *texture,
		const struct wlr_fbox *src, double dx, double dy, double dw, double dh,
		float scale, float alpha) {
	struct wlr_gles2_texture_attribs attribs;
	wlr_gles2_texture_get_attribs(texture, &attribs);
	GLuint program = layer_transform_get_program(attribs.target != GL_TEXTURE_2D);
	if (!program) return;
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	if (viewport[2] <= 0 || viewport[3] <= 0) return;

	double tw = texture->width, th = texture->height;
	bool has_src = src && src->width > 0 && src->height > 0 && tw > 0 && th > 0;
	float u0 = has_src ? (float)(src->x / tw) : 0.f, v0 = has_src ? (float)(src->y / th) : 0.f;
	float u1 = has_src ? (float)((src->x + src->width) / tw) : 1.f;
	float v1 = has_src ? (float)((src->y + src->height) / th) : 1.f;
	struct layer_quad quad;
	if (!layer_quad_from_plane(&quad, pl, dx, dy, dw, dh, scale, viewport[2], viewport[3])) return;
	const GLfloat uv[8] = { u0, v0, u1, v0, u0, v1, u1, v1 };

	GLint old_program = 0, old_buffer = 0, old_active = 0, old_texture = 0;
	GLint blend_src_rgb = 0, blend_dst_rgb = 0, blend_src_alpha = 0, blend_dst_alpha = 0;
	GLboolean blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
	GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
	glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &old_buffer);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active);
	glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
	glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
	glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_alpha);
	glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_alpha);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);

	glUseProgram(program);
	glBindTexture(attribs.target, attribs.tex);
	glTexParameteri(attribs.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(attribs.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glUniform1i(glGetUniformLocation(program, "u_tex"), 0);
	glUniform1f(glGetUniformLocation(program, "u_alpha"), alpha);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, quad.clip);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uv);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);

	glBindTexture(attribs.target, 0);
	glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
	glActiveTexture((GLenum)old_active);
	glBindBuffer(GL_ARRAY_BUFFER, (GLuint)old_buffer);
	glUseProgram((GLuint)old_program);
	glBlendFuncSeparate((GLenum)blend_src_rgb, (GLenum)blend_dst_rgb,
		(GLenum)blend_src_alpha, (GLenum)blend_dst_alpha);
	if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
	if (scissor) glEnable(GL_SCISSOR_TEST);
	if (depth) glEnable(GL_DEPTH_TEST);
}

/* The layer surface's plane on its output, from layout geometry. */
static bool layer_plane_for(struct shady_server *server, struct shady_layer_surface *layer,
		const struct layer_transform *t, struct wlr_output *output, struct layer_plane *pl) {
	struct wlr_layer_surface_v1 *ls = layer->layer_surface;
	if (!ls->surface->mapped || !output) return false;
	struct wlr_box out_box;
	wlr_output_layout_get_box(server->output_layout, output, &out_box);
	if (out_box.width <= 0 || out_box.height <= 0) return false;
	int lx = 0, ly = 0;
	wlr_scene_node_coords(&layer->scene_layer->tree->node, &lx, &ly);
	const struct wlr_box box = {
		.x = lx - out_box.x, .y = ly - out_box.y,
		.width = ls->surface->current.width, .height = ls->surface->current.height,
	};
	layer_plane_setup(pl, &t->desc, &box, ls->current.anchor, out_box.width, out_box.height);
	return true;
}

int shady_render_layer_pick(struct shady_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop || !server->config.spatial_mode || desktop->session_locked) return -1;
	/* The reverse of the drawing order: overlay down to background, and
	 * newest first within a layer (the list's head). */
	for (int z = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; z >= ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; z--) {
		struct shady_layer_surface *layer;
		wl_list_for_each(layer, &desktop->layer_surfaces, link) {
			struct wlr_layer_surface_v1 *ls = layer->layer_surface;
			if ((int)ls->current.layer != z || !ls->surface->mapped || !ls->output) continue;
			int nx = 0, ny = 0;
			wlr_scene_node_coords(&layer->scene_layer->tree->node, &nx, &ny);
			double x = lx - nx, y = ly - ny;
			struct layer_transform *t = layer_transform_for(ls->namespace);
			if (t) {
				struct layer_plane pl;
				struct wlr_box out_box;
				wlr_output_layout_get_box(server->output_layout, ls->output, &out_box);
				double px, py;
				if (!layer_plane_for(server, layer, t, ls->output, &pl) ||
						!layer_plane_unproject(&pl, lx - out_box.x, ly - out_box.y, &px, &py))
					continue;
				x = px - (nx - out_box.x);
				y = py - (ny - out_box.y);
			}
			/* Subsurfaces only: popups are hit-tested by the caller, as
			 * they are drawn above every layer and never transformed. */
			struct wlr_surface *hit = wlr_surface_surface_at(ls->surface, x, y, sx, sy);
			if (hit) {
				*surface = hit;
				return 1;
			}
		}
	}
	return 0;
}

bool shady_render_layer_is_transformed(struct shady_server *server, struct wlr_surface *surface) {
	/* Only the spatial renderer draws the transform. */
	if (!surface || !server->config.spatial_mode || shady_desktop_state(server)->session_locked)
		return false;
	struct wlr_layer_surface_v1 *ls =
		wlr_layer_surface_v1_try_from_wlr_surface(wlr_surface_get_root_surface(surface));
	return ls && layer_transform_for(ls->namespace) != NULL;
}

struct shady_overlay_render_data {
	struct wlr_render_pass *pass;
	struct wlr_output *output;
	double ox, oy;
	float scale;
	struct timespec now;
	struct layer_effect *effect; /* backdrop for the layer being drawn */
	const struct layer_plane *plane; /* 3D placement for the layer being drawn */
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

	float seconds = (float)(ctx->now.tv_sec % 3600) + (float)ctx->now.tv_nsec * 1e-9f;
	GLint viewport[4];
	if (ctx->plane) {
		if (plugin_make_current()) {
			glGetIntegerv(GL_VIEWPORT, viewport);
			struct layer_quad quad;
			if (ctx->effect && viewport[2] > 0 && viewport[3] > 0 &&
					layer_quad_from_plane(&quad, ctx->plane, (double)sx - ctx->ox,
						(double)sy - ctx->oy, width, height, ctx->scale, viewport[2], viewport[3]))
				draw_layer_backdrop(ctx->effect, texture, &buffer->src_box, &quad,
					width, height, ctx->scale, seconds);
			draw_transformed_buffer(ctx->plane, texture, &buffer->src_box,
				(double)sx - ctx->ox, (double)sy - ctx->oy, width, height,
				ctx->scale, buffer->opacity);
		}
		wlr_scene_surface_send_frame_done(scene_surface, &ctx->now);
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
	if (ctx->effect && plugin_make_current()) {
		glGetIntegerv(GL_VIEWPORT, viewport);
		if (viewport[2] > 0 && viewport[3] > 0) {
			struct layer_quad quad;
			layer_quad_from_rect(&quad, &options.dst_box, viewport[2], viewport[3]);
			draw_layer_backdrop(ctx->effect, texture, &buffer->src_box, &quad,
				width, height, ctx->scale, seconds);
		}
	}
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
	float wobble_x = ctx->toplevel->motion.wobble_x;
	float wobble_y = ctx->toplevel->motion.wobble_y;
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
			ctx->toplevel->motion.tilt_x,
			ctx->toplevel->motion.tilt_y);
		shady_mat4_multiply(mvp, ctx->vp, model);
	}
	struct shady_server *server = ctx->toplevel->server;
	bool focused = !wl_list_empty(&server->toplevels) &&
		server->toplevels.next == &ctx->toplevel->link;
	const float tint[4] = {
		server->config.window_tint[0] * (focused ? 0.92f : 1.0f),
		server->config.window_tint[1] * (focused ? 1.06f : 1.0f),
		server->config.window_tint[2] * (focused ? 1.16f : 1.0f),
		server->config.window_opacity,
	};

	float water[4] = {
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_amplitude,
		ctx->toplevel->water_frequency,
		ctx->toplevel->water_speed,
		ctx->toplevel->water_phase,
	};
	float water_surface[4] = {
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_fresnel,
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_specular,
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_caustic,
		ctx->toplevel->fullscreen ? 0.f : ctx->toplevel->water_tint,
	};
	const float no_border_color[4] = {0.f, 0.f, 0.f, 0.f};
	const float no_border_width[2] = {0.f, 0.f};
	const struct shady_close_animation_state *close_state =
		shady_close_state_for_const(ctx->toplevel);
	const float close_effect[4] = {
		(float)close_state->style,
		close_state->strength > 0.f ? close_state->strength : 1.f,
		close_state->direction_x,
		close_state->direction_y,
	};
	const float full_frame_rect[4] = {0.f, 0.f, 1.f, 1.f};
	shady_gl_pipeline_draw_window(&pipeline,
		attribs.target, attribs.tex, attribs.has_alpha,
		mvp, model, full_frame_rect, ctx->time_seconds,
		wobble_x,
		wobble_y,
		water,
		water_surface,
		no_border_color,
		no_border_width,
		shady_close_state_for_const(ctx->toplevel)->progress,
		close_effect,
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

	/* Layer-shell stays in normal 2D screen space in spatial mode. Draw it
	 * bottom to top: by layer (background .. overlay), and within a layer
	 * oldest first, as the list keeps the newest surface at its head. */
	struct shady_layer_surface *layer;
	for (uint32_t z = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
			z <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; z++)
	wl_list_for_each_reverse(layer, &shady_desktop_state(server)->layer_surfaces, link) {
		if ((uint32_t)layer->layer_surface->current.layer != z) continue;
		if (layer->layer_surface->output &&
				layer->layer_surface->output != output) {
			continue;
		}
		struct layer_transform *transform = layer_transform_for(layer->layer_surface->namespace);
		struct layer_plane plane;
		ctx.plane = transform && layer_plane_for(server, layer, transform, output, &plane)
			? &plane : NULL;
		ctx.effect = layer_effect_for(layer->layer_surface->namespace);
		wlr_scene_node_for_each_buffer(&layer->scene_layer->tree->node,
			render_scene_overlay_buffer, &ctx);
		ctx.effect = NULL;
		ctx.plane = NULL;
	}

	/* XDG popups remain readable/interactive while their parent is spatial. */
	struct shady_popup *popup;
	wl_list_for_each(popup, &server->popups, link) {
		wlr_scene_node_for_each_buffer(&popup->scene_tree->node,
			render_scene_overlay_buffer, &ctx);
	}
	/* Animated effects (u_time) need the next frame too. */
	if (layer_effects_animating) {
		layer_effects_animating = false;
		shady_render_schedule_all_outputs(server);
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
			return;
		}
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		wlr_scene_output_send_frame_done(scene_output, &now);
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
	float time_seconds = shader_time_seconds();

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
	plugin_render_hooks_run(server, SHADY_RENDER_STAGE_AFTER_BACKGROUND,
		output, buf_w, buf_h, logical_w, logical_h, time_seconds);

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

	shady_environment_sync(server);
	shady_environment_draw_sky(server);

	/*
	 * Draw a world-space reference plane before windows. Because it shares
	 * the depth buffer and camera VP matrix, orbiting immediately reveals
	 * perspective and per-window Z separation.
	 */
	shady_scene_effects_draw_floor(server, &pipeline, vp);
	shady_environment_draw_scene(server, vp);

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
	plugin_render_hooks_run(server, SHADY_RENDER_STAGE_BEFORE_WINDOWS,
		output, buf_w, buf_h, logical_w, logical_h, time_seconds);
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
					snapshot->tilt_x = toplevel->motion.tilt_x;
					snapshot->tilt_y = toplevel->motion.tilt_y;
					snapshot->z = shady_spatial_toplevel_state(toplevel)->z;
					snapshot->wobble_x = toplevel->motion.wobble_x;
					snapshot->wobble_y = toplevel->motion.wobble_y;
					snapshot->has_alpha = attribs.has_alpha;
					const struct shady_close_animation_state *close_state =
						shady_close_state_for_const(toplevel);
					snapshot->close_style = close_state->style;
					snapshot->close_duration = close_state->duration;
					snapshot->close_strength = close_state->strength;
					snapshot->close_direction_x = close_state->direction_x;
					snapshot->close_direction_y = close_state->direction_y;
					snapshot->plugin_shader_program = toplevel->plugin_shader_program;
					snapshot->plugin_shader_owner = toplevel->plugin_shader_owner;
					memcpy(snapshot->plugin_shader_params, toplevel->plugin_shader_params,
						sizeof(snapshot->plugin_shader_params));
					snapshot->dirty = false;
				}
			}
		}

		bool focused = !wl_list_empty(&server->toplevels) &&
			server->toplevels.next == &toplevel->link;
		float center_x = (layout_x + tw * .5f - logical_w * .5f) / logical_h;
		float center_y = .5f - (layout_y + th * .5f) / logical_h;
		struct shady_representation_context representation_context = {
			.struct_size = sizeof(representation_context),
			.logical_width = logical_w,
			.logical_height = logical_h,
			.window_width = tw,
			.window_height = th,
			.center_x = center_x,
			.center_y = center_y,
			.center_z = shady_spatial_toplevel_state(toplevel)->z,
			.tilt_x = toplevel->motion.tilt_x,
			.tilt_y = toplevel->motion.tilt_y,
			.first_person = shady_spatial_state(server)->runtime.camera.first_person,
			.folded = shady_spatial_state(server)->runtime.camera.first_person &&
				!shady_fps_toplevel_state_const(toplevel)->expanded,
			.held = shady_fps_is_holding(server, toplevel),
			.focused = focused,
		};
		struct shady_window_representation representation_base = {0};
		bool has_representation_base = shady_toplevel_representation_base(
			toplevel, &representation_base);
		struct shady_representation_model representation_model;
		bool folded_representation = representation_context.folded &&
			has_representation_base &&
			shady_toplevel_representation_model(toplevel,
				&representation_context, &representation_model);
		struct shady_representation_mesh representation_mesh = {0};
		bool mesh_representation = folded_representation &&
			representation_base.kind == SHADY_WINDOW_REPRESENTATION_MESH &&
			shady_toplevel_representation_mesh(toplevel,
				&representation_context, &representation_mesh);
		bool frame_has_titlebar = !toplevel->fullscreen &&
			!(folded_representation && representation_model.hide_titlebar) &&
			server->config.window_titlebar && toplevel->titlebar_texture &&
			wlr_texture_is_gles2(toplevel->titlebar_texture) &&
			toplevel->titlebar_height > 0;
		float frame_title_h = frame_has_titlebar ? (float)toplevel->titlebar_height : 0.f;
		float frame_x = layout_x;
		float frame_y = layout_y - frame_title_h;
		float frame_w = tw;
		float frame_h = th + frame_title_h;
		if (frame_h <= 0.f) frame_h = th;
		const float client_frame_rect[4] = {
			0.f,
			0.f,
			1.f,
			frame_h > 0.f ? th / frame_h : 1.f,
		};

		bool screen_space = !shady_spatial_state(server)->runtime.camera.first_person &&
			(toplevel->fullscreen || toplevel->maximized);
		float wobble_x = toplevel->motion.wobble_x;
		float wobble_y = toplevel->motion.wobble_y;
		if (screen_space) {
			shady_mat4_identity(model);
			shady_screen_space_model(mvp, frame_x, frame_y, frame_w, frame_h,
				logical_w, logical_h);
			wobble_x = 0.f;
			wobble_y = 0.f;
		} else {
			if (folded_representation) {
				shady_window_box_model(model,
					representation_model.center_x, representation_model.center_y,
					representation_model.center_z,
					representation_model.width, representation_model.height,
					representation_model.depth,
					representation_model.tilt_x, representation_model.tilt_y);
			}else{
				shady_window_model(
				model,
				frame_x,
				frame_y,
				frame_w,
				frame_h,
				logical_w,
				logical_h,
				shady_spatial_toplevel_state(toplevel)->z,
				toplevel->motion.tilt_x,
				toplevel->motion.tilt_y
			);
			}
			shady_mat4_multiply(mvp, vp, model);
		}

		float border_px = 0.f, border_color[4];
		shady_toplevel_get_border(toplevel, &border_px, border_color);
		/* Free-floating decorated windows get one rounded frame shared by the
		 * side walls, client and title bar. Screen-space (maximized or
		 * fullscreen) and folded representations keep their square edges. */
		bool rounded_frame = !screen_space && !folded_representation;
		shady_gl_pipeline_set_frame_style(&pipeline,
			rounded_frame ? frame_w : 0.f, rounded_frame ? frame_h : 0.f,
			server->config.window_corner_radius, border_px, border_color);

		if (!screen_space && !mesh_representation) {
			shady_scene_effects_draw_sides(server, &pipeline, mvp, model,
				wobble_x, wobble_y, shady_close_state_for_const(toplevel)->progress);
		}

		if (screen_space) glDisable(GL_DEPTH_TEST);
		float focused_tint[4] = {
			window_tint[0] * (focused ? 0.92f : 1.0f),
			window_tint[1] * (focused ? 1.06f : 1.0f),
			window_tint[2] * (focused ? 1.16f : 1.0f),
			server->config.window_opacity,
		};
		float water[4] = {
			toplevel->fullscreen ? 0.f : toplevel->water_amplitude,
			toplevel->water_frequency,
			toplevel->water_speed,
			toplevel->water_phase,
		};
		float water_surface[4] = {
			toplevel->fullscreen ? 0.f : toplevel->water_fresnel,
			toplevel->fullscreen ? 0.f : toplevel->water_specular,
			toplevel->fullscreen ? 0.f : toplevel->water_caustic,
			toplevel->fullscreen ? 0.f : toplevel->water_tint,
		};
		float border_width[2] = {
			tw > 0.f ? border_px / tw : 0.f,
			th > 0.f ? border_px / th : 0.f,
		};
		const struct shady_close_animation_state *close_state =
			shady_close_state_for_const(toplevel);
		const float close_effect[4] = {
			(float)close_state->style,
			close_state->strength > 0.f ? close_state->strength : 1.f,
			close_state->direction_x,
			close_state->direction_y,
		};
		bool custom_drawn = false;
		GLenum portal_target = 0;
		GLuint portal_texture = 0;
		int portal_width = 0, portal_height = 0;
		plugin_shader_source_texture(server, toplevel,
			&portal_target, &portal_texture, &portal_width, &portal_height);
		if (toplevel->plugin_shader_program && toplevel->plugin_shader_owner) {
			custom_drawn = plugin_shader_draw_window(
				server, toplevel->plugin_shader_program, toplevel->plugin_shader_owner,
				attribs.target, attribs.tex,
				texture->width, texture->height, attribs.has_alpha,
				portal_target, portal_texture, portal_width, portal_height,
				mvp, model, client_frame_rect, time_seconds,
				(float)buf_w, (float)buf_h, tw, th, wobble_x, wobble_y,
				water, water_surface, border_color, border_width,
				shady_close_state_for_const(toplevel)->progress, close_effect,
				focused_tint, server->config.window_effect_strength,
				server->config.window_brightness * (focused ? 1.08f : 1.0f),
				mesh_representation ? (const float *)representation_mesh.vertices : NULL,
				mesh_representation ? representation_mesh.vertex_count : 0,
				mesh_representation ? representation_mesh.indices : NULL,
				mesh_representation ? representation_mesh.index_count : 0,
				toplevel->plugin_shader_params);
		}
		if (!custom_drawn && mesh_representation) {
			shady_gl_pipeline_draw_window_mesh(
				&pipeline, attribs.target, attribs.tex, attribs.has_alpha,
				mvp, model, client_frame_rect, time_seconds,
				wobble_x, wobble_y, water, water_surface,
				border_color, border_width,
				shady_close_state_for_const(toplevel)->progress, close_effect,
				focused_tint, server->config.window_effect_strength,
				server->config.window_brightness * (focused ? 1.08f : 1.0f),
				(const float *)representation_mesh.vertices,
				representation_mesh.vertex_count,
				representation_mesh.indices,
				representation_mesh.index_count);
			custom_drawn = true;
		}
		if (!custom_drawn) {
			shady_gl_pipeline_draw_window(
				&pipeline,
				attribs.target,
				attribs.tex,
				attribs.has_alpha,
				mvp,
				model,
				client_frame_rect,
				time_seconds,
				wobble_x,
				wobble_y,
				water,
				water_surface,
				border_color,
				border_width,
				shady_close_state_for_const(toplevel)->progress,
				close_effect,
				focused_tint,
				server->config.window_effect_strength,
				server->config.window_brightness * (focused ? 1.08f : 1.0f)
			);
		}

		if (frame_has_titlebar) {
			struct wlr_gles2_texture_attribs title_attribs;
			wlr_gles2_texture_get_attribs(toplevel->titlebar_texture, &title_attribs);
			if (title_attribs.target == GL_TEXTURE_2D) {
				float client_fraction = frame_h > 0.f ? th / frame_h : 1.f;
				const float title_frame_rect[4] = {
					0.f,
					client_fraction,
					1.f,
					1.f - client_fraction,
				};
				/* The legacy client border stays off for the title bar, but the
				 * rounded-frame path still needs the colour so its outline runs
				 * continuously around the title bar as well. */
				const float *title_border_color = border_color;
				const float no_border_width[2] = {0.f, 0.f};
				const float title_tint[4] = {
					1.f, 1.f, 1.f, server->config.window_opacity
				};

				/*
				 * The title bar is now a sub-rect of the same full-frame model
				 * as the client surface. Both therefore share the same pivot,
				 * tilt, wobble/water deformation and close animation instead of
				 * behaving like two independent quads.
				 */
				bool custom_title_drawn = false;
				if (toplevel->plugin_shader_program && toplevel->plugin_shader_owner) {
					custom_title_drawn = plugin_shader_draw_window(
						server, toplevel->plugin_shader_program,
						toplevel->plugin_shader_owner,
						GL_TEXTURE_2D, title_attribs.tex,
						toplevel->titlebar_width, toplevel->titlebar_height, true,
						0, 0, 0, 0,
						mvp, model, title_frame_rect, time_seconds,
						(float)buf_w, (float)buf_h,
						tw, frame_title_h, wobble_x, wobble_y,
						water, water_surface,
						title_border_color, no_border_width,
						close_state->progress, close_effect, title_tint,
						0.f, 1.f, NULL, 0, NULL, 0,
						toplevel->plugin_shader_params);
				}
				if (!custom_title_drawn) {
					shady_gl_pipeline_draw_window(
						&pipeline,
						GL_TEXTURE_2D,
						title_attribs.tex,
						true,
						mvp,
						model,
						title_frame_rect,
						time_seconds,
						wobble_x,
						wobble_y,
						water,
						water_surface,
						title_border_color,
						no_border_width,
						close_state->progress,
						close_effect,
						title_tint,
						0.f,
						1.f
					);
				}
			}
		}

		/* Subsurfaces are rectangles inside the client; never clip them to
		 * the parent's frame shape. */
		shady_gl_pipeline_set_frame_style(&pipeline, 0.f, 0.f, 0.f, 0.f, NULL);

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
	shady_gl_pipeline_set_frame_style(&pipeline, 0.f, 0.f, 0.f, 0.f, NULL);

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

		/* Keep the closing window's silhouette rounded so the close animation
		 * does not snap back to square corners on its first frame. */
		shady_gl_pipeline_set_frame_style(&pipeline, snapshot->width,
			snapshot->height, server->config.window_corner_radius, 0.f, NULL);
		shady_scene_effects_draw_sides(server, &pipeline, mvp, model,
			snapshot->wobble_x, snapshot->wobble_y, snapshot->progress);

		const float no_water[4] = {0.f, 1.f, 0.f, 0.f};
		const float no_water_surface[4] = {0.f, 0.f, 0.f, 0.f};
		const float no_border_color[4] = {0.f, 0.f, 0.f, 0.f};
		const float no_border_width[2] = {0.f, 0.f};
		const float close_effect[4] = {
			(float)snapshot->close_style,
			snapshot->close_strength > 0.f ? snapshot->close_strength : 1.f,
			snapshot->close_direction_x,
			snapshot->close_direction_y,
		};
		const float full_frame_rect[4] = {0.f, 0.f, 1.f, 1.f};
		bool snapshot_custom_drawn = false;
		if (snapshot->plugin_shader_program && snapshot->plugin_shader_owner) {
			snapshot_custom_drawn = plugin_shader_draw_window(
				server, snapshot->plugin_shader_program, snapshot->plugin_shader_owner,
				GL_TEXTURE_2D, snapshot->texture,
				snapshot->texture_width, snapshot->texture_height, snapshot->has_alpha,
				0, 0, 0, 0,
				mvp, model, full_frame_rect, time_seconds,
				(float)buf_w, (float)buf_h, snapshot->width, snapshot->height,
				0.f, 0.f, no_water, no_water_surface,
				no_border_color, no_border_width,
				snapshot->progress, close_effect, window_tint,
				server->config.window_effect_strength,
				server->config.window_brightness, NULL, 0, NULL, 0,
				snapshot->plugin_shader_params);
		}
		if (!snapshot_custom_drawn) {
			shady_gl_pipeline_draw_window(
				&pipeline,
				GL_TEXTURE_2D,
				snapshot->texture,
				snapshot->has_alpha,
				mvp,
				model,
				full_frame_rect,
				time_seconds,
				0.0f,
				0.0f,
				no_water,
				no_water_surface,
				no_border_color,
				no_border_width,
				snapshot->progress,
				close_effect,
				window_tint,
				server->config.window_effect_strength,
				server->config.window_brightness
			);
		}
	}
	shady_gl_pipeline_set_frame_style(&pipeline, 0.f, 0.f, 0.f, 0.f, NULL);
	plugin_render_hooks_run(server, SHADY_RENDER_STAGE_AFTER_WINDOWS,
		output, buf_w, buf_h, logical_w, logical_h, time_seconds);
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
			float dx=(float)debug_t->scene_tree->node.x+ox;
			float dy=(float)debug_t->scene_tree->node.y+oy;
			float dcx=(dx+dw*.5f-logical_w*.5f)/logical_h;
			float dcy=.5f-(dy+dh*.5f)/logical_h;
			struct shady_representation_context debug_context = {
				.struct_size = sizeof(debug_context),
				.logical_width = logical_w, .logical_height = logical_h,
				.window_width = dw, .window_height = dh,
				.center_x = dcx, .center_y = dcy,
				.center_z = shady_spatial_toplevel_state(debug_t)->z,
				.tilt_x = debug_t->motion.tilt_x,
				.tilt_y = debug_t->motion.tilt_y,
				.first_person = shady_spatial_state(server)->runtime.camera.first_person,
				.folded = shady_spatial_state(server)->runtime.camera.first_person &&
					!shady_fps_toplevel_state_const(debug_t)->expanded,
				.held = shady_fps_is_holding(server, debug_t),
				.focused = !wl_list_empty(&server->toplevels) &&
					server->toplevels.next == &debug_t->link,
			};
			struct shady_representation_model debug_model;
			bool debug_folded = debug_context.folded &&
				shady_toplevel_representation_model(debug_t, &debug_context, &debug_model);
			if (debug_folded) {
				struct shady_resolved_collision_compound compound = {0};
				if (shady_toplevel_representation_collision_compound_world(debug_t,
						&debug_context, &debug_model, &compound)) {
					float hull_model[16];
					shady_mat4_identity(hull_model);
					hull_model[12] = compound.center[0];
					hull_model[13] = compound.center[1];
					hull_model[14] = compound.center[2];
					for (size_t part = 0; part < compound.part_count; ++part) {
						shady_gl_pipeline_draw_debug_convex(&pipeline, vp, hull_model,
							(const float *)compound.parts[part].vertices,
							compound.parts[part].vertex_count,
							compound.parts[part].indices,
							compound.parts[part].index_count);
					}
				} else {
					/* No convex provider: draw the authoritative fallback collision box. */
					struct shady_collision_box collision;
					shady_toplevel_representation_collision(debug_t, &debug_context,
						&debug_model, &collision);
					struct shady_box_collider box={
						collision.center[0]-collision.half[0],
						collision.center[0]+collision.half[0],
						collision.center[1]-collision.half[1],
						collision.center[1]+collision.half[1],
						collision.center[2]-collision.half[2],
						collision.center[2]+collision.half[2]
					};
					shady_gl_pipeline_draw_debug_box(&pipeline,vp,&box,false);
				}
			}else{
				float dm[16];
				shady_window_model(dm,(float)debug_t->scene_tree->node.x+ox,
					(float)debug_t->scene_tree->node.y+oy,dw,dh,logical_w,logical_h,
					shady_spatial_toplevel_state(debug_t)->z,debug_t->motion.tilt_x,debug_t->motion.tilt_y);
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
	plugin_render_hooks_run(server, SHADY_RENDER_STAGE_OVERLAY,
		output, buf_w, buf_h, logical_w, logical_h, time_seconds);
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
	if (renderer_is_software && plugin_make_current()) glFinish();

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