#include "math3d.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void shady_camera_reset(struct shady_camera *cam) {
	/* Frontal view: desktop looks like normal 2D; orbit/pan moves the camera. */
	cam->yaw = 0.f;
	cam->pitch = 0.f;
	cam->distance = SHADY_CAMERA_DEFAULT_DIST;
	cam->target_x = 0.f;
	cam->target_y = 0.f;
	cam->target_z = 0.f;
	cam->first_person = false;
	cam->pos_x = 0.f;
	cam->pos_y = -0.22f;
	cam->pos_z = SHADY_CAMERA_DEFAULT_DIST;
	cam->vel_y = 0.f;
	cam->grounded = false;
}

void shady_mat4_identity(float m[16]) {
	memset(m, 0, sizeof(float) * 16);
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void shady_mat4_multiply(float out[16], const float a[16], const float b[16]) {
	float r[16];
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			r[col * 4 + row] =
				a[0 * 4 + row] * b[col * 4 + 0] +
				a[1 * 4 + row] * b[col * 4 + 1] +
				a[2 * 4 + row] * b[col * 4 + 2] +
				a[3 * 4 + row] * b[col * 4 + 3];
		}
	}
	memcpy(out, r, sizeof(r));
}

bool shady_mat4_invert(float out[16], const float m[16]) {
	float inv[16];
	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
		m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
		m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
		m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
		m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
		m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
		m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
		m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
		m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
		m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
		m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
		m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
		m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
		m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
		m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
		m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
		m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf(det) < 1e-8f) {
		return false;
	}
	det = 1.0f / det;
	for (int i = 0; i < 16; i++) {
		out[i] = inv[i] * det;
	}
	return true;
}

void shady_mat4_perspective(float out[16], float fovy_rad, float aspect,
		float znear, float zfar) {
	float f = 1.0f / tanf(fovy_rad * 0.5f);
	memset(out, 0, sizeof(float) * 16);
	out[0] = f / aspect;
	out[5] = f;
	out[10] = (zfar + znear) / (znear - zfar);
	out[11] = -1.0f;
	out[14] = (2.0f * zfar * znear) / (znear - zfar);
}

void shady_mat4_look_at(float out[16],
		struct shady_vec3 eye, struct shady_vec3 center, struct shady_vec3 up) {
	struct shady_vec3 f = {
		center.x - eye.x,
		center.y - eye.y,
		center.z - eye.z,
	};
	float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
	f.x /= fl; f.y /= fl; f.z /= fl;

	struct shady_vec3 s = {
		f.y * up.z - f.z * up.y,
		f.z * up.x - f.x * up.z,
		f.x * up.y - f.y * up.x,
	};
	float sl = sqrtf(s.x * s.x + s.y * s.y + s.z * s.z);
	s.x /= sl; s.y /= sl; s.z /= sl;

	struct shady_vec3 u = {
		s.y * f.z - s.z * f.y,
		s.z * f.x - s.x * f.z,
		s.x * f.y - s.y * f.x,
	};

	shady_mat4_identity(out);
	out[0] = s.x; out[4] = s.y; out[8] = s.z;
	out[1] = u.x; out[5] = u.y; out[9] = u.z;
	out[2] = -f.x; out[6] = -f.y; out[10] = -f.z;
	out[12] = -(s.x * eye.x + s.y * eye.y + s.z * eye.z);
	out[13] = -(u.x * eye.x + u.y * eye.y + u.z * eye.z);
	out[14] = f.x * eye.x + f.y * eye.y + f.z * eye.z;
}

void shady_mat4_translate(float out[16], float x, float y, float z) {
	shady_mat4_identity(out);
	out[12] = x;
	out[13] = y;
	out[14] = z;
}

void shady_mat4_scale(float out[16], float x, float y, float z) {
	shady_mat4_identity(out);
	out[0] = x;
	out[5] = y;
	out[10] = z;
}

