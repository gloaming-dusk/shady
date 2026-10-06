#ifndef SHADY_MATH3D_H
#define SHADY_MATH3D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct shady_camera {
	float yaw;      /* radians, around +Y */
	float pitch;    /* radians, clamped */
	float distance; /* from target along orbit */
	float target_x; /* look-at / orbit center (world) */
	float target_y;
	float target_z;

	/* First-person mode: eye position + velocity in world space. */
	bool first_person;
	float pos_x, pos_y, pos_z;
	float vel_y;
	bool grounded;
};

struct shady_vec3 {
	float x, y, z;
};

struct shady_ray {
	struct shady_vec3 origin;
	struct shady_vec3 dir;
};

#define SHADY_CAMERA_FOV_Y 0.872664625997f /* ~50 degrees */
#define SHADY_CAMERA_NEAR 0.05f
#define SHADY_CAMERA_FAR 100.0f
/* Distance so a full-output-height plane (~1 world unit) fills the FOV. */
#define SHADY_CAMERA_DEFAULT_DIST 1.07215f

void shady_camera_reset(struct shady_camera *cam);

/* Camera basis in world space (for panning). */
void shady_camera_basis(const struct shady_camera *cam,
	struct shady_vec3 *right, struct shady_vec3 *up, struct shady_vec3 *forward);

/* Column-major 4x4, OpenGL convention. */
void shady_mat4_identity(float m[16]);
void shady_mat4_multiply(float out[16], const float a[16], const float b[16]);
bool shady_mat4_invert(float out[16], const float m[16]);
void shady_mat4_perspective(float out[16], float fovy_rad, float aspect,
	float znear, float zfar);
void shady_mat4_look_at(float out[16],
	struct shady_vec3 eye, struct shady_vec3 center, struct shady_vec3 up);
void shady_mat4_translate(float out[16], float x, float y, float z);
void shady_mat4_scale(float out[16], float x, float y, float z);
void shady_mat4_rotate_x(float out[16], float radians);
void shady_mat4_rotate_y(float out[16], float radians);

void shady_camera_view(const struct shady_camera *cam, float view[16]);
void shady_camera_eye(const struct shady_camera *cam, struct shady_vec3 *eye);

/*
 * Map layout pixel rect (top-left origin) into a model matrix for a unit quad
 * on z=0. World: origin at output center, +Y up, 1 unit = output height px.
 */
void shady_window_box_model(float model[16], float center_x,float center_y,float center_z,
	float width,float height,float depth,float tilt_x,float tilt_y);
void shady_window_cube_model(float model[16], float center_x,float center_y,float center_z,
	float size,float tilt_x,float tilt_y);
void shady_window_model(float model[16],
	float layout_x, float layout_y, float width_px, float height_px,
	float output_w, float output_h, float z, float tilt_x, float tilt_y);

/* NDC (x,y in [-1,1], y up) → world ray for this view*proj. */
void shady_ray_from_ndc(struct shady_ray *ray,
	float ndc_x, float ndc_y, const float view[16], const float proj[16]);

/*
 * Ray vs unit quad transformed by model (corners (0,0,0)-(1,0,0)-(0,1,0)-(1,1,0)).
 * On hit: t > 0, uv in [0,1]^2 (v grows with layout +Y / texture +V).
 */
bool shady_ray_quad_hit(const struct shady_ray *ray, const float model[16],
	float *t_out, float *u_out, float *v_out);

/* Ray vs the same 16x16 wobble-deformed front mesh used by the renderer. */
bool shady_ray_wobble_hit(const struct shady_ray *ray, const float model[16],
	float wobble_x, float wobble_y, float *t_out, float *u_out, float *v_out);

/* Ray vs the complete extruded window shell: front, back and four sides. */
bool shady_ray_window_shell_hit(const struct shady_ray *ray, const float model[16],
	float wobble_x, float wobble_y, float *t_out, float *u_out, float *v_out,
	bool *front_out);
bool shady_ray_mesh_hit(const struct shady_ray *ray, const float model[16],
	const float *vertices, size_t vertex_count,
	const uint16_t *indices, size_t index_count,
	float *t_out, float *u_out, float *v_out);
bool shady_mesh_bounds(const float model[16], const float *vertices,
	size_t vertex_count, const uint16_t *indices, size_t index_count,
	float center[3], float half[3]);

#endif
