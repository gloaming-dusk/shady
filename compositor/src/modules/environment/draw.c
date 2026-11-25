#include "internal.h"

#include <stddef.h>

#include <GLES2/gl2.h>
#include <wlr/util/log.h>

#include "../../shady.h"

static const char *scene_vertex_shader =
	"attribute vec3 a_pos;\n"
	"attribute vec3 a_normal;\n"
	"attribute vec4 a_color;\n"
	"attribute vec2 a_uv;\n"
	"uniform mat4 u_vp;\n"
	"varying vec3 v_normal;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_uv;\n"
	"varying vec3 v_pos;\n"
	"void main() {\n"
	"	v_normal = a_normal;\n"
	"	v_color = a_color;\n"
	"	v_uv = a_uv;\n"
	"	v_pos = a_pos;\n"
	"	gl_Position = u_vp * vec4(a_pos, 1.0);\n"
	"	gl_Position.y = -gl_Position.y;\n"
	"}\n";

/* Colours are sRGB; light is added in linear light, as three.js does. The
 * colour's alpha scales light and shade (0: unlit). Toon: three bands of a
 * [150, 215, 255] ramp indexed by N.L * 0.5 + 0.5, plus a rim of light along
 * the silhouette. */
static const char *scene_fragment_shader =
	"precision mediump float;\n"
	"uniform vec3 u_light;\n"
	"uniform vec3 u_eye;\n"
	"uniform float u_toon;\n"
	"uniform sampler2D u_tex;\n"
	"varying vec3 v_normal;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_uv;\n"
	"varying vec3 v_pos;\n"
	"void main() {\n"
	"	vec4 t = texture2D(u_tex, vec2(v_uv.x, 1.0 - v_uv.y));\n"
	"	if (t.a < 0.3) discard;\n"
	"	vec3 base = v_color.rgb * t.rgb;\n"
	"	float shade = clamp(v_color.a, 0.0, 1.0);\n"
	"	float len = length(v_normal);\n"
	"	if (len < 1e-4 || shade <= 0.0) {\n"
	"		gl_FragColor = vec4(base, 1.0);\n"
	"		return;\n"
	"	}\n"
	"	vec3 n = v_normal / len;\n"
	"	float d = dot(n, normalize(u_light));\n"
	"	vec3 c;\n"
	"	if (u_toon > 0.5) {\n"
	"		float r = d * 0.5 + 0.5;\n"
	"		float band = mix(1.0, r < 0.3333 ? 0.588 : (r < 0.6667 ? 0.843 : 1.0), shade);\n"
	"		vec3 v = normalize(u_eye - v_pos);\n"
	"		float rim = smoothstep(0.45, 0.75, pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0)) * 0.22 * shade;\n"
	"		vec3 lin = pow(base, vec3(2.2)) * band + rim * vec3(1.0, 0.97, 1.0);\n"
	"		c = pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));\n"
	"	} else {\n"
	"		c = base * mix(1.0, 0.28 + 0.72 * max(d, 0.0), shade);\n"
	"	}\n"
	"	gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

/* Without per-vertex colours the whole mesh takes this one. */
static const GLfloat default_color[4] = { 0.18f, 0.28f, 0.38f, 1.0f };

static GLuint scene_program, scene_vbo, scene_color_vbo;
static GLuint scene_texture, white_texture;
static bool scene_has_colors, scene_has_texture;
static GLint scene_u_vp, scene_u_light, scene_u_eye, scene_u_toon, scene_u_tex;
static GLsizei scene_vertex_count;
/* Revisions of the environment scene and texture in GL; 0 = none. */
static uint64_t uploaded_revision, uploaded_texture_revision;