void shady_mat4_rotate_x(float out[16], float radians) {
	float c = cosf(radians);
	float s = sinf(radians);
	shady_mat4_identity(out);
	out[5] = c;
	out[6] = s;
	out[9] = -s;
	out[10] = c;
}

void shady_mat4_rotate_y(float out[16], float radians) {
	float c = cosf(radians);
	float s = sinf(radians);
	shady_mat4_identity(out);
	out[0] = c;
	out[2] = -s;
	out[8] = s;
	out[10] = c;
}

void shady_camera_eye(const struct shady_camera *cam, struct shady_vec3 *eye) {
	if (cam->first_person) {
		*eye = (struct shady_vec3){ cam->pos_x, cam->pos_y, cam->pos_z };
		return;
	}
	float cp = cosf(cam->pitch);
	float sp = sinf(cam->pitch);
	float cy = cosf(cam->yaw);
	float sy = sinf(cam->yaw);
	eye->x = cam->target_x + cam->distance * cp * sy;
	eye->y = cam->target_y + cam->distance * sp;
	eye->z = cam->target_z + cam->distance * cp * cy;
}

void shady_camera_basis(const struct shady_camera *cam,
		struct shady_vec3 *right, struct shady_vec3 *up, struct shady_vec3 *forward) {
	struct shady_vec3 eye;
	shady_camera_eye(cam, &eye);
	struct shady_vec3 f;
	if (cam->first_person) {
		float cp = cosf(cam->pitch);
		f = (struct shady_vec3){
			-sinf(cam->yaw) * cp,
			sinf(cam->pitch),
			-cosf(cam->yaw) * cp,
		};
	} else {
		f = (struct shady_vec3){
			cam->target_x - eye.x,
			cam->target_y - eye.y,
			cam->target_z - eye.z,
		};
	}
	float fl = sqrtf(f.x * f.x + f.y * f.y + f.z * f.z);
	if (fl < 1e-8f) {
		f = (struct shady_vec3){ 0.f, 0.f, -1.f };
	} else {
		f.x /= fl; f.y /= fl; f.z /= fl;
	}
	struct shady_vec3 world_up = { 0.f, 1.f, 0.f };
	struct shady_vec3 r = {
		f.y * world_up.z - f.z * world_up.y,
		f.z * world_up.x - f.x * world_up.z,
		f.x * world_up.y - f.y * world_up.x,
	};
	float rl = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
	if (rl < 1e-8f) {
		r = (struct shady_vec3){ 1.f, 0.f, 0.f };
	} else {
		r.x /= rl; r.y /= rl; r.z /= rl;
	}
	struct shady_vec3 u = {
		r.y * f.z - r.z * f.y,
		r.z * f.x - r.x * f.z,
		r.x * f.y - r.y * f.x,
	};
	if (right) {
		*right = r;
	}
	if (up) {
		*up = u;
	}
	if (forward) {
		*forward = f;
	}
}

void shady_camera_view(const struct shady_camera *cam, float view[16]) {
	struct shady_vec3 eye;
	shady_camera_eye(cam, &eye);
	struct shady_vec3 center;
	if (cam->first_person) {
		float cp = cosf(cam->pitch);
		center = (struct shady_vec3){
			eye.x - sinf(cam->yaw) * cp,
			eye.y + sinf(cam->pitch),
			eye.z - cosf(cam->yaw) * cp,
		};
	} else {
		center = (struct shady_vec3){ cam->target_x, cam->target_y, cam->target_z };
	}
	struct shady_vec3 up = { 0.f, 1.f, 0.f };
	shady_mat4_look_at(view, eye, center, up);
}

