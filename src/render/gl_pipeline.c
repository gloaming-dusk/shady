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

static const char *SIDE_VERT =
	"attribute vec3 a_pos;\n"
	"attribute vec3 a_normal;\n"
	"uniform mat4 u_mvp;\n"
	"uniform mat4 u_model;\n"
	"uniform vec2 u_wobble;\n"
	"varying vec3 v_normal;\n"
	"void main() {\n"
	"    vec3 pos = a_pos;\n"
	"    float bend_x = sin(pos.y * 3.14159265);\n"
	"    float bend_y = sin(pos.x * 3.14159265);\n"
	"    float cx = pos.x - 0.5;\n"
	"    float cy = pos.y - 0.5;\n"
	"    pos.x += u_wobble.x * bend_x * (0.75 + 0.25 * cos(cy * 3.14159265));\n"
	"    pos.y += u_wobble.y * bend_y * (0.75 + 0.25 * cos(cx * 3.14159265));\n"
	"    pos.x += u_wobble.y * cy * 0.18 * bend_y;\n"
	"    pos.y += u_wobble.x * cx * 0.18 * bend_x;\n"
	"    float depth_shape = sin(pos.x * 3.14159265) * sin(pos.y * 3.14159265);\n"
	"    pos.z += (u_wobble.x * cy - u_wobble.y * cx) * 0.65 * depth_shape;\n"
	"    gl_Position = u_mvp * vec4(pos, 1.0);\n"
	"    gl_Position.y = -gl_Position.y;\n"
	"    v_normal = normalize(mat3(u_model) * a_normal);\n"
	"}\n";

static const char *SIDE_FRAG =
	"precision mediump float;\n"
	"uniform vec3 u_light_dir;\n"
	"uniform vec4 u_base_color;\n"
	"uniform vec2 u_wobble;\n"
	"varying vec3 v_normal;\n"
	"void main() {\n"
	"    vec3 n = normalize(v_normal);\n"
	"    vec3 l = normalize(u_light_dir);\n"
	"    float diffuse = max(dot(n, l), 0.0);\n"
	"    float rim = pow(1.0 - abs(n.z), 2.0);\n"
	"    float light = 0.24 + diffuse * 0.76;\n"
	"    vec3 color = u_base_color.rgb * light;\n"
	"    color += vec3(0.035, 0.075, 0.13) * rim;\n"
	"    gl_FragColor = vec4(color, u_base_color.a);\n"
	"}\n";

static const char *SHADOW_VERT =
	"attribute vec3 a_pos;\n"
	"uniform mat4 u_vp;\n"
	"uniform mat4 u_model;\n"
	"uniform vec2 u_wobble;\n"
	"uniform float u_softness;\n"
	"uniform vec4 u_floor_bounds;\n"
	"varying vec2 v_uv;\n"
	"varying vec2 v_shadow_xz;\n"
	"void main() {\n"
	"    vec2 uv = a_pos.xy;\n"
	"    vec3 pos = a_pos;\n"
	"    float cx = uv.x - 0.5;\n"
	"    float cy = uv.y - 0.5;\n"
	"    float bx = sin(uv.y * 3.14159265);\n"
	"    float by = sin(uv.x * 3.14159265);\n"
	"    pos.x += u_wobble.x * bx * (0.75 + 0.25 * cos(cy * 3.14159265));\n"
	"    pos.y += u_wobble.y * by * (0.75 + 0.25 * cos(cx * 3.14159265));\n"
	"    pos.x += u_wobble.y * cy * 0.18 * by;\n"
	"    pos.y += u_wobble.x * cx * 0.18 * bx;\n"
	"    float ds = sin(uv.x * 3.14159265) * sin(uv.y * 3.14159265);\n"
	"    pos.z += (u_wobble.x * cy - u_wobble.y * cx) * 0.65 * ds;\n"
	"    vec3 world = (u_model * vec4(pos, 1.0)).xyz;\n"
	"    vec3 light = normalize(vec3(-0.45, 0.72, 0.53));\n"
	"    float t = (-0.618 - world.y) / -light.y;\n"
	"    vec3 projected = world + light * t;\n"
	"    projected.y = -0.618;\n"
	"    gl_Position = u_vp * vec4(projected, 1.0);\n"
	"    gl_Position.y = -gl_Position.y;\n"
	"    v_uv = uv;\n"
	"    v_shadow_xz = projected.xz;\n"
	"}\n";

