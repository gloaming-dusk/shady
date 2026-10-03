#include "gl_pipeline.h"
#include "math3d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>

#include <EGL/egl.h>
#include <GLES2/gl2ext.h>
#include "../world/floor.h"

#ifndef SHADY_SHADER_DIR
#define SHADY_SHADER_DIR "shaders"
#endif

#define WOBBLE_MESH_X 16
#define WOBBLE_MESH_Y 16


static char *read_shader_file(const char *name) {
	char path[512];

	int n = snprintf(
		path,
		sizeof(path),
		"%s/%s",
		SHADY_SHADER_DIR,
		name
	);

	if (n < 0 || (size_t)n >= sizeof(path)) {
		wlr_log(
			WLR_ERROR,
			"shader path too long for %s",
			name
		);
		return NULL;
	}

	FILE *f = fopen(path, "rb");

	if (!f) {
		wlr_log(
			WLR_ERROR,
			"failed to open shader %s",
			path
		);
		return NULL;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}

	long len = ftell(f);

	if (len < 0) {
		fclose(f);
		return NULL;
	}

	rewind(f);

	char *buf = malloc((size_t)len + 1);

	if (!buf) {
		fclose(f);
		return NULL;
	}

	size_t nread = fread(
		buf,
		1,
		(size_t)len,
		f
	);

	fclose(f);

	if (nread != (size_t)len) {
		free(buf);
		return NULL;
	}

	buf[len] = '\0';

	return buf;
}

static GLuint compile_shader(
	GLenum type,
	const char *source,
	const char *label
) {
	GLuint shader = glCreateShader(type);

	glShaderSource(
		shader,
		1,
		&source,
		NULL
	);

	glCompileShader(shader);

	GLint ok = GL_FALSE;

	glGetShaderiv(
		shader,
		GL_COMPILE_STATUS,
		&ok
	);

	if (!ok) {
		char log[1024];

		glGetShaderInfoLog(
			shader,
			sizeof(log),
			NULL,
			log
		);

		wlr_log(
			WLR_ERROR,
			"failed to compile %s: %s",
			label,
			log
		);

		glDeleteShader(shader);

		return 0;
	}

	return shader;
}

static GLuint link_program(
	const char *vert_src,
	const char *frag_src,
	const char *label
) {
	GLuint vs = compile_shader(
		GL_VERTEX_SHADER,
		vert_src,
		"vertex"
	);

	if (!vs) {
		return 0;
	}

	GLuint fs = compile_shader(
		GL_FRAGMENT_SHADER,
		frag_src,
		label
	);

	if (!fs) {
		glDeleteShader(vs);
		return 0;
	}

	GLuint prog = glCreateProgram();

	glAttachShader(prog, vs);
	glAttachShader(prog, fs);

	glBindAttribLocation(
		prog,
		0,
		"a_pos"
	);
	glBindAttribLocation(
		prog,
		1,
		"a_normal"
	);
	glBindAttribLocation(prog, 2, "a_uv");

	glLinkProgram(prog);

	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = GL_FALSE;

	glGetProgramiv(
		prog,
		GL_LINK_STATUS,
		&ok
	);

	if (!ok) {
		char log[1024];

		glGetProgramInfoLog(
			prog,
			sizeof(log),
			NULL,
			log
		);

		wlr_log(
			WLR_ERROR,
			"failed to link %s: %s",
			label,
			log
		);

		glDeleteProgram(prog);

		return 0;
	}

	return prog;
}

static GLuint link_program_files(const char *vert_name,
		const char *frag_name, const char *label) {
	char *vert = read_shader_file(vert_name);
	char *frag = read_shader_file(frag_name);
	if (!vert || !frag) {
		free(vert);
		free(frag);
		return 0;
	}
	GLuint prog = link_program(vert, frag, label);
	free(vert);
	free(frag);
	return prog;
}

static bool make_egl_current(
	struct wlr_renderer *renderer
) {
	struct wlr_egl *egl =
		wlr_gles2_renderer_get_egl(renderer);

	if (!egl) {
		return false;
	}

	EGLDisplay dpy =
		wlr_egl_get_display(egl);

	EGLContext ctx =
		wlr_egl_get_context(egl);

	if (!eglMakeCurrent(
		dpy,
		EGL_NO_SURFACE,
		EGL_NO_SURFACE,
		ctx
	)) {
		wlr_log(
			WLR_ERROR,
			"eglMakeCurrent failed"
		);

		return false;
	}

	return true;
}

static bool create_window_mesh(
	struct shady_gl_pipeline *pipeline
) {
	/*
	 * Two triangles per grid cell.
	 *
	 * 16 x 16 cells =
	 * 512 triangles =
	 * 1536 vertices.
	 *
	 * Tiny for a GPU, but enough subdivisions for a smooth wobble.
	 */
	const int cells =
		WOBBLE_MESH_X *
		WOBBLE_MESH_Y;

	const int vertex_count =
		cells * 6;

	const int floats_per_vertex = 3;

	GLfloat *vertices = calloc(
		(size_t)vertex_count *
		floats_per_vertex,
		sizeof(GLfloat)
	);

	if (!vertices) {
		return false;
	}

	int index = 0;

	for (int y = 0; y < WOBBLE_MESH_Y; ++y) {
		float y0 =
			(float)y /
			(float)WOBBLE_MESH_Y;

		float y1 =
			(float)(y + 1) /
			(float)WOBBLE_MESH_Y;

		for (int x = 0; x < WOBBLE_MESH_X; ++x) {
			float x0 =
				(float)x /
				(float)WOBBLE_MESH_X;

			float x1 =
				(float)(x + 1) /
				(float)WOBBLE_MESH_X;

#define PUSH_VERTEX(px, py) \
			do { \
				vertices[index++] = (px); \
				vertices[index++] = (py); \
				vertices[index++] = 0.0f; \
			} while (0)

			/*
			 * Triangle 1
			 */
			PUSH_VERTEX(x0, y0);
			PUSH_VERTEX(x1, y0);
			PUSH_VERTEX(x0, y1);

			/*
			 * Triangle 2
			 */
			PUSH_VERTEX(x0, y1);
			PUSH_VERTEX(x1, y0);
			PUSH_VERTEX(x1, y1);

#undef PUSH_VERTEX
		}
	}

	glGenBuffers(
		1,
		&pipeline->mesh_vbo
	);

	glBindBuffer(
		GL_ARRAY_BUFFER,
		pipeline->mesh_vbo
	);

	glBufferData(
		GL_ARRAY_BUFFER,
		(GLsizeiptr)(
			vertex_count *
			floats_per_vertex *
			sizeof(GLfloat)
		),
		vertices,
		GL_STATIC_DRAW
	);

	glBindBuffer(
		GL_ARRAY_BUFFER,
		0
	);

	free(vertices);

	pipeline->mesh_vertex_count =
		(GLsizei)vertex_count;

	return pipeline->mesh_vbo != 0;
}

