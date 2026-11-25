#include "internal.h"

#include <stddef.h>

#include <GLES2/gl2.h>
#include <wlr/util/log.h>

#include "../../shady.h"

static const char *scene_vertex_shader =
	"attribute vec3 a_pos;\n"
	"attribute vec3 a_normal;\n"
	"attribute vec4 a_color;\n"
	"uniform mat4 u_vp;\n"
	"varying vec3 v_normal;\n"
	"varying vec3 v_color;\n"
	"void main() {\n"
	"	v_normal = a_normal;\n"
	"	v_color = a_color.rgb;\n"
	"	gl_Position = u_vp * vec4(a_pos, 1.0);\n"
	"	gl_Position.y = -gl_Position.y;\n"
	"}\n";

static const char *scene_fragment_shader =
	"precision mediump float;\n"
	"uniform vec3 u_light;\n"
	"varying vec3 v_normal;\n"
	"varying vec3 v_color;\n"
	"void main() {\n"
	"	float d = max(dot(normalize(v_normal), normalize(u_light)), 0.0);\n"
	"	vec3 c = v_color * (0.28 + 0.72 * d);\n"
	"	gl_FragColor = vec4(c, 1.0);\n"
	"}\n";

/* Without per-vertex colours the whole mesh takes this one. */
static const GLfloat default_color[4] = { 0.18f, 0.28f, 0.38f, 1.0f };

static GLuint scene_program, scene_vbo, scene_color_vbo;
static bool scene_has_colors;
static GLint scene_u_vp, scene_u_light;
static GLsizei scene_vertex_count;
/* Revision of the environment scene currently in scene_vbo; 0 = none. */
static uint64_t uploaded_revision;

bool shady_environment_gl_init(void) {
	static const char *const attributes[] = { "a_pos", "a_normal", "a_color" };
	scene_program = shady_environment_compile_program(scene_vertex_shader,
		scene_fragment_shader, attributes, 3);
	if (!scene_program) return false;
	scene_u_vp = glGetUniformLocation(scene_program, "u_vp");
	scene_u_light = glGetUniformLocation(scene_program, "u_light");
	uploaded_revision = 0;
	return shady_environment_sky_gl_init();
}

void shady_environment_gl_fini(void) {
	if (scene_vbo) glDeleteBuffers(1, &scene_vbo);
	if (scene_color_vbo) glDeleteBuffers(1, &scene_color_vbo);
	if (scene_program) glDeleteProgram(scene_program);
	scene_vbo = scene_color_vbo = scene_program = 0;
	scene_has_colors = false;
	scene_vertex_count = 0;
	uploaded_revision = 0;
	shady_environment_sky_gl_fini();
}

static void upload(const struct shady_environment *env) {
	uploaded_revision = env->revision;
	scene_vertex_count = (GLsizei)env->scene.vertex_count;
	scene_has_colors = scene_vertex_count && env->scene.colors;
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

void shady_environment_draw_scene(struct shady_server *server, const float vp[16]) {
	const struct shady_environment *env = server->environment;
	if (!env || !scene_program) return;
	if (uploaded_revision != env->revision) upload(env);
	if (!scene_vertex_count) return;

	const GLsizei stride = sizeof(struct shady_environment_vertex);
	glUseProgram(scene_program);
	glUniformMatrix4fv(scene_u_vp, 1, GL_FALSE, vp);
	glUniform3f(scene_u_light, -0.45f, 0.72f, 0.53f);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);
	glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
		(void *)offsetof(struct shady_environment_vertex, position));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
		(void *)offsetof(struct shady_environment_vertex, normal));
	glEnableVertexAttribArray(1);
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
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(0);
}
