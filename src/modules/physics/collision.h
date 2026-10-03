#ifndef SHADY_MODULE_PHYSICS_COLLISION_H
#define SHADY_MODULE_PHYSICS_COLLISION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../world/world.h"

bool shady_physics_triangle_cube_overlap(
	const struct shady_triangle_collider *triangle,
	const float center[3], const float half[3]);
bool shady_physics_triangle_convex_overlap(
	const struct shady_triangle_collider *triangle,
	const float (*vertices)[3], size_t vertex_count,
	const uint16_t *indices, size_t index_count,
	const float offset[3]);

bool shady_physics_sweep_cube_axis(const struct shady_world *world,
	float center[3], const float half[3], int axis, float delta,
	float *velocity, float restitution);
bool shady_physics_sweep_convex_axis(const struct shady_world *world,
	float center[3], const float half[3],
	const float (*vertices)[3], size_t vertex_count,
	const uint16_t *indices, size_t index_count,
	int axis, float delta, float *velocity, float restitution);

void shady_physics_move_box(const struct shady_world *world, float center[3],
	const float target[3], const float half[3]);
void shady_physics_move_convex(const struct shady_world *world, float center[3],
	const float target[3], const float half[3],
	const float (*vertices)[3], size_t vertex_count,
	const uint16_t *indices, size_t index_count);
void shady_physics_move_cube(const struct shady_world *world, float center[3],
	const float target[3], float half_size);

#endif
