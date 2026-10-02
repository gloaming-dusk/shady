#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/render/math3d.h"

static int failures;

static bool nearf(float a, float b, float eps) {
	return fabsf(a - b) <= eps;
}

static void fail(const char *name) {
	fprintf(stderr, "math3d: FAIL: %s\n", name);
	failures++;
}

static void expect_near(float actual, float expected, float eps, const char *name) {
	if (!nearf(actual, expected, eps)) {
		fprintf(stderr, "math3d: FAIL: %s (got %.7f expected %.7f)\n",
			name, actual, expected);
		failures++;
	}
}

static void mul_point(const float m[16], float x, float y, float z, float out[3]) {
	out[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
	out[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
	out[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}

static void test_inverse(void) {
	float t[16], rx[16], ry[16], s[16], a[16], b[16], m[16], inv[16], id[16];
	shady_mat4_translate(t, 1.25f, -2.0f, 3.5f);
	shady_mat4_rotate_x(rx, 0.37f);
	shady_mat4_rotate_y(ry, -0.81f);
	shady_mat4_scale(s, 2.0f, 0.5f, 1.5f);
	shady_mat4_multiply(a, t, ry);
	shady_mat4_multiply(b, a, rx);
	shady_mat4_multiply(m, b, s);
	if (!shady_mat4_invert(inv, m)) {
		fail("invert composed transform");
		return;
	}
	shady_mat4_multiply(id, m, inv);
	for (int col = 0; col < 4; col++) {
		for (int row = 0; row < 4; row++) {
			float expected = row == col ? 1.0f : 0.0f;
			if (!nearf(id[col * 4 + row], expected, 1e-4f)) {
				fail("M * inverse(M) is identity");
				return;
			}
		}
	}

	float singular[16] = {0};
	if (shady_mat4_invert(inv, singular))
		fail("singular matrix rejected");
}

static void test_cube_center(void) {
	float model[16];
	const float cx = 1.2f, cy = -0.4f, cz = 2.3f;
	shady_window_cube_model(model, cx, cy, cz, 0.7f, 0.31f, -0.52f);
	float center[3];
	mul_point(model, 0.5f, 0.5f, -0.5f, center);
	expect_near(center[0], cx, 1e-5f, "cube local center x");
	expect_near(center[1], cy, 1e-5f, "cube local center y");
	expect_near(center[2], cz, 1e-5f, "cube local center z");
}

static void test_center_ray(void) {
	struct shady_camera cam;
	shady_camera_reset(&cam);
	float view[16], proj[16];
	shady_camera_view(&cam, view);
	shady_mat4_perspective(proj, SHADY_CAMERA_FOV_Y, 16.0f / 9.0f,
		SHADY_CAMERA_NEAR, SHADY_CAMERA_FAR);

	struct shady_ray ray;
	shady_ray_from_ndc(&ray, 0.0f, 0.0f, view, proj);

	float len = sqrtf(ray.dir.x * ray.dir.x +
		ray.dir.y * ray.dir.y + ray.dir.z * ray.dir.z);
	expect_near(len, 1.0f, 1e-5f, "center ray normalized");

	struct shady_vec3 forward;
	shady_camera_basis(&cam, NULL, NULL, &forward);
	float dot = ray.dir.x * forward.x +
		ray.dir.y * forward.y + ray.dir.z * forward.z;
	if (dot < 0.9999f)
		fail("center ray follows camera forward");
}

static void test_quad_hit(void) {
	float model[16];
	shady_mat4_identity(model);
	struct shady_ray ray = {
		.origin = {0.5f, 0.5f, 1.0f},
		.dir = {0.0f, 0.0f, -1.0f},
	};
	float t = 0.f, u = 0.f, v = 0.f;
	if (!shady_ray_quad_hit(&ray, model, &t, &u, &v)) {
		fail("unit quad center hit");
		return;
	}
	expect_near(t, 1.0f, 1e-5f, "quad hit distance");
	expect_near(u, 0.5f, 1e-5f, "quad hit u");
	expect_near(v, 0.5f, 1e-5f, "quad hit v");

	ray.origin.x = 2.0f;
	if (shady_ray_quad_hit(&ray, model, NULL, NULL, NULL))
		fail("quad miss outside bounds");
}

int main(void) {
	test_inverse();
	test_cube_center();
	test_center_ray();
	test_quad_hit();

	if (failures) {
		fprintf(stderr, "math3d: %d failure(s)\n", failures);
		return 1;
	}
	puts("math3d: PASS");
	return 0;
}