static bool create_floor_mesh(struct shady_gl_pipeline *pipeline) {
	/*
	 * Horizontal XZ ground plane. The desktop origin is centered on-screen,
	 * so place the ground below it in world Y instead of behind it in Z.
	 */
	struct shady_floor f = shady_world_floor();
	GLfloat v[] = {
		f.min_x,f.y,f.min_z, f.max_x,f.y,f.min_z, f.min_x,f.y,f.max_z,
		f.min_x,f.y,f.max_z, f.max_x,f.y,f.min_z, f.max_x,f.y,f.max_z,
	};
	glGenBuffers(1, &pipeline->floor_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->floor_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	pipeline->floor_vertex_count = 6;
	return pipeline->floor_vbo != 0;
}

static bool create_side_mesh(struct shady_gl_pipeline *pipeline) {
	/*
	 * Four subdivided walls of a unit box. Subdivision lets the perimeter
	 * follow the same flexible deformation as the front surface.
	 */
	const int segments = WOBBLE_MESH_X;
	/* Keep only the four thickness walls. A solid back plate becomes visible
	 * behind the flexible front face when wobble bows the window, producing
	 * a large dark rectangle instead of revealing the environment. */
	const int wall_vertex_count = segments * 4 * 6;
	const int vertex_count = wall_vertex_count;
	GLfloat *v = calloc((size_t)vertex_count * 6, sizeof(GLfloat));
	if (!v) return false;
	int n = 0;
#define SIDE_VERTEX(px,py,pz,nx,ny,nz) do { \
	v[n++]=(px); v[n++]=(py); v[n++]=(pz); \
	v[n++]=(nx); v[n++]=(ny); v[n++]=(nz); \
} while (0)
#define SIDE_QUAD(x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3,nx,ny,nz) do { \
	SIDE_VERTEX(x0,y0,z0,nx,ny,nz); SIDE_VERTEX(x1,y1,z1,nx,ny,nz); SIDE_VERTEX(x2,y2,z2,nx,ny,nz); \
	SIDE_VERTEX(x2,y2,z2,nx,ny,nz); SIDE_VERTEX(x1,y1,z1,nx,ny,nz); SIDE_VERTEX(x3,y3,z3,nx,ny,nz); \
} while (0)
	for (int i = 0; i < segments; ++i) {
		float a=(float)i/segments, b=(float)(i+1)/segments;
		SIDE_QUAD(0,a,0, 0,a,-1, 0,b,0, 0,b,-1, -1,0,0);
		SIDE_QUAD(1,a,0, 1,b,0, 1,a,-1, 1,b,-1, 1,0,0);
		SIDE_QUAD(a,0,0, b,0,0, a,0,-1, b,0,-1, 0,-1,0);
		SIDE_QUAD(a,1,0, a,1,-1, b,1,0, b,1,-1, 0,1,0);
	}
#undef SIDE_QUAD
#undef SIDE_VERTEX
	glGenBuffers(1, &pipeline->side_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->side_vbo);
	glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof(GLfloat)), v, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	free(v);
	pipeline->side_vertex_count = vertex_count;
	return pipeline->side_vbo != 0;
}

/* old static mesh removed */
#if 0
	static const GLfloat v[] = {
		/* left, normal -X */
		0,0,0, -1,0,0,  0,1,0, -1,0,0,  0,0,-1, -1,0,0,
		0,0,-1, -1,0,0,  0,1,0, -1,0,0,  0,1,-1, -1,0,0,
		/* right, normal +X */
		1,0,0, 1,0,0,  1,0,-1, 1,0,0,  1,1,0, 1,0,0,
		1,0,-1, 1,0,0,  1,1,-1, 1,0,0,  1,1,0, 1,0,0,
		/* bottom, normal -Y */
		0,0,0, 0,-1,0,  0,0,-1, 0,-1,0,  1,0,0, 0,-1,0,
		1,0,0, 0,-1,0,  0,0,-1, 0,-1,0,  1,0,-1, 0,-1,0,
		/* top, normal +Y */
		0,1,0, 0,1,0,  1,1,0, 0,1,0,  0,1,-1, 0,1,0,
		0,1,-1, 0,1,0,  1,1,0, 0,1,0,  1,1,-1, 0,1,0,
	};
	glGenBuffers(1, &pipeline->side_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->side_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	pipeline->side_vertex_count = 24;
	return pipeline->side_vbo != 0;
}
#endif