void shady_window_box_model(float model[16],float x,float y,float z,
		float width,float height,float depth,float tx,float ty){
	float t[16],rx[16],ry[16],r[16],s[16],a[16];
	shady_mat4_translate(t,x,y,z);shady_mat4_rotate_x(rx,tx);shady_mat4_rotate_y(ry,ty);
	shady_mat4_multiply(r,ry,rx);shady_mat4_scale(s,width,height,depth);
	shady_mat4_multiply(a,t,r);shady_mat4_multiply(model,a,s);
	/* Window shell geometry spans x/y=[0,1] and z=[-1,0]. Keep its local
	 * centre (0.5,0.5,-0.5) on the representation's world-space centre. */
	model[12]+=-.5f*model[0]-.5f*model[4]+.5f*model[8];
	model[13]+=-.5f*model[1]-.5f*model[5]+.5f*model[9];
	model[14]+=-.5f*model[2]-.5f*model[6]+.5f*model[10];
}

void shady_window_cube_model(float model[16],float x,float y,float z,float size,float tx,float ty){
	shady_window_box_model(model,x,y,z,size,size,size,tx,ty);
}

void shady_window_model(float model[16],
		float layout_x, float layout_y, float width_px, float height_px,
		float output_w, float output_h, float z, float tilt_x, float tilt_y) {
	float inv_h = 1.0f / output_h;
	float bl_x = (layout_x - output_w * 0.5f) * inv_h;
	float bl_y = (output_h * 0.5f - (layout_y + height_px)) * inv_h;
	float sx = width_px * inv_h;
	float sy = height_px * inv_h;

	/*
	 * Rotate around the visual centre of the front face. Z scale is a
	 * constant world-space thickness (~14 logical pixels at 1080p).
	 */
	float t[16], pivot[16], unpivot[16], rx[16], ry[16], r[16], s[16];
	float a[16], b[16], c[16];

	shady_mat4_translate(t, bl_x, bl_y, z);
	shady_mat4_translate(pivot, sx * 0.5f, sy * 0.5f, 0.f);
	shady_mat4_translate(unpivot, -0.5f, -0.5f, 0.f);
	shady_mat4_rotate_x(rx, tilt_x);
	shady_mat4_rotate_y(ry, tilt_y);
	shady_mat4_multiply(r, ry, rx);
	shady_mat4_scale(s, sx, sy, 14.0f * inv_h);

	shady_mat4_multiply(a, t, pivot);
	shady_mat4_multiply(b, a, r);
	shady_mat4_multiply(c, b, s);
	shady_mat4_multiply(model, c, unpivot);
}

static void mat4_mul_vec4(const float m[16], const float v[4], float out[4]) {
	out[0] = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3];
	out[1] = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3];
	out[2] = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3];
	out[3] = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3];
}

void shady_ray_from_ndc(struct shady_ray *ray,
		float ndc_x, float ndc_y, const float view[16], const float proj[16]) {
	float vp[16], inv[16];
	shady_mat4_multiply(vp, proj, view);
	if (!shady_mat4_invert(inv, vp)) {
		ray->origin = (struct shady_vec3){ 0, 0, 0 };
		ray->dir = (struct shady_vec3){ 0, 0, -1 };
		return;
	}

	float near_h[4] = { ndc_x, ndc_y, -1.f, 1.f };
	float far_h[4] = { ndc_x, ndc_y, 1.f, 1.f };
	float near_w[4], far_w[4];
	mat4_mul_vec4(inv, near_h, near_w);
	mat4_mul_vec4(inv, far_h, far_w);
	near_w[0] /= near_w[3]; near_w[1] /= near_w[3]; near_w[2] /= near_w[3];
	far_w[0] /= far_w[3]; far_w[1] /= far_w[3]; far_w[2] /= far_w[3];

	ray->origin.x = near_w[0];
	ray->origin.y = near_w[1];
	ray->origin.z = near_w[2];
	ray->dir.x = far_w[0] - near_w[0];
	ray->dir.y = far_w[1] - near_w[1];
	ray->dir.z = far_w[2] - near_w[2];
	float len = sqrtf(ray->dir.x * ray->dir.x + ray->dir.y * ray->dir.y +
		ray->dir.z * ray->dir.z);
	if (len > 1e-8f) {
		ray->dir.x /= len;
		ray->dir.y /= len;
		ray->dir.z /= len;
	}
}