static const char *SHADOW_FRAG =
	"precision mediump float;\n"
	"uniform float u_softness;\n"
	"uniform float u_opacity;\n"
	"uniform vec4 u_floor_bounds;\n"
	"varying vec2 v_uv;\n"
	"varying vec2 v_shadow_xz;\n"
	"void main() {\n"
	"    if (v_shadow_xz.x < u_floor_bounds.x || v_shadow_xz.x > u_floor_bounds.y || v_shadow_xz.y < u_floor_bounds.z || v_shadow_xz.y > u_floor_bounds.w) discard;\n"
	"    float edge = min(min(v_uv.x, 1.0-v_uv.x), min(v_uv.y, 1.0-v_uv.y));\n"
	"    float feather = smoothstep(0.0, u_softness, edge);\n"
	"    float core = smoothstep(0.0, u_softness * 2.2, edge);\n"
	"    float alpha = u_opacity * mix(0.48, 1.0, core) * feather;\n"
	"    gl_FragColor = vec4(0.0, 0.0, 0.0, alpha);\n"
	"}\n";

static const char *BACKGROUND_VERT =
	"attribute vec2 a_pos;\n"
	"varying float v_y;\n"
	"void main() {\n"
	"    v_y = a_pos.y * 0.5 + 0.5;\n"
	"    gl_Position = vec4(a_pos, 0.999, 1.0);\n"
	"}\n";

static const char *BACKGROUND_FRAG =
	"precision mediump float;\n"
	"uniform vec3 u_top;\n"
	"uniform vec3 u_horizon;\n"
	"uniform vec3 u_bottom;\n"
	"varying float v_y;\n"
	"void main() {\n"
	"    float horizon = smoothstep(0.18, 0.58, v_y);\n"
	"    vec3 lower = mix(u_bottom, u_horizon, horizon);\n"
	"    float upper_mix = smoothstep(0.48, 1.0, v_y);\n"
	"    vec3 color = mix(lower, u_top, upper_mix);\n"
	"    gl_FragColor = vec4(color, 1.0);\n"
	"}\n";

static const char *FLOOR_VERT =
	"attribute vec3 a_pos;\n"
	"uniform mat4 u_vp;\n"
	"varying vec2 v_world;\n"
	"void main() {\n"
	"    v_world = a_pos.xz;\n"
	"    gl_Position = u_vp * vec4(a_pos, 1.0);\n"
	"    gl_Position.y = -gl_Position.y;\n"
	"}\n";

static const char *FLOOR_FRAG =
	"#extension GL_OES_standard_derivatives : enable\n"
	"precision mediump float;\n"
	"varying vec2 v_world;\n"
	"uniform vec3 u_base_color;\n"
	"uniform vec3 u_grid_color;\n"
	"uniform float u_grid_strength;\n"
	"uniform float u_major_strength;\n"
	"uniform float u_fade_start;\n"
	"uniform float u_fade_end;\n"
	"void main() {\n"
	"    vec2 g = abs(fract(v_world * 10.0 - 0.5) - 0.5) / max(fwidth(v_world * 10.0), vec2(0.0001));\n"
	"    float line = 1.0 - min(min(g.x, g.y), 1.0);\n"
	"    vec2 major_g = abs(fract(v_world * 2.0 - 0.5) - 0.5) / max(fwidth(v_world * 2.0), vec2(0.0001));\n"
	"    float major = 1.0 - min(min(major_g.x, major_g.y), 1.0);\n"
	"    float fade = 1.0 - smoothstep(u_fade_start, max(u_fade_end, u_fade_start + 0.001), length(v_world));\n"
	"    vec3 grid = u_grid_color * (line * u_grid_strength + major * u_major_strength);\n"
	"    gl_FragColor = vec4(u_base_color + grid * fade, 1.0);\n"
	"}\n";

static const char *DEBUG_VERT =
	"attribute vec3 a_pos;\n"
	"uniform mat4 u_vp;\n"
	"void main() { gl_Position = u_vp * vec4(a_pos, 1.0); gl_Position.y = -gl_Position.y; }\n";

static const char *DEBUG_FRAG =
	"precision mediump float;\n"
	"uniform vec4 u_color;\n"
	"void main() { gl_FragColor = u_color; }\n";

static const char *COPY_VERT =
	"attribute vec3 a_pos;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"    v_uv = a_pos.xy;\n"
	"    gl_Position = vec4(\n"
	"        a_pos.x * 2.0 - 1.0,\n"
	"        a_pos.y * 2.0 - 1.0,\n"
	"        0.0,\n"
	"        1.0\n"
	"    );\n"
	"}\n";

static const char *COPY_FRAG_2D =
	"precision mediump float;\n"
	"uniform sampler2D u_tex;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(u_tex, v_uv);\n"
	"}\n";