bool shady_gl_pipeline_init(
	struct shady_gl_pipeline *pipeline,
	struct wlr_renderer *renderer
) {
	memset(
		pipeline,
		0,
		sizeof(*pipeline)
	);

	if (!wlr_renderer_is_gles2(renderer)) {
		wlr_log(
			WLR_ERROR,
			"gl_pipeline requires GLES2 renderer"
		);

		return false;
	}

	if (!make_egl_current(renderer)) {
		return false;
	}

	char *vert =
		read_shader_file("window.vert");

	char *frag =
		read_shader_file("window.frag");

	char *frag_ext =
		read_shader_file("window_ext.frag");

	if (!vert || !frag || !frag_ext) {
		free(vert);
		free(frag);
		free(frag_ext);

		return false;
	}

	pipeline->prog_2d =
		link_program(
			vert,
			frag,
			"window.frag"
		);

	pipeline->prog_ext =
		link_program(
			vert,
			frag_ext,
			"window_ext.frag"
		);

	free(vert);
	free(frag);
	free(frag_ext);

	if (!pipeline->prog_2d ||
			!pipeline->prog_ext) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}

	/*
	 * Normal GLES2 texture uniforms.
	 */
	pipeline->u_mvp_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_mvp"
		);

	pipeline->u_tex_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_tex"
		);

	pipeline->u_tint_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_tint"
		);

	pipeline->u_has_alpha_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_has_alpha"
		);

	pipeline->u_time_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_time"
		);
	pipeline->u_wobble_2d =
		glGetUniformLocation(
			pipeline->prog_2d,
			"u_wobble"
		);
	pipeline->u_water_2d = glGetUniformLocation(pipeline->prog_2d, "u_water");
	pipeline->u_water_surface_2d = glGetUniformLocation(pipeline->prog_2d, "u_water_surface");
	pipeline->u_border_color_2d = glGetUniformLocation(pipeline->prog_2d, "u_border_color");
	pipeline->u_border_width_2d = glGetUniformLocation(pipeline->prog_2d, "u_border_width");

	pipeline->u_close_progress_2d =
		glGetUniformLocation(pipeline->prog_2d, "u_close_progress");
	pipeline->u_close_effect_2d =
		glGetUniformLocation(pipeline->prog_2d, "u_close_effect");
	pipeline->u_model_2d = glGetUniformLocation(pipeline->prog_2d, "u_model");
	pipeline->u_light_dir_2d = glGetUniformLocation(pipeline->prog_2d, "u_light_dir");
	pipeline->u_effect_strength_2d = glGetUniformLocation(pipeline->prog_2d, "u_effect_strength");
	pipeline->u_brightness_2d = glGetUniformLocation(pipeline->prog_2d, "u_brightness");
	pipeline->u_frame_rect_2d = glGetUniformLocation(pipeline->prog_2d, "u_frame_rect");
	pipeline->u_use_vertex_uv_2d = glGetUniformLocation(pipeline->prog_2d, "u_use_vertex_uv");

	/*
	 * EGL external texture uniforms.
	 */
	pipeline->u_mvp_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_mvp"
		);

	pipeline->u_tex_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_tex"
		);

	pipeline->u_tint_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_tint"
		);

	pipeline->u_has_alpha_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_has_alpha"
		);

	pipeline->u_time_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_time"
		);
	
	pipeline->u_wobble_ext =
		glGetUniformLocation(
			pipeline->prog_ext,
			"u_wobble"
		);
	pipeline->u_water_ext = glGetUniformLocation(pipeline->prog_ext, "u_water");
	pipeline->u_water_surface_ext = glGetUniformLocation(pipeline->prog_ext, "u_water_surface");
	pipeline->u_border_color_ext = glGetUniformLocation(pipeline->prog_ext, "u_border_color");
	pipeline->u_border_width_ext = glGetUniformLocation(pipeline->prog_ext, "u_border_width");

	pipeline->u_close_progress_ext =
		glGetUniformLocation(pipeline->prog_ext, "u_close_progress");
	pipeline->u_close_effect_ext =
		glGetUniformLocation(pipeline->prog_ext, "u_close_effect");
	pipeline->u_model_ext = glGetUniformLocation(pipeline->prog_ext, "u_model");
	pipeline->u_light_dir_ext = glGetUniformLocation(pipeline->prog_ext, "u_light_dir");
	pipeline->u_effect_strength_ext = glGetUniformLocation(pipeline->prog_ext, "u_effect_strength");
	pipeline->u_brightness_ext = glGetUniformLocation(pipeline->prog_ext, "u_brightness");
	pipeline->u_frame_rect_ext = glGetUniformLocation(pipeline->prog_ext, "u_frame_rect");
	pipeline->u_use_vertex_uv_ext = glGetUniformLocation(pipeline->prog_ext, "u_use_vertex_uv");

	pipeline->titlebar_prog = link_program_files("titlebar.vert", "titlebar.frag", "titlebar");
	if (!pipeline->titlebar_prog) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->titlebar_u_mvp = glGetUniformLocation(pipeline->titlebar_prog, "u_mvp");
	pipeline->titlebar_u_tex = glGetUniformLocation(pipeline->titlebar_prog, "u_tex");
	pipeline->titlebar_u_opacity = glGetUniformLocation(pipeline->titlebar_prog, "u_opacity");

	pipeline->copy_prog_2d = link_program_files(
		"copy.vert", "copy.frag", "snapshot copy 2D");

	pipeline->copy_prog_ext = link_program_files(
		"copy.vert", "copy_ext.frag", "snapshot copy external");

	if (
		!pipeline->copy_prog_2d ||
		!pipeline->copy_prog_ext
	) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}

	pipeline->copy_tex_2d =
		glGetUniformLocation(
			pipeline->copy_prog_2d,
			"u_tex"
		);

	pipeline->copy_tex_ext =
		glGetUniformLocation(
			pipeline->copy_prog_ext,
			"u_tex"
		);

	pipeline->side_prog = link_program_files("side.vert", "side.frag", "window sides");
	if (!pipeline->side_prog || !create_side_mesh(pipeline)) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->side_u_mvp = glGetUniformLocation(pipeline->side_prog, "u_mvp");
	pipeline->side_u_model = glGetUniformLocation(pipeline->side_prog, "u_model");
	pipeline->side_u_light_dir = glGetUniformLocation(pipeline->side_prog, "u_light_dir");
	pipeline->side_u_base_color = glGetUniformLocation(pipeline->side_prog, "u_base_color");
	pipeline->side_u_wobble = glGetUniformLocation(pipeline->side_prog, "u_wobble");

	pipeline->background_prog = link_program_files(
		"background.vert", "background.frag", "background gradient");
	if (!pipeline->background_prog) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->background_u_top = glGetUniformLocation(pipeline->background_prog, "u_top");
	pipeline->background_u_horizon = glGetUniformLocation(pipeline->background_prog, "u_horizon");
	pipeline->background_u_bottom = glGetUniformLocation(pipeline->background_prog, "u_bottom");

	pipeline->floor_prog = link_program_files("floor.vert", "floor.frag", "3D floor");
	if (!pipeline->floor_prog || !create_floor_mesh(pipeline)) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->floor_u_vp = glGetUniformLocation(pipeline->floor_prog, "u_vp");
	pipeline->floor_u_base_color = glGetUniformLocation(pipeline->floor_prog, "u_base_color");
	pipeline->floor_u_grid_color = glGetUniformLocation(pipeline->floor_prog, "u_grid_color");
	pipeline->floor_u_grid_strength = glGetUniformLocation(pipeline->floor_prog, "u_grid_strength");
	pipeline->floor_u_major_strength = glGetUniformLocation(pipeline->floor_prog, "u_major_strength");
	pipeline->floor_u_fade_start = glGetUniformLocation(pipeline->floor_prog, "u_fade_start");
	pipeline->floor_u_fade_end = glGetUniformLocation(pipeline->floor_prog, "u_fade_end");

	pipeline->shadow_prog = link_program_files("shadow.vert", "shadow.frag", "window shadow");
	if (!pipeline->shadow_prog) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->shadow_u_vp = glGetUniformLocation(pipeline->shadow_prog, "u_vp");
	pipeline->shadow_u_model = glGetUniformLocation(pipeline->shadow_prog, "u_model");
	pipeline->shadow_u_wobble = glGetUniformLocation(pipeline->shadow_prog, "u_wobble");
	pipeline->shadow_u_softness = glGetUniformLocation(pipeline->shadow_prog, "u_softness");
	pipeline->shadow_u_opacity = glGetUniformLocation(pipeline->shadow_prog, "u_opacity");
	pipeline->shadow_u_floor_bounds = glGetUniformLocation(pipeline->shadow_prog, "u_floor_bounds");

	pipeline->debug_prog = link_program_files("debug.vert", "debug.frag", "debug ray");
	if (!pipeline->debug_prog) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->debug_u_vp = glGetUniformLocation(pipeline->debug_prog, "u_vp");
	pipeline->debug_u_color = glGetUniformLocation(pipeline->debug_prog, "u_color");

	wlr_log(
		WLR_INFO,
		"GLES2 3D window pipeline ready "
		"(animated shaders from %s)",
		SHADY_SHADER_DIR
	);

	if (!create_window_mesh(pipeline)) {
		wlr_log(
			WLR_ERROR,
			"failed to create wobbly window mesh"
		);

		shady_gl_pipeline_fini(
			pipeline
		);

		return false;
	}

	return true;
}