static struct shady_vec3 transform_point(const float m[16], float x, float y, float z) {
	float v[4] = { x, y, z, 1.f };
	float o[4];
	mat4_mul_vec4(m, v, o);
	return (struct shady_vec3){ o[0], o[1], o[2] };
}

static bool ray_triangle(const struct shady_ray *ray,
		struct shady_vec3 v0, struct shady_vec3 v1, struct shady_vec3 v2,
		float *t_out, float *u_out, float *v_out) {
	const float eps = 1e-6f;
	struct shady_vec3 e1 = { v1.x - v0.x, v1.y - v0.y, v1.z - v0.z };
	struct shady_vec3 e2 = { v2.x - v0.x, v2.y - v0.y, v2.z - v0.z };
	struct shady_vec3 p = {
		ray->dir.y * e2.z - ray->dir.z * e2.y,
		ray->dir.z * e2.x - ray->dir.x * e2.z,
		ray->dir.x * e2.y - ray->dir.y * e2.x,
	};
	float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
	if (fabsf(det) < eps) {
		return false;
	}
	float inv_det = 1.0f / det;
	struct shady_vec3 tvec = {
		ray->origin.x - v0.x,
		ray->origin.y - v0.y,
		ray->origin.z - v0.z,
	};
	float u = (tvec.x * p.x + tvec.y * p.y + tvec.z * p.z) * inv_det;
	if (u < 0.f || u > 1.f) {
		return false;
	}
	struct shady_vec3 q = {
		tvec.y * e1.z - tvec.z * e1.y,
		tvec.z * e1.x - tvec.x * e1.z,
		tvec.x * e1.y - tvec.y * e1.x,
	};
	float v = (ray->dir.x * q.x + ray->dir.y * q.y + ray->dir.z * q.z) * inv_det;
	if (v < 0.f || u + v > 1.f) {
		return false;
	}
	float t = (e2.x * q.x + e2.y * q.y + e2.z * q.z) * inv_det;
	if (t < eps) {
		return false;
	}
	*t_out = t;
	*u_out = u;
	*v_out = v;
	return true;
}

bool shady_mesh_bounds(const float model[16], const float *vertices,
		size_t vertex_count, const uint16_t *indices, size_t index_count,
		float center[3], float half[3]) {
	if (!model || !vertices || !center || !half || vertex_count < 1) return false;
	size_t count = indices ? index_count : vertex_count;
	if (count < 1) return false;
	float minv[3] = {INFINITY, INFINITY, INFINITY};
	float maxv[3] = {-INFINITY, -INFINITY, -INFINITY};
	for (size_t i = 0; i < count; ++i) {
		size_t index = indices ? indices[i] : i;
		if (index >= vertex_count) return false;
		const float *v = &vertices[index * 5];
		struct shady_vec3 p = transform_point(model, v[0], v[1], v[2]);
		const float values[3] = {p.x, p.y, p.z};
		for (int axis = 0; axis < 3; ++axis) {
			if (values[axis] < minv[axis]) minv[axis] = values[axis];
			if (values[axis] > maxv[axis]) maxv[axis] = values[axis];
		}
	}
	for (int axis = 0; axis < 3; ++axis) {
		center[axis] = (minv[axis] + maxv[axis]) * .5f;
		half[axis] = fmaxf((maxv[axis] - minv[axis]) * .5f, .001f);
	}
	return true;
}

