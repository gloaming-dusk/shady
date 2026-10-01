#ifndef SHADY_GL_PIPELINE_H
#define SHADY_GL_PIPELINE_H

#include <GLES2/gl2.h>
#include <stdbool.h>
#include "../world/floor.h"
#include "../world/collider.h"
#include "../world/world.h"

struct wlr_renderer;

struct shady_gl_pipeline {
	GLuint prog_2d;
	GLuint prog_ext;

	GLint u_mvp_2d;
	GLint u_tex_2d;
	GLint u_tint_2d;
	GLint u_has_alpha_2d;
	GLint u_time_2d;
	GLint u_wobble_2d;
	GLint u_close_progress_2d;
	GLint u_model_2d;
	GLint u_light_dir_2d;
	GLint u_effect_strength_2d;
	GLint u_brightness_2d;

	GLint u_mvp_ext;
	GLint u_tex_ext;
	GLint u_tint_ext;
	GLint u_has_alpha_ext;
	GLint u_time_ext;
	GLint u_wobble_ext;
	GLint u_close_progress_ext;
	GLint u_model_ext;
	GLint u_light_dir_ext;
	GLint u_effect_strength_ext;
	GLint u_brightness_ext;

	/*
	 * Subdivided mesh used for wobbly and crumple deformation.
	 */
	GLuint mesh_vbo;
	GLsizei mesh_vertex_count;
	GLuint copy_prog_2d;
	GLuint copy_prog_ext;

	GLint copy_tex_2d;
	GLint copy_tex_ext;

	/* Solid side-wall pipeline for real window thickness. */
	GLuint side_prog;
	GLint side_u_mvp;
	GLint side_u_model;
	GLint side_u_light_dir;
	GLint side_u_base_color;
	GLint side_u_wobble;
	GLuint side_vbo;
	GLsizei side_vertex_count;

	/* Fullscreen atmospheric background. */
	GLuint background_prog;
	GLint background_u_top;
	GLint background_u_horizon;
	GLint background_u_bottom;

	/* World-space reference plane and perspective grid. */
	GLuint floor_prog;
	GLint floor_u_vp;
	GLint floor_u_base_color;
	GLint floor_u_grid_color;
	GLint floor_u_grid_strength;
	GLint floor_u_major_strength;
	GLint floor_u_fade_start;
	GLint floor_u_fade_end;
	GLuint floor_vbo;
	GLsizei floor_vertex_count;

	/* Projected window silhouettes on the horizontal floor. */
	GLuint shadow_prog;
	GLint shadow_u_vp;
	GLint shadow_u_model;
	GLint shadow_u_wobble;
	GLint shadow_u_softness;
	GLint shadow_u_opacity;
	GLint shadow_u_floor_bounds;

	/* FPS picking-ray debug overlay. */
	GLuint debug_prog;
	GLint debug_u_vp;
	GLint debug_u_color;
};

bool shady_gl_pipeline_init(
	struct shady_gl_pipeline *pipeline,
	struct wlr_renderer *renderer
);

void shady_gl_pipeline_fini(
	struct shady_gl_pipeline *pipeline
);

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
	float close_progress,
	const float tint[4],
	float effect_strength,
	float brightness
);

void shady_gl_pipeline_draw_sides(
	struct shady_gl_pipeline *pipeline,
	const float mvp[16],
	const float model[16],
	float wobble_x,
	float wobble_y
);

void shady_gl_pipeline_draw_background(
	struct shady_gl_pipeline *pipeline,
	const float top[3],
	const float horizon[3],
	const float bottom[3]
);

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
);

void shady_gl_pipeline_draw_shadow(
	struct shady_gl_pipeline *pipeline,
	const float vp[16],
	const float model[16],
	float wobble_x,
	float wobble_y,
	float height,
	const struct shady_floor *floor
);

void shady_gl_pipeline_draw_crosshair(
	struct shady_gl_pipeline *pipeline, bool target, bool holding
);

void shady_gl_pipeline_draw_debug_ray(
	struct shady_gl_pipeline *pipeline,
	const float vp[16], const float origin[3], const float end[3], bool hit
);
void shady_gl_pipeline_draw_debug_window_body(
	struct shady_gl_pipeline *pipeline, const float vp[16], const float model[16]
);
void shady_gl_pipeline_draw_debug_box(
	struct shady_gl_pipeline *pipeline, const float vp[16],
	const struct shady_box_collider *box, bool environment
);
void shady_gl_pipeline_draw_debug_triangle(
	struct shady_gl_pipeline *pipeline, const float vp[16],
	const struct shady_triangle_collider *triangle
);

bool shady_gl_pipeline_copy_texture(
	struct shady_gl_pipeline *pipeline,
	GLenum source_target,
	GLuint source_texture,
	int width,
	int height,
	GLuint *out_texture
);

#endif