void shady_gl_pipeline_fini(
	struct shady_gl_pipeline *pipeline
) {
	if (pipeline->background_prog) {
		glDeleteProgram(pipeline->background_prog);
		pipeline->background_prog = 0;
	}
	if (pipeline->debug_prog) {
		glDeleteProgram(pipeline->debug_prog);
		pipeline->debug_prog = 0;
	}
	if (pipeline->shadow_prog) {
		glDeleteProgram(pipeline->shadow_prog);
		pipeline->shadow_prog = 0;
	}
	if (pipeline->floor_vbo) {
		glDeleteBuffers(1, &pipeline->floor_vbo);
		pipeline->floor_vbo = 0;
		pipeline->floor_vertex_count = 0;
	}
	if (pipeline->floor_prog) {
		glDeleteProgram(pipeline->floor_prog);
		pipeline->floor_prog = 0;
	}
	if (pipeline->side_vbo) {
		glDeleteBuffers(1, &pipeline->side_vbo);
		pipeline->side_vbo = 0;
		pipeline->side_vertex_count = 0;
	}
	if (pipeline->side_prog) {
		glDeleteProgram(pipeline->side_prog);
		pipeline->side_prog = 0;
	}
	if (pipeline->mesh_vbo) {
		glDeleteBuffers(1, &pipeline->mesh_vbo);
		pipeline->mesh_vbo = 0;
		pipeline->mesh_vertex_count = 0;
	}
	if (pipeline->dynamic_mesh_vbo) {
		glDeleteBuffers(1, &pipeline->dynamic_mesh_vbo);
		pipeline->dynamic_mesh_vbo = 0;
	}
	if (pipeline->prog_2d) {
		glDeleteProgram(
			pipeline->prog_2d
		);

		pipeline->prog_2d = 0;
	}

	if (pipeline->prog_ext) {
		glDeleteProgram(
			pipeline->prog_ext
		);

		pipeline->prog_ext = 0;
	}

	if (pipeline->titlebar_prog) {
		glDeleteProgram(pipeline->titlebar_prog);
		pipeline->titlebar_prog = 0;
	}

	if (pipeline->copy_prog_2d) {
		glDeleteProgram(
			pipeline->copy_prog_2d
		);

		pipeline->copy_prog_2d = 0;
	}

	if (pipeline->copy_prog_ext) {
		glDeleteProgram(
			pipeline->copy_prog_ext
		);

		pipeline->copy_prog_ext = 0;
	}
}