bool shady_ray_mesh_hit(const struct shady_ray *ray, const float model[16],
		const float *vertices, size_t vertex_count,
		const uint16_t *indices, size_t index_count,
		float *t_out, float *u_out, float *v_out) {
	if (!ray || !model || !vertices || vertex_count < 3) return false;
	size_t element_count = indices ? index_count : vertex_count;
	if (element_count < 3 || element_count % 3 != 0) return false;
	bool hit = false;
	float best_t = 1e30f, best_u = 0.f, best_v = 0.f;
	for (size_t i = 0; i < element_count; i += 3) {
		size_t ia = indices ? indices[i + 0] : i + 0;
		size_t ib = indices ? indices[i + 1] : i + 1;
		size_t ic = indices ? indices[i + 2] : i + 2;
		if (ia >= vertex_count || ib >= vertex_count || ic >= vertex_count) continue;
		const float *a = &vertices[ia * 5];
		const float *b = &vertices[ib * 5];
		const float *c = &vertices[ic * 5];
		struct shady_vec3 p0 = transform_point(model, a[0], a[1], a[2]);
		struct shady_vec3 p1 = transform_point(model, b[0], b[1], b[2]);
		struct shady_vec3 p2 = transform_point(model, c[0], c[1], c[2]);
		float t, bu, bv;
		if (!ray_triangle(ray, p0, p1, p2, &t, &bu, &bv) || t >= best_t)
			continue;
		float bw = 1.f - bu - bv;
		best_t = t;
		best_u = a[3] * bw + b[3] * bu + c[3] * bv;
		best_v = a[4] * bw + b[4] * bu + c[4] * bv;
		hit = true;
	}
	if (!hit) return false;
	if (t_out) *t_out = best_t;
	if (u_out) *u_out = best_u;
	if (v_out) *v_out = best_v;
	return true;
}

bool shady_ray_quad_hit(const struct shady_ray *ray, const float model[16],
		float *t_out, float *u_out, float *v_out) {
	/* Unit quad: (0,0)-(1,0)-(0,1)-(1,1). Two triangles. */
	struct shady_vec3 p00 = transform_point(model, 0.f, 0.f, 0.f);
	struct shady_vec3 p10 = transform_point(model, 1.f, 0.f, 0.f);
	struct shady_vec3 p01 = transform_point(model, 0.f, 1.f, 0.f);
	struct shady_vec3 p11 = transform_point(model, 1.f, 1.f, 0.f);

	float t, bu, bv;
	bool hit = false;
	float best_t = 1e30f;
	float best_u = 0.f, best_v = 0.f;

	/* Triangle (0,0)-(1,0)-(0,1): bary u→+X, v→+Y → quad UV */
	if (ray_triangle(ray, p00, p10, p01, &t, &bu, &bv) && t < best_t) {
		best_t = t;
		best_u = bu;
		best_v = bv;
		hit = true;
	}
	/* Triangle (1,0)-(1,1)-(0,1): map bary to UV */
	if (ray_triangle(ray, p10, p11, p01, &t, &bu, &bv) && t < best_t) {
		best_t = t;
		/* p10=u1v0, p11=u1v1, p01=u0v1 → u = 1-bv, v = bu+bv */
		best_u = 1.f - bv;
		best_v = bu + bv;
		hit = true;
	}

	if (!hit) {
		return false;
	}
	*t_out = best_t;
	*u_out = best_u;
	*v_out = best_v;
	return true;
}

static struct shady_vec3 wobble_front_point(const float model[16],
		float u, float v, float wx, float wy) {
	const float pi = 3.14159265f;
	float cx = u - 0.5f, cy = v - 0.5f;
	float bend_x = sinf(v * pi), bend_y = sinf(u * pi);
	float x = u + wx * bend_x * (0.75f + 0.25f * cosf(cy * pi))
		+ wy * cy * 0.18f * bend_y;
	float y = v + wy * bend_y * (0.75f + 0.25f * cosf(cx * pi))
		+ wx * cx * 0.18f * bend_x;
	float depth = sinf(u * pi) * sinf(v * pi);
	float z = (wx * cy - wy * cx) * 0.65f * depth;
	return transform_point(model, x, y, z);
}

