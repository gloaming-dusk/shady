#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GLES2/gl2.h>
#include <wlr/util/log.h>

#include "../spatial/state.h"
#include "../../shady.h"

static const char *sky_vertex_shader =
	"attribute vec2 a_pos;\n"
	"varying vec2 v_uv;\n"
	"void main() {\n"
	"	v_uv = a_pos * 0.5 + 0.5;\n"
	"	gl_Position = vec4(a_pos, 0.0, 1.0);\n"
	"}\n";

/* Equirectangular lookup along the camera ray through each pixel. */
static const char *sky_fragment_shader =
	"precision mediump float;\n"
	"uniform sampler2D u_tex;\n"
	"uniform vec3 u_right, u_up, u_forward;\n"
	"uniform float u_aspect;\n"
	"varying vec2 v_uv;\n"
	"const float PI = 3.14159265359;\n"
	"void main() {\n"
	"	vec2 p = v_uv * 2.0 - 1.0;\n"
	"	p.x *= u_aspect;\n"
	"	vec3 d = normalize(vec3(p.x, -p.y, -2.145));\n"
	"	vec3 q = normalize(u_right * d.x + u_up * d.y + u_forward * (-d.z));\n"
	"	float u = atan(q.x, -q.z) / (2.0 * PI) + 0.5;\n"
	"	float v = asin(clamp(q.y, -1.0, 1.0)) / PI + 0.5;\n"
	"	gl_FragColor = texture2D(u_tex, vec2(u, 1.0 - v));\n"
	"}\n";

static GLuint sky_program, sky_vbo, sky_texture;
static GLint sky_u_tex, sky_u_right, sky_u_up, sky_u_forward, sky_u_aspect;
static char sky_loaded_path[SHADY_ENVIRONMENT_PATH_MAX];

static GLuint compile_shader(GLenum type, const char *source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = {0};
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		wlr_log(WLR_ERROR, "environment: shader compile failed: %s", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

GLuint shady_environment_compile_program(const char *vertex, const char *fragment,
		const char *const *attributes, size_t attribute_count) {
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment);
	if (!vs || !fs) {
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return 0;
	}
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	for (size_t i = 0; i < attribute_count; ++i)
		glBindAttribLocation(program, (GLuint)i, attributes[i]);
	glLinkProgram(program);
	glDeleteShader(vs);
	glDeleteShader(fs);
	GLint ok = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (!ok) {
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

bool shady_environment_sky_gl_init(void) {
	static const char *const attributes[] = { "a_pos" };
	sky_program = shady_environment_compile_program(sky_vertex_shader,
		sky_fragment_shader, attributes, 1);
	if (!sky_program) return false;
	sky_u_tex = glGetUniformLocation(sky_program, "u_tex");
	sky_u_right = glGetUniformLocation(sky_program, "u_right");
	sky_u_up = glGetUniformLocation(sky_program, "u_up");
	sky_u_forward = glGetUniformLocation(sky_program, "u_forward");
	sky_u_aspect = glGetUniformLocation(sky_program, "u_aspect");

	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1 };
	glGenBuffers(1, &sky_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, sky_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	return true;
}

void shady_environment_sky_gl_fini(void) {
	if (sky_texture) glDeleteTextures(1, &sky_texture);
	if (sky_vbo) glDeleteBuffers(1, &sky_vbo);
	if (sky_program) glDeleteProgram(sky_program);
	sky_texture = sky_vbo = sky_program = 0;
	sky_loaded_path[0] = '\0';
}

/* Binary PPM (P6, maxval 255) only. */
static bool load_ppm(const char *path) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		wlr_log(WLR_ERROR, "sky: cannot open %s", path);
		return false;
	}
	char magic[3] = {0};
	int w = 0, h = 0, max = 0;
	if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '6') {
		fclose(f);
		wlr_log(WLR_ERROR, "sky: %s is not a binary PPM (P6)", path);
		return false;
	}
	int c = fgetc(f);
	while (c == '#' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
		if (c == '#') while (c != '\n' && c != EOF) c = fgetc(f);
		c = fgetc(f);
	}
	ungetc(c, f);
	if (fscanf(f, "%d %d %d", &w, &h, &max) != 3 || w <= 0 || h <= 0 || max != 255) {
		fclose(f);
		wlr_log(WLR_ERROR, "sky: unsupported PPM header in %s", path);
		return false;
	}
	fgetc(f);
	size_t n = (size_t)w * (size_t)h * 3;
	unsigned char *pixels = malloc(n);
	if (!pixels || fread(pixels, 1, n, f) != n) {
		free(pixels);
		fclose(f);
		wlr_log(WLR_ERROR, "sky: truncated PPM %s", path);
		return false;
	}
	fclose(f);

	if (!sky_texture) glGenTextures(1, &sky_texture);
	glBindTexture(GL_TEXTURE_2D, sky_texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	/* RGB rows are 3*w bytes. GLES defaults to 4-byte unpack alignment, which
	 * corrupts PPMs whose row size is not divisible by four (e.g. 330px wide). */
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
	free(pixels);
	snprintf(sky_loaded_path, sizeof(sky_loaded_path), "%s", path);
	wlr_log(WLR_INFO, "sky: loaded %s (%dx%d)", path, w, h);
	return true;
}

void shady_environment_draw_sky(struct shady_server *server) {
	if (!sky_program || !server->config.sky || !server->config.sky_path[0]) return;
	if (strcmp(sky_loaded_path, server->config.sky_path)) {
		/* Remember failures too so a bad path is not re-read every frame. */
		if (!load_ppm(server->config.sky_path)) {
			snprintf(sky_loaded_path, sizeof(sky_loaded_path), "%s",
				server->config.sky_path);
			if (sky_texture) glDeleteTextures(1, &sky_texture);
			sky_texture = 0;
		}
	}
	if (!sky_texture) return;

	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);
	struct shady_vec3 right, up, forward;
	shady_camera_basis(&shady_spatial_state(server)->runtime.camera, &right, &up, &forward);

	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glUseProgram(sky_program);
	glUniform1i(sky_u_tex, 0);
	glUniform3f(sky_u_right, right.x, right.y, right.z);
	glUniform3f(sky_u_up, up.x, up.y, up.z);
	glUniform3f(sky_u_forward, forward.x, forward.y, forward.z);
	glUniform1f(sky_u_aspect, viewport[3] ? (float)viewport[2] / viewport[3] : 1.0f);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, sky_texture);
	glBindBuffer(GL_ARRAY_BUFFER, sky_vbo);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);
}
