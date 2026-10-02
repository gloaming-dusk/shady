#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../src/modules/physics/collision.h"

static int failures;

static void expect_true(bool value, const char *name) {
	if (!value) {
		fprintf(stderr, "physics-collision: FAIL: %s\n", name);
		failures++;
	}
}

static void expect_false(bool value, const char *name) {
	expect_true(!value, name);
}

static void expect_near(float actual, float expected, float eps, const char *name) {
	if (fabsf(actual - expected) > eps) {
		fprintf(stderr,
			"physics-collision: FAIL: %s (got %.6f expected %.6f)\n",
			name, actual, expected);
		failures++;
	}
}

static struct shady_triangle_collider triangle(
		float ax, float ay, float az,
		float bx, float by, float bz,
		float cx, float cy, float cz) {
	struct shady_triangle_collider t = {
		.v = {
			{ax, ay, az},
			{bx, by, bz},
			{cx, cy, cz},
		},
	};
	for (int axis = 0; axis < 3; axis++) {
		t.min[axis] = t.max[axis] = t.v[0][axis];
		for (int i = 1; i < 3; i++) {
			if (t.v[i][axis] < t.min[axis]) t.min[axis] = t.v[i][axis];
			if (t.v[i][axis] > t.max[axis]) t.max[axis] = t.v[i][axis];
		}
	}
	return t;
}

static void test_triangle_sat(void) {
	const float center[3] = {0.f, 0.f, 0.f};
	const float half[3] = {.5f, .5f, .5f};

	struct shady_triangle_collider through = triangle(
		-.8f, 0.f, -.8f,
		 .8f, 0.f, -.8f,
		 0.f, 0.f,  .8f);
	expect_true(shady_physics_triangle_cube_overlap(&through, center, half),
		"triangle crossing cube overlaps");

	struct shady_triangle_collider far = triangle(
		2.f, 2.f, 2.f,
		3.f, 2.f, 2.f,
		2.f, 3.f, 2.f);
	expect_false(shady_physics_triangle_cube_overlap(&far, center, half),
		"distant triangle does not overlap");

	struct shady_triangle_collider touching = triangle(
		-.25f, .5f, -.25f,
		 .25f, .5f, -.25f,
		 0.f,   .5f,  .25f);
	expect_true(shady_physics_triangle_cube_overlap(&touching, center, half),
		"face-touching triangle counts as contact");
}

static void test_box_sweep(void) {
	struct shady_world world = {0};
	world.colliders[0] = (struct shady_box_collider){
		.min_x = 1.f, .max_x = 2.f,
		.min_y = -1.f, .max_y = 1.f,
		.min_z = -1.f, .max_z = 1.f,
	};
	world.collider_count = 1;

	float center[3] = {0.f, 0.f, 0.f};
	const float half[3] = {.25f, .25f, .25f};
	float velocity = 4.f;
	expect_true(shady_physics_sweep_cube_axis(&world, center, half, 0,
		2.f, &velocity, .5f), "positive x sweep hits wall");
	expect_near(center[0], .75f, 1e-6f, "positive sweep contact position");
	expect_near(velocity, -2.f, 1e-6f, "positive sweep restitution");

	center[0] = 3.f;
	velocity = -6.f;
	expect_true(shady_physics_sweep_cube_axis(&world, center, half, 0,
		-2.f, &velocity, .25f), "negative x sweep hits wall");
	expect_near(center[0], 2.25f, 1e-6f, "negative sweep contact position");
	expect_near(velocity, 1.5f, 1e-6f, "negative sweep restitution");
}

static void test_substepped_move(void) {
	struct shady_world world = {0};
	world.colliders[0] = (struct shady_box_collider){
		.min_x = 1.f, .max_x = 1.2f,
		.min_y = -2.f, .max_y = 2.f,
		.min_z = -2.f, .max_z = 2.f,
	};
	world.collider_count = 1;

	float center[3] = {-2.f, 0.f, 0.f};
	const float target[3] = {3.f, 0.f, 0.f};
	shady_physics_move_cube(&world, center, target, .25f);
	expect_near(center[0], .75f, 1e-5f,
		"substepped movement does not tunnel through wall");
}

static void test_triangle_world_contact(void) {
	struct shady_world world = {0};
	/* collider slot zero is the built-in floor slot when triangle collision
	 * exists; keep it far away so this test exercises the triangle path. */
	world.colliders[0] = (struct shady_box_collider){
		.min_x = -100.f, .max_x = 100.f,
		.min_y = -100.f, .max_y = -99.f,
		.min_z = -100.f, .max_z = 100.f,
	};
	world.collider_count = 1;
	world.triangles[0] = triangle(
		0.f, -1.f, -1.f,
		0.f,  1.f, -1.f,
		0.f,  0.f,  1.f);
	world.triangle_count = 1;

	float center[3] = {-1.f, 0.f, 0.f};
	const float half[3] = {.25f, .25f, .25f};
	float velocity = 1.f;
	expect_true(shady_physics_sweep_cube_axis(&world, center, half, 0,
		1.f, &velocity, 0.f), "triangle sweep reports contact");
	expect_near(center[0], -1.f, 1e-6f,
		"triangle contact rejects penetrating step");
}

int main(void) {
	test_triangle_sat();
	test_box_sweep();
	test_substepped_move();
	test_triangle_world_contact();

	if (failures) {
		fprintf(stderr, "physics-collision: %d failure(s)\n", failures);
		return 1;
	}
	puts("physics-collision: PASS");
	return 0;
}