void shady_gl_pipeline_draw_window(
	struct shady_gl_pipeline *pipeline,
	GLenum target,
	GLuint tex,
	bool has_alpha,
	const float mvp[16],
	const float model[16],
	const float frame_rect[4],
	float time_seconds,
	float wobble_x,
	float wobble_y,
	const float water[4],
	const float water_surface[4],
	const float border_color[4],
	const float border_width[2],
	float close_progress,
	const float close_effect[4],
	const float tint[4],
	float effect_strength,
	float brightness
) {
	bool external =
		(target == GL_TEXTURE_EXTERNAL_OES);

	GLuint prog =
		external
			? pipeline->prog_ext
			: pipeline->prog_2d;

	GLint u_mvp =
		external
			? pipeline->u_mvp_ext
			: pipeline->u_mvp_2d;

	GLint u_tex =
		external
			? pipeline->u_tex_ext
			: pipeline->u_tex_2d;

	GLint u_tint =
		external
			? pipeline->u_tint_ext
			: pipeline->u_tint_2d;

	GLint u_has_alpha =
		external
			? pipeline->u_has_alpha_ext
			: pipeline->u_has_alpha_2d;

	GLint u_time =
		external
			? pipeline->u_time_ext
			: pipeline->u_time_2d;

	GLint u_wobble =
		external
			? pipeline->u_wobble_ext
			: pipeline->u_wobble_2d;

	GLint u_water = external ? pipeline->u_water_ext : pipeline->u_water_2d;
	GLint u_water_surface = external ? pipeline->u_water_surface_ext : pipeline->u_water_surface_2d;
	GLint u_border_color = external ? pipeline->u_border_color_ext : pipeline->u_border_color_2d;
	GLint u_border_width = external ? pipeline->u_border_width_ext : pipeline->u_border_width_2d;
	GLint u_close_progress = external ? pipeline->u_close_progress_ext : pipeline->u_close_progress_2d;
	GLint u_close_effect = external ? pipeline->u_close_effect_ext : pipeline->u_close_effect_2d;
	GLint u_model = external ? pipeline->u_model_ext : pipeline->u_model_2d;
	GLint u_light_dir = external ? pipeline->u_light_dir_ext : pipeline->u_light_dir_2d;
	GLint u_effect_strength = external ? pipeline->u_effect_strength_ext : pipeline->u_effect_strength_2d;
	GLint u_brightness = external ? pipeline->u_brightness_ext : pipeline->u_brightness_2d;
	GLint u_frame_rect = external ? pipeline->u_frame_rect_ext : pipeline->u_frame_rect_2d;
	GLint u_use_vertex_uv = external ? pipeline->u_use_vertex_uv_ext : pipeline->u_use_vertex_uv_2d;

	glUseProgram(prog);

	glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
	glUniform3f(u_light_dir, -0.45f, 0.72f, 0.53f);
	glUniform1f(u_effect_strength, effect_strength);
	glUniform1f(u_brightness, brightness);
	glUniform4fv(u_frame_rect, 1, frame_rect);
	if (u_use_vertex_uv >= 0)
		glUniform1f(u_use_vertex_uv, pipeline->mesh_use_vertex_uv ? 1.f : 0.f);

	glUniform4fv(
		u_tint,
		1,
		tint
	);

	glUniform1f(
		u_has_alpha,
		has_alpha ? 1.0f : 0.0f
	);

	glUniform1f(
		u_time,
		time_seconds
	);

	glUniform2f(
		u_wobble,
		wobble_x,
		wobble_y
	);
	glUniform4fv(u_water, 1, water);
	glUniform4fv(u_water_surface, 1, water_surface);
	glUniform4fv(u_border_color, 1, border_color);
	glUniform2fv(u_border_width, 1, border_width);

	glUniform1f(
		u_close_progress,
		close_progress
	);
	glUniform4fv(u_close_effect, 1, close_effect);

	glUniform1i(
		u_tex,
		0
	);

	glActiveTexture(
		GL_TEXTURE0
	);

	glBindTexture(
		target,
		tex
	);

	glTexParameteri(
		target,
		GL_TEXTURE_MIN_FILTER,
		GL_LINEAR
	);

	glTexParameteri(
		target,
		GL_TEXTURE_MAG_FILTER,
		GL_LINEAR
	);

	glTexParameteri(
		target,
		GL_TEXTURE_WRAP_S,
		GL_CLAMP_TO_EDGE
	);

	glTexParameteri(
		target,
		GL_TEXTURE_WRAP_T,
		GL_CLAMP_TO_EDGE
	);

	if (has_alpha || tint[3] < 0.999f) {
		glEnable(GL_BLEND);

		glBlendFunc(
			GL_ONE,
			GL_ONE_MINUS_SRC_ALPHA
		);

		glDepthMask(GL_FALSE);
	} else {
		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
	}

	glBindBuffer(
		GL_ARRAY_BUFFER,
		pipeline->mesh_vbo
	);

	GLsizei vertex_stride = (pipeline->mesh_use_vertex_uv ? 5 : 3) * sizeof(GLfloat);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vertex_stride, (void *)0);
	glEnableVertexAttribArray(0);
	if (pipeline->mesh_use_vertex_uv) {
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vertex_stride,
			(void *)(3 * sizeof(GLfloat)));
		glEnableVertexAttribArray(2);
	} else {
		glDisableVertexAttribArray(2);
	}

	glDrawArrays(
		GL_TRIANGLES,
		0,
		pipeline->mesh_vertex_count
	);

	glDisableVertexAttribArray(0);
	if (pipeline->mesh_use_vertex_uv) glDisableVertexAttribArray(2);

	glBindBuffer(
		GL_ARRAY_BUFFER,
		0
	);

	glDepthMask(GL_TRUE);

	glBindTexture(
		target,
		0
	);

	glUseProgram(0);
}

bool shady_gl_pipeline_prepare_dynamic_mesh(
		struct shady_gl_pipeline *pipeline, const float *vertices,
		size_t vertex_count, const uint16_t *indices, size_t index_count) {
	if (!pipeline || !vertices || vertex_count < 3 || vertex_count > 16384)
		return false;
	size_t draw_count = indices ? index_count : vertex_count;
	if (draw_count < 3 || draw_count > 49152 || draw_count % 3 != 0)
		return false;
	float *expanded = malloc(draw_count * 5 * sizeof(float));
	if (!expanded) return false;
	for (size_t i = 0; i < draw_count; ++i) {
		size_t src = indices ? indices[i] : i;
		if (src >= vertex_count) { free(expanded); return false; }
		memcpy(&expanded[i * 5], &vertices[src * 5], 5 * sizeof(float));
	}
	if (!pipeline->dynamic_mesh_vbo)
		glGenBuffers(1, &pipeline->dynamic_mesh_vbo);
	if (!pipeline->dynamic_mesh_vbo) { free(expanded); return false; }
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->dynamic_mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(draw_count * 5 * sizeof(float)),
		expanded, GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	free(expanded);
	pipeline->dynamic_mesh_vertex_count = (GLsizei)draw_count;
	return true;
}