bool shady_environment_gl_init(void) {
	static const char *const attributes[] = { "a_pos", "a_normal", "a_color", "a_uv" };
	scene_program = shady_environment_compile_program(scene_vertex_shader,
		scene_fragment_shader, attributes, 4);
	if (!scene_program) return false;
	scene_u_vp = glGetUniformLocation(scene_program, "u_vp");
	scene_u_light = glGetUniformLocation(scene_program, "u_light");
	scene_u_eye = glGetUniformLocation(scene_program, "u_eye");
	scene_u_toon = glGetUniformLocation(scene_program, "u_toon");
	scene_u_tex = glGetUniformLocation(scene_program, "u_tex");

	/* Sampled when the scene has no texture: multiplies by one. */
	static const GLubyte white[4] = { 255, 255, 255, 255 };
	glGenTextures(1, &white_texture);
	glBindTexture(GL_TEXTURE_2D, white_texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindTexture(GL_TEXTURE_2D, 0);
	uploaded_revision = uploaded_texture_revision = 0;
	return shady_environment_sky_gl_init();
}

void shady_environment_gl_fini(void) {
	if (scene_vbo) glDeleteBuffers(1, &scene_vbo);
	if (scene_color_vbo) glDeleteBuffers(1, &scene_color_vbo);
	if (scene_texture) glDeleteTextures(1, &scene_texture);
	if (white_texture) glDeleteTextures(1, &white_texture);
	if (scene_program) glDeleteProgram(scene_program);
	scene_vbo = scene_color_vbo = scene_program = 0;
	scene_texture = white_texture = 0;
	scene_has_colors = scene_has_texture = false;
	scene_vertex_count = 0;
	uploaded_revision = uploaded_texture_revision = 0;
	shady_environment_sky_gl_fini();
}

static void upload_texture(const struct shady_environment *env) {
	uploaded_texture_revision = env->texture_revision;
	scene_has_texture = env->scene.texture != NULL;
	if (!scene_has_texture) {
		if (scene_texture) glDeleteTextures(1, &scene_texture);
		scene_texture = 0;
		return;
	}
	if (!scene_texture) glGenTextures(1, &scene_texture);
	glBindTexture(GL_TEXTURE_2D, scene_texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)env->scene.texture_width,
		(GLsizei)env->scene.texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, env->scene.texture);
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

static void upload(const struct shady_environment *env) {
	uploaded_revision = env->revision;
	scene_vertex_count = (GLsizei)env->scene.vertex_count;
	scene_has_colors = scene_vertex_count && env->scene.colors;
	if (uploaded_texture_revision != env->texture_revision) upload_texture(env);
	if (!scene_vertex_count) {
		if (scene_vbo) glDeleteBuffers(1, &scene_vbo);
		if (scene_color_vbo) glDeleteBuffers(1, &scene_color_vbo);
		scene_vbo = scene_color_vbo = 0;
		return;
	}
	/* A procedural scene (an animated one) is replaced every frame. */
	GLenum usage = env->source == SHADY_ENVIRONMENT_SOURCE_PROCEDURAL ?
		GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
	if (!scene_vbo) glGenBuffers(1, &scene_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
	glBufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)(env->scene.vertex_count * sizeof(*env->scene.vertices)),
		env->scene.vertices, usage);
	if (scene_has_colors) {
		if (!scene_color_vbo) glGenBuffers(1, &scene_color_vbo);
		glBindBuffer(GL_ARRAY_BUFFER, scene_color_vbo);
		glBufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(env->scene.vertex_count * sizeof(*env->scene.colors)),
			env->scene.colors, usage);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void shady_environment_draw_scene(struct shady_server *server, const float vp[16],
		const float eye[3]) {
	const struct shady_environment *env = server->environment;
	if (!env || !scene_program) return;
	if (uploaded_revision != env->revision) upload(env);
	if (!scene_vertex_count) return;

	const GLsizei stride = sizeof(struct shady_environment_vertex);
	const bool cull = env->scene.flags & SHADY_ENVIRONMENT_SCENE_CULL_BACK_FACES;
	glUseProgram(scene_program);
	glUniformMatrix4fv(scene_u_vp, 1, GL_FALSE, vp);
	glUniform3f(scene_u_light, -0.45f, 0.72f, 0.53f);
	glUniform3f(scene_u_eye, eye[0], eye[1], eye[2]);
	glUniform1f(scene_u_toon, env->scene.flags & SHADY_ENVIRONMENT_SCENE_TOON ? 1.0f : 0.0f);
	glUniform1i(scene_u_tex, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, scene_has_texture ? scene_texture : white_texture);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	if (cull) {
		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		/* The vertex shader flips y, which turns counter-clockwise faces
		 * clockwise on screen. */
		glFrontFace(GL_CW);
	}
	glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
		(void *)offsetof(struct shady_environment_vertex, position));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
		(void *)offsetof(struct shady_environment_vertex, normal));
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride,
		(void *)offsetof(struct shady_environment_vertex, uv));
	glEnableVertexAttribArray(3);
	if (scene_has_colors) {
		glBindBuffer(GL_ARRAY_BUFFER, scene_color_vbo);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
			sizeof(struct shady_environment_color), (void *)0);
		glEnableVertexAttribArray(2);
	} else {
		glVertexAttrib4fv(2, default_color);
	}
	glDrawArrays(GL_TRIANGLES, 0, scene_vertex_count);
	if (scene_has_colors) glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(3);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	if (cull) {
		glDisable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
	}
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}
