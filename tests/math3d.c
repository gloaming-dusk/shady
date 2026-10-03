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

static void test_wobble_hit(void) {
	float model[16];
	shady_mat4_identity(model);
	struct shady_ray ray = {
		.origin = {0.5f, 0.5f, 1.0f},
		.dir = {0.0f, 0.0f, -1.0f},
	};
	float t = 0.f, u = 0.f, v = 0.f;
	if (!shady_ray_wobble_hit(&ray, model, 0.18f, -0.12f, &t, &u, &v)) {
		fail("wobble mesh center hit");
		return;
	}
	if (!(t > 0.f)) fail("wobble hit positive distance");
	if (u < 0.f || u > 1.f || v < 0.f || v > 1.f)
		fail("wobble hit uv bounds");

	ray.origin.x = 2.0f;
	if (shady_ray_wobble_hit(&ray, model, 0.18f, -0.12f, &t, &u, &v))
		fail("wobble mesh miss outside bounds");
}

static void test_window_shell_hit(void) {
	float model[16];
	shady_mat4_identity(model);
	float t = 0.f, u = 0.f, v = 0.f;
	bool front = false;

	struct shady_ray front_ray = {
		.origin = {0.5f, 0.5f, 1.0f},
		.dir = {0.0f, 0.0f, -1.0f},
	};
	if (!shady_ray_window_shell_hit(&front_ray, model, 0.f, 0.f,
			&t, &u, &v, &front)) {
		fail("window shell front hit");
	} else if (!front) {
		fail("window shell front classified front");
	}

	struct shady_ray back_ray = {
		.origin = {0.5f, 0.5f, -2.0f},
		.dir = {0.0f, 0.0f, 1.0f},
	};
	if (!shady_ray_window_shell_hit(&back_ray, model, 0.f, 0.f,
			&t, &u, &v, &front)) {
		fail("window shell back hit");
	} else if (front) {
		fail("window shell back classified non-front");
	}

	struct shady_ray side_ray = {
		.origin = {-1.0f, 0.5f, -0.25f},
		.dir = {1.0f, 0.0f, 0.0f},
	};
	if (!shady_ray_window_shell_hit(&side_ray, model, 0.f, 0.f,
			&t, &u, &v, &front)) {
		fail("window shell side hit");
	} else if (front) {
		fail("window shell side classified non-front");
	}

	struct shady_ray miss = {
		.origin = {2.0f, 2.0f, 1.0f},
		.dir = {0.0f, 0.0f, -1.0f},
	};
	if (shady_ray_window_shell_hit(&miss, model, 0.f, 0.f,
			&t, &u, &v, &front))
		fail("window shell miss outside bounds");
}

static void test_transformed_shell_hit(void) {
	float tmat[16], ry[16], model[16];
	shady_mat4_translate(tmat, 0.4f, -0.2f, -0.7f);
	shady_mat4_rotate_y(ry, 0.25f);
	shady_mat4_multiply(model, tmat, ry);

	/* Shoot through the transformed local center from well in front. */
	float center[3];
	mul_point(model, 0.5f, 0.5f, 0.0f, center);
	struct shady_ray ray = {
		.origin = {center[0], center[1], center[2] + 2.0f},
		.dir = {0.0f, 0.0f, -1.0f},
	};
	float t = 0.f, u = 0.f, v = 0.f;
	bool front = false;
	if (!shady_ray_window_shell_hit(&ray, model, 0.08f, 0.03f,
			&t, &u, &v, &front)) {
		fail("transformed window shell hit");
		return;
	}
	if (!(t > 0.f)) fail("transformed shell positive distance");
	if (u < 0.f || u > 1.f || v < 0.f || v > 1.f)
		fail("transformed shell uv bounds");
}

static void test_mesh_bounds(void) {
	float model[16];
	shady_mat4_identity(model);
	const float mesh[] = {
		0.f, 0.f, 0.f,    0.f, 0.f,
		1.f, 0.f, -0.2f,  1.f, 0.f,
		0.f, 1.f, 0.1f,   0.f, 1.f,
		1.f, 1.f, 0.f,    1.f, 1.f,
		9.f, 9.f, 9.f,    0.f, 0.f,
	};
	const uint16_t indices[] = {0, 1, 2, 2, 1, 3};
	float center[3], half[3];
	if (!shady_mesh_bounds(model, mesh, 5, indices, 6, center, half)) {
		fail("indexed mesh bounds");
		return;
	}
	expect_near(center[0], 0.5f, 1e-5f, "mesh bounds center x");
	expect_near(center[1], 0.5f, 1e-5f, "mesh bounds center y");
	expect_near(center[2], -0.05f, 1e-5f, "mesh bounds center z");
	expect_near(half[0], 0.5f, 1e-5f, "mesh bounds half x");
	expect_near(half[1], 0.5f, 1e-5f, "mesh bounds half y");
	expect_near(half[2], 0.15f, 1e-5f, "mesh bounds half z");
}

static void test_mesh_hit(void) {
	float model[16];
	shady_mat4_identity(model);
	const float mesh[] = {
		0.f, 0.f, 0.f,   0.10f, 0.20f,
		1.f, 0.f, -0.2f, 0.90f, 0.20f,
		0.f, 1.f, 0.1f,  0.10f, 0.80f,
		1.f, 1.f, 0.f,   0.90f, 0.80f,
	};
	const uint16_t indices[] = {0, 1, 2, 2, 1, 3};
	struct shady_ray ray = {
		.origin = {0.25f, 0.25f, 1.f},
		.dir = {0.f, 0.f, -1.f},
	};
	float t = 0.f, u = 0.f, v = 0.f;
	if (!shady_ray_mesh_hit(&ray, model, mesh, 4, indices, 6, &t, &u, &v)) {
		fail("deformable mesh hit");
		return;
	}
	expect_near(u, 0.30f, 1e-5f, "mesh hit independent u");
	expect_near(v, 0.35f, 1e-5f, "mesh hit independent v");
	if (!(t > 0.f)) fail("mesh hit positive distance");

	ray.origin.x = 2.f;
	if (shady_ray_mesh_hit(&ray, model, mesh, 4, indices, 6, &t, &u, &v))
		fail("deformable mesh miss outside bounds");
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
	test_mesh_hit();
	test_mesh_bounds();
	test_wobble_hit();
	test_window_shell_hit();
	test_transformed_shell_hit();

	if (failures) {
		fprintf(stderr, "math3d: %d failure(s)\n", failures);
		return 1;
	}
	puts("math3d: PASS");
	return 0;
}