void shady_gl_pipeline_draw_window_mesh(
		struct shady_gl_pipeline *pipeline, GLenum target, GLuint tex,
		bool has_alpha, const float mvp[16], const float model[16],
		const float frame_rect[4], float time_seconds,
		float wobble_x, float wobble_y, const float water[4],
		const float water_surface[4], const float border_color[4],
		const float border_width[2], float close_progress,
		const float close_effect[4], const float tint[4],
		float effect_strength, float brightness,
		const float *vertices, size_t vertex_count,
		const uint16_t *indices, size_t index_count) {
	if (!shady_gl_pipeline_prepare_dynamic_mesh(pipeline, vertices, vertex_count,
			indices, index_count))
		return;
	GLuint old_vbo = pipeline->mesh_vbo;
	GLsizei old_count = pipeline->mesh_vertex_count;
	bool old_use_uv = pipeline->mesh_use_vertex_uv;
	pipeline->mesh_vbo = pipeline->dynamic_mesh_vbo;
	pipeline->mesh_vertex_count = pipeline->dynamic_mesh_vertex_count;
	pipeline->mesh_use_vertex_uv = true;
	shady_gl_pipeline_draw_window(pipeline, target, tex, has_alpha,
		mvp, model, frame_rect, time_seconds, wobble_x, wobble_y,
		water, water_surface, border_color, border_width,
		close_progress, close_effect, tint, effect_strength, brightness);
	pipeline->mesh_vbo = old_vbo;
	pipeline->mesh_vertex_count = old_count;
	pipeline->mesh_use_vertex_uv = old_use_uv;
}

void shady_gl_pipeline_draw_titlebar(
	struct shady_gl_pipeline *pipeline,
	GLuint texture,
	const float mvp[16],
	float opacity
) {
	if (!pipeline || !pipeline->titlebar_prog || !texture || opacity <= 0.f) return;
	if (opacity > 1.f) opacity = 1.f;

	glUseProgram(pipeline->titlebar_prog);
	glUniformMatrix4fv(pipeline->titlebar_u_mvp, 1, GL_FALSE, mvp);
	glUniform1i(pipeline->titlebar_u_tex, 0);
	glUniform1f(pipeline->titlebar_u_opacity, opacity);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->mesh_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
		3 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, pipeline->mesh_vertex_count);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDepthMask(GL_TRUE);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_sides(
	struct shady_gl_pipeline *pipeline,
	const float mvp[16],
	const float model[16],
	float wobble_x,
	float wobble_y
) {
	glUseProgram(pipeline->side_prog);
	glUniformMatrix4fv(pipeline->side_u_mvp, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(pipeline->side_u_model, 1, GL_FALSE, model);

	/* A fixed world-space key light from upper-left and slightly forward. */
	glUniform3f(pipeline->side_u_light_dir, -0.45f, 0.72f, 0.53f);
	glUniform4f(pipeline->side_u_base_color, 0.16f, 0.21f, 0.31f, 1.0f);
	glUniform2f(pipeline->side_u_wobble, wobble_x, wobble_y);

	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->side_vbo);

	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
		6 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
		6 * sizeof(GLfloat), (void *)(3 * sizeof(GLfloat)));
	glEnableVertexAttribArray(1);

	glDrawArrays(GL_TRIANGLES, 0, pipeline->side_vertex_count);

	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_background(
		struct shady_gl_pipeline *pipeline,
		const float top[3], const float horizon[3], const float bottom[3]) {
	static const GLfloat verts[] = {
		-1.0f, -1.0f,
		 1.0f, -1.0f,
		-1.0f,  1.0f,
		-1.0f,  1.0f,
		 1.0f, -1.0f,
		 1.0f,  1.0f,
	};
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_BLEND);
	glUseProgram(pipeline->background_prog);
	glUniform3fv(pipeline->background_u_top, 1, top);
	glUniform3fv(pipeline->background_u_horizon, 1, horizon);
	glUniform3fv(pipeline->background_u_bottom, 1, bottom);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), verts);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(0);
	glUseProgram(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}