bool shady_ray_wobble_hit(const struct shady_ray *ray, const float model[16],
		float wx, float wy, float *t_out, float *u_out, float *v_out) {
	const int n = 16;
	bool hit = false;
	float best_t = 1e30f, best_u = 0.f, best_v = 0.f;
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < n; ++x) {
			float u0 = (float)x / n, u1 = (float)(x + 1) / n;
			float v0 = (float)y / n, v1 = (float)(y + 1) / n;
			struct shady_vec3 p00 = wobble_front_point(model, u0, v0, wx, wy);
			struct shady_vec3 p10 = wobble_front_point(model, u1, v0, wx, wy);
			struct shady_vec3 p01 = wobble_front_point(model, u0, v1, wx, wy);
			struct shady_vec3 p11 = wobble_front_point(model, u1, v1, wx, wy);
			float t, bu, bv;
			if (ray_triangle(ray, p00, p10, p01, &t, &bu, &bv) && t < best_t) {
				best_t=t; best_u=u0+(u1-u0)*bu; best_v=v0+(v1-v0)*bv; hit=true;
			}
			if (ray_triangle(ray, p10, p11, p01, &t, &bu, &bv) && t < best_t) {
				best_t=t;
				best_u=u1-(u1-u0)*bv;
				best_v=v0+(v1-v0)*(bu+bv);
				hit=true;
			}
		}
	}
	if (!hit) return false;
	*t_out=best_t; *u_out=best_u; *v_out=best_v;
	return true;
}

bool shady_ray_window_shell_hit(const struct shady_ray *ray, const float model[16],
		float wx, float wy, float *t_out, float *u_out, float *v_out,
		bool *front_out) {
	float best_t=1e30f, best_u=0.f, best_v=0.f, t, bu, bv;
	bool hit=false, front=false;
	/* Exact deformed front face. */
	if (shady_ray_wobble_hit(ray, model, wx, wy, &t, &bu, &bv)) {
		best_t=t; best_u=bu; best_v=bv; hit=true; front=true;
	}
	/* Back face and four walls. Local thickness matches model z scale: z=-0.5..0.5. */
	struct shady_vec3 b00=transform_point(model,0,0,-0.5f), b10=transform_point(model,1,0,-0.5f);
	struct shady_vec3 b01=transform_point(model,0,1,-0.5f), b11=transform_point(model,1,1,-0.5f);
	struct shady_vec3 f00=transform_point(model,0,0,0), f10=transform_point(model,1,0,0);
	struct shady_vec3 f01=transform_point(model,0,1,0), f11=transform_point(model,1,1,0);
#define TEST_TRI(A,B,C,U0,V0,U1,V1,U2,V2) do { \
	if (ray_triangle(ray,A,B,C,&t,&bu,&bv) && t<best_t) { \
		best_t=t; best_u=(U0)+(bu)*((U1)-(U0))+(bv)*((U2)-(U0)); \
		best_v=(V0)+(bu)*((V1)-(V0))+(bv)*((V2)-(V0)); hit=true; front=false; } \
} while(0)
	TEST_TRI(b00,b01,b10,0,0,0,1,1,0); TEST_TRI(b10,b01,b11,1,0,0,1,1,1);
	TEST_TRI(f00,b00,f01,0,0,0,0,0,1); TEST_TRI(f01,b00,b01,0,1,0,0,0,1);
	TEST_TRI(f10,f11,b10,1,0,1,1,1,0); TEST_TRI(f11,b11,b10,1,1,1,1,1,0);
	TEST_TRI(f00,f10,b00,0,0,1,0,0,0); TEST_TRI(f10,b10,b00,1,0,1,0,0,0);
	TEST_TRI(f01,b01,f11,0,1,0,1,1,1); TEST_TRI(f11,b01,b11,1,1,0,1,1,1);
#undef TEST_TRI
	if(!hit) return false;
	*t_out=best_t; *u_out=best_u; *v_out=best_v; if(front_out)*front_out=front;
	return true;
}