static const char *COPY_FRAG_EXT =
	"#extension GL_OES_EGL_image_external : require\n"
	"precision mediump float;\n"
	"uniform samplerExternalOES u_tex;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"    gl_FragColor = texture2D(u_tex, v_uv);\n"
	"}\n";

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

	pipeline->u_close_progress_2d =
		glGetUniformLocation(pipeline->prog_2d, "u_close_progress");
	pipeline->u_model_2d = glGetUniformLocation(pipeline->prog_2d, "u_model");
	pipeline->u_light_dir_2d = glGetUniformLocation(pipeline->prog_2d, "u_light_dir");
	pipeline->u_effect_strength_2d = glGetUniformLocation(pipeline->prog_2d, "u_effect_strength");
	pipeline->u_brightness_2d = glGetUniformLocation(pipeline->prog_2d, "u_brightness");

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

	pipeline->u_close_progress_ext =
		glGetUniformLocation(pipeline->prog_ext, "u_close_progress");
	pipeline->u_model_ext = glGetUniformLocation(pipeline->prog_ext, "u_model");
	pipeline->u_light_dir_ext = glGetUniformLocation(pipeline->prog_ext, "u_light_dir");
	pipeline->u_effect_strength_ext = glGetUniformLocation(pipeline->prog_ext, "u_effect_strength");
	pipeline->u_brightness_ext = glGetUniformLocation(pipeline->prog_ext, "u_brightness");

	pipeline->copy_prog_2d =
	link_program(
		COPY_VERT,
		COPY_FRAG_2D,
		"snapshot copy 2D"
	);

	pipeline->copy_prog_ext =
		link_program(
			COPY_VERT,
			COPY_FRAG_EXT,
			"snapshot copy external"
		);

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

	pipeline->side_prog = link_program(SIDE_VERT, SIDE_FRAG, "window sides");
	if (!pipeline->side_prog || !create_side_mesh(pipeline)) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->side_u_mvp = glGetUniformLocation(pipeline->side_prog, "u_mvp");
	pipeline->side_u_model = glGetUniformLocation(pipeline->side_prog, "u_model");
	pipeline->side_u_light_dir = glGetUniformLocation(pipeline->side_prog, "u_light_dir");
	pipeline->side_u_base_color = glGetUniformLocation(pipeline->side_prog, "u_base_color");
	pipeline->side_u_wobble = glGetUniformLocation(pipeline->side_prog, "u_wobble");

	pipeline->background_prog = link_program(BACKGROUND_VERT, BACKGROUND_FRAG, "background gradient");
	if (!pipeline->background_prog) {
		shady_gl_pipeline_fini(pipeline);
		return false;
	}
	pipeline->background_u_top = glGetUniformLocation(pipeline->background_prog, "u_top");
	pipeline->background_u_horizon = glGetUniformLocation(pipeline->background_prog, "u_horizon");
	pipeline->background_u_bottom = glGetUniformLocation(pipeline->background_prog, "u_bottom");

	pipeline->floor_prog = link_program(FLOOR_VERT, FLOOR_FRAG, "3D floor");
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

	pipeline->shadow_prog = link_program(SHADOW_VERT, SHADOW_FRAG, "window shadow");
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

	pipeline->debug_prog = link_program(DEBUG_VERT, DEBUG_FRAG, "debug ray");
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
		glDeleteBuffers(
			1,
			&pipeline->mesh_vbo
		);

		pipeline->mesh_vbo = 0;
		pipeline->mesh_vertex_count = 0;
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
	float time_seconds,
	float wobble_x,
	float wobble_y,
	const float water[4],
	float close_progress,
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
	GLint u_close_progress = external ? pipeline->u_close_progress_ext : pipeline->u_close_progress_2d;
	GLint u_model = external ? pipeline->u_model_ext : pipeline->u_model_2d;
	GLint u_light_dir = external ? pipeline->u_light_dir_ext : pipeline->u_light_dir_2d;
	GLint u_effect_strength = external ? pipeline->u_effect_strength_ext : pipeline->u_effect_strength_2d;
	GLint u_brightness = external ? pipeline->u_brightness_ext : pipeline->u_brightness_2d;

	glUseProgram(prog);

	glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
	glUniformMatrix4fv(u_model, 1, GL_FALSE, model);
	glUniform3f(u_light_dir, -0.45f, 0.72f, 0.53f);
	glUniform1f(u_effect_strength, effect_strength);
	glUniform1f(u_brightness, brightness);

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

	glUniform1f(
		u_close_progress,
		close_progress
	);

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

	if (has_alpha) {
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

	glVertexAttribPointer(
		0,
		3,
		GL_FLOAT,
		GL_FALSE,
		3 * sizeof(GLfloat),
		(void *)0
	);

	glEnableVertexAttribArray(0);

	glDrawArrays(
		GL_TRIANGLES,
		0,
		pipeline->mesh_vertex_count
	);

	glDisableVertexAttribArray(0);

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