void shady_gl_pipeline_draw_floor(
	struct shady_gl_pipeline *pipeline,
	const float vp[16],
	const struct shady_floor *floor,
	const float base_color[3],
	const float grid_color[3],
	float grid_strength,
	float major_strength,
	float fade_start,
	float fade_end
) {
	(void)floor;
	glUseProgram(pipeline->floor_prog);
	glUniformMatrix4fv(pipeline->floor_u_vp, 1, GL_FALSE, vp);
	glUniform3fv(pipeline->floor_u_base_color, 1, base_color);
	glUniform3fv(pipeline->floor_u_grid_color, 1, grid_color);
	glUniform1f(pipeline->floor_u_grid_strength, grid_strength);
	glUniform1f(pipeline->floor_u_major_strength, major_strength);
	glUniform1f(pipeline->floor_u_fade_start, fade_start);
	glUniform1f(pipeline->floor_u_fade_end, fade_end);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glBindBuffer(GL_ARRAY_BUFFER, pipeline->floor_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, pipeline->floor_vertex_count);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_shadow(
	struct shady_gl_pipeline *pipeline,
	const float vp[16],
	const float model[16],
	float wobble_x,
	float wobble_y,
	float height,
	const struct shady_floor *floor
) {
	glUseProgram(pipeline->shadow_prog);
	glUniformMatrix4fv(pipeline->shadow_u_vp, 1, GL_FALSE, vp);
	glUniformMatrix4fv(pipeline->shadow_u_model, 1, GL_FALSE, model);
	glUniform2f(pipeline->shadow_u_wobble, wobble_x, wobble_y);
	glUniform4f(pipeline->shadow_u_floor_bounds, floor->min_x, floor->max_x,
		floor->min_z, floor->max_z);

	/*
	 * Approximate an area light: as the window moves away from the ground,
	 * widen the penumbra and reduce density. Height is world-space Z here,
	 * which is the user's explicit depth control, so it gives a stable and
	 * perceptible softness cue even while the camera orbits.
	 */
	float h = height;
	if (h < 0.0f) h = -h;
	if (h > 1.5f) h = 1.5f;
	float softness = 0.035f + h * 0.085f;
	float opacity = 0.32f / (1.0f + h * 1.35f);
	glUniform1f(pipeline->shadow_u_softness, softness);
	glUniform1f(pipeline->shadow_u_opacity, opacity);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_FALSE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-1.0f, -1.0f);

	glBindBuffer(GL_ARRAY_BUFFER, pipeline->mesh_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void *)0);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, pipeline->mesh_vertex_count);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glDisable(GL_POLYGON_OFFSET_FILL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_crosshair(
		struct shady_gl_pipeline *pipeline, bool target, bool holding) {
	/* Screen-space reticle: independent from the host cursor and camera depth. */
	const float sx = holding ? 0.018f : 0.012f;
	const float sy = sx * 1.78f; /* approximate 16:9 so it looks square-ish */
	GLfloat v[] = { -sx,0.f,0.f, sx,0.f,0.f, 0.f,-sy,0.f, 0.f,sy,0.f };
	glUseProgram(pipeline->debug_prog);
	float ident[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
	glUniformMatrix4fv(pipeline->debug_u_vp,1,GL_FALSE,ident);
	if (holding) glUniform4f(pipeline->debug_u_color,1.0f,0.72f,0.18f,1.0f);
	else if (target) glUniform4f(pipeline->debug_u_color,1.0f,0.25f,0.12f,1.0f);
	else glUniform4f(pipeline->debug_u_color,0.75f,0.88f,1.0f,1.0f);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER,0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,v);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_LINES,0,4);
	glDisableVertexAttribArray(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_debug_ray(
		struct shady_gl_pipeline *pipeline, const float vp[16],
		const float origin[3], const float end[3], bool hit) {
	GLfloat line[] = { origin[0],origin[1],origin[2], end[0],end[1],end[2] };
	glUseProgram(pipeline->debug_prog);
	glUniformMatrix4fv(pipeline->debug_u_vp,1,GL_FALSE,vp);
	glDisable(GL_BLEND);
	glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER,0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,line);
	glEnableVertexAttribArray(0);
	glUniform4f(pipeline->debug_u_color,0.15f,0.95f,1.0f,1.0f);
	glDrawArrays(GL_LINES,0,2);
	if (hit) {
		glUniform4f(pipeline->debug_u_color,1.0f,0.25f,0.12f,1.0f);
		const float s = 0.018f;
		GLfloat marker[] = {
			end[0]-s,end[1],end[2], end[0]+s,end[1],end[2],
			end[0],end[1]-s,end[2], end[0],end[1]+s,end[2]
		};
		glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,marker);
		glDrawArrays(GL_LINES,0,4);
	}
	glDisableVertexAttribArray(0);
	glDepthMask(GL_TRUE);
	glUseProgram(0);
}

void shady_gl_pipeline_draw_debug_window_body(
		struct shady_gl_pipeline *pipeline,const float vp[16],const float model[16]){
	static const GLfloat q[]={0,0,0,1,0,0, 1,0,0,1,1,0, 1,1,0,0,1,0, 0,1,0,0,0,0};
	float mvp[16];shady_mat4_multiply(mvp,vp,model);
	glUseProgram(pipeline->debug_prog);glUniformMatrix4fv(pipeline->debug_u_vp,1,GL_FALSE,mvp);
	glUniform4f(pipeline->debug_u_color,1.f,.15f,.85f,1.f);glDisable(GL_BLEND);glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER,0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,q);
	glEnableVertexAttribArray(0);glDrawArrays(GL_LINES,0,8);glDisableVertexAttribArray(0);
	glDepthMask(GL_TRUE);glUseProgram(0);
}

void shady_gl_pipeline_draw_debug_box(
		struct shady_gl_pipeline *pipeline, const float vp[16],
		const struct shady_box_collider *b, bool environment) {
	GLfloat v[] = {
		b->min_x,b->min_y,b->min_z, b->max_x,b->min_y,b->min_z,
		b->max_x,b->min_y,b->min_z, b->max_x,b->min_y,b->max_z,
		b->max_x,b->min_y,b->max_z, b->min_x,b->min_y,b->max_z,
		b->min_x,b->min_y,b->max_z, b->min_x,b->min_y,b->min_z,
		b->min_x,b->max_y,b->min_z, b->max_x,b->max_y,b->min_z,
		b->max_x,b->max_y,b->min_z, b->max_x,b->max_y,b->max_z,
		b->max_x,b->max_y,b->max_z, b->min_x,b->max_y,b->max_z,
		b->min_x,b->max_y,b->max_z, b->min_x,b->max_y,b->min_z,
		b->min_x,b->min_y,b->min_z, b->min_x,b->max_y,b->min_z,
		b->max_x,b->min_y,b->min_z, b->max_x,b->max_y,b->min_z,
		b->max_x,b->min_y,b->max_z, b->max_x,b->max_y,b->max_z,
		b->min_x,b->min_y,b->max_z, b->min_x,b->max_y,b->max_z
	};
	glUseProgram(pipeline->debug_prog);
	glUniformMatrix4fv(pipeline->debug_u_vp,1,GL_FALSE,vp);
	if(environment)glUniform4f(pipeline->debug_u_color,1.f,.55f,.12f,1.f);
	else glUniform4f(pipeline->debug_u_color,.18f,1.f,.45f,1.f);
	glDisable(GL_BLEND); glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER,0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,v);
	glEnableVertexAttribArray(0); glDrawArrays(GL_LINES,0,24);
	glDisableVertexAttribArray(0); glDepthMask(GL_TRUE); glUseProgram(0);
}

