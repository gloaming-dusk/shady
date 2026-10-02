#ifndef SHADY_PICK3D_H
#define SHADY_PICK3D_H

struct shady_server;
struct shady_toplevel;
struct wlr_surface;
struct wlr_output;

struct shady_toplevel *shady_toplevel_at_3d(struct shady_server *server,
	double lx, double ly, struct wlr_surface **surface, double *sx, double *sy);

/* Pick the nearest window along the first-person camera's center ray. */
struct shady_toplevel *shady_toplevel_at_camera_center(
	struct shady_server *server, float *distance_out);

/* Center-ray pick with the exact world-space shell hit point. */
struct shady_toplevel *shady_toplevel_at_camera_center_hit(
	struct shady_server *server, float *distance_out,
	float *hit_x, float *hit_y, float *hit_z);

/* Center-ray pick plus the output-local projection that produced the hit. */
struct shady_toplevel *shady_toplevel_at_camera_center_hit_output(
	struct shady_server *server, float *distance_out,
	float *hit_x, float *hit_y, float *hit_z,
	struct wlr_output **output_out);

#endif