void shady_gl_pipeline_draw_debug_convex(
		struct shady_gl_pipeline *pipeline, const float vp[16], const float model[16],
		const float *vertices, size_t vertex_count,
		const uint16_t *indices, size_t index_count) {
	if (!pipeline || !vp || !model || !vertices || !indices ||
			vertex_count < 4 || index_count < 12 || index_count % 3 != 0)
		return;
	size_t line_vertex_count = index_count * 2;
	GLfloat *lines = malloc(line_vertex_count * 3 * sizeof(GLfloat));
	if (!lines) return;
	size_t out = 0;
	for (size_t i = 0; i < index_count; i += 3) {
		uint16_t tri[3] = {indices[i], indices[i + 1], indices[i + 2]};
		if (tri[0] >= vertex_count || tri[1] >= vertex_count || tri[2] >= vertex_count) {
			free(lines);
			return;
		}
		for (int edge = 0; edge < 3; ++edge) {
			uint16_t a = tri[edge];
			uint16_t b = tri[(edge + 1) % 3];
			memcpy(&lines[out * 3], &vertices[a * 3], 3 * sizeof(GLfloat));
			out++;
			memcpy(&lines[out * 3], &vertices[b * 3], 3 * sizeof(GLfloat));
			out++;
		}
	}
	float mvp[16];
	shady_mat4_multiply(mvp, vp, model);
	glUseProgram(pipeline->debug_prog);
	glUniformMatrix4fv(pipeline->debug_u_vp, 1, GL_FALSE, mvp);
	glUniform4f(pipeline->debug_u_color, .25f, 1.f, .95f, 1.f);
	glDisable(GL_BLEND);
	glDepthMask(GL_FALSE);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, lines);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_LINES, 0, (GLsizei)out);
	glDisableVertexAttribArray(0);
	glDepthMask(GL_TRUE);
	glUseProgram(0);
	free(lines);
}

void shady_gl_pipeline_draw_debug_triangle(
		struct shady_gl_pipeline *pipeline,const float vp[16],
		const struct shady_triangle_collider *t){
	GLfloat v[]={
		t->v[0][0],t->v[0][1],t->v[0][2], t->v[1][0],t->v[1][1],t->v[1][2],
		t->v[1][0],t->v[1][1],t->v[1][2], t->v[2][0],t->v[2][1],t->v[2][2],
		t->v[2][0],t->v[2][1],t->v[2][2], t->v[0][0],t->v[0][1],t->v[0][2]
	};
	glUseProgram(pipeline->debug_prog);
	glUniformMatrix4fv(pipeline->debug_u_vp,1,GL_FALSE,vp);
	glUniform4f(pipeline->debug_u_color,1.f,.95f,.15f,1.f);
	glDisable(GL_BLEND);glDepthMask(GL_FALSE);glBindBuffer(GL_ARRAY_BUFFER,0);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,v);glEnableVertexAttribArray(0);
	glDrawArrays(GL_LINES,0,6);glDisableVertexAttribArray(0);
	glDepthMask(GL_TRUE);glUseProgram(0);
}

bool shady_gl_pipeline_copy_texture(
	struct shady_gl_pipeline *pipeline,
	GLenum source_target,
	GLuint source_texture,
	int width,
	int height,
	GLuint *out_texture
) {
	if (
		width <= 0 ||
		height <= 0 ||
		!out_texture
	) {
		return false;
	}

	GLint old_fbo = 0;
	GLint old_viewport[4];

	glGetIntegerv(
		GL_FRAMEBUFFER_BINDING,
		&old_fbo
	);

	glGetIntegerv(
		GL_VIEWPORT,
		old_viewport
	);

	GLuint texture = 0;
	GLuint fbo = 0;

	glGenTextures(
		1,
		&texture
	);

	glBindTexture(
		GL_TEXTURE_2D,
		texture
	);

	glTexParameteri(
		GL_TEXTURE_2D,
		GL_TEXTURE_MIN_FILTER,
		GL_LINEAR
	);

	glTexParameteri(
		GL_TEXTURE_2D,
		GL_TEXTURE_MAG_FILTER,
		GL_LINEAR
	);

	glTexParameteri(
		GL_TEXTURE_2D,
		GL_TEXTURE_WRAP_S,
		GL_CLAMP_TO_EDGE
	);

	glTexParameteri(
		GL_TEXTURE_2D,
		GL_TEXTURE_WRAP_T,
		GL_CLAMP_TO_EDGE
	);

	glTexImage2D(
		GL_TEXTURE_2D,
		0,
		GL_RGBA,
		width,
		height,
		0,
		GL_RGBA,
		GL_UNSIGNED_BYTE,
		NULL
	);

	glGenFramebuffers(
		1,
		&fbo
	);

	glBindFramebuffer(
		GL_FRAMEBUFFER,
		fbo
	);

	glFramebufferTexture2D(
		GL_FRAMEBUFFER,
		GL_COLOR_ATTACHMENT0,
		GL_TEXTURE_2D,
		texture,
		0
	);

	if (
		glCheckFramebufferStatus(
			GL_FRAMEBUFFER
		) != GL_FRAMEBUFFER_COMPLETE
	) {
		glBindFramebuffer(
			GL_FRAMEBUFFER,
			(GLuint)old_fbo
		);

		glDeleteFramebuffers(
			1,
			&fbo
		);

		glDeleteTextures(
			1,
			&texture
		);

		return false;
	}

	glViewport(
		0,
		0,
		width,
		height
	);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);

	bool external =
		source_target ==
		GL_TEXTURE_EXTERNAL_OES;

	GLuint program =
		external
			? pipeline->copy_prog_ext
			: pipeline->copy_prog_2d;

	GLint u_tex =
		external
			? pipeline->copy_tex_ext
			: pipeline->copy_tex_2d;

	glUseProgram(
		program
	);

	glUniform1i(
		u_tex,
		0
	);

	glActiveTexture(
		GL_TEXTURE0
	);

	glBindTexture(
		source_target,
		source_texture
	);

	glBindBuffer(
		GL_ARRAY_BUFFER,
		pipeline->mesh_vbo
	);

	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		3 * sizeof(GLfloat),
		(void *)0
	);

	glEnableVertexAttribArray(
		0
	);

	glDrawArrays(
		GL_TRIANGLES,
		0,
		pipeline->mesh_vertex_count
	);

	glDisableVertexAttribArray(
		0
	);

	glBindBuffer(
		GL_ARRAY_BUFFER,
		0
	);

	glBindTexture(
		source_target,
		0
	);

	glUseProgram(
		0
	);

	glBindFramebuffer(
		GL_FRAMEBUFFER,
		(GLuint)old_fbo
	);

	glViewport(
		old_viewport[0],
		old_viewport[1],
		old_viewport[2],
		old_viewport[3]
	);

	glDeleteFramebuffers(
		1,
		&fbo
	);

	*out_texture =
		texture;

	return true;
}

