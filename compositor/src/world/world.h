#ifndef SHADY_WORLD_WORLD_H
#define SHADY_WORLD_WORLD_H
#include <stddef.h>
#include "collider.h"
#include "floor.h"

#define SHADY_WORLD_MAX_COLLIDERS 32
#define SHADY_WORLD_MAX_TRIANGLES 4096

struct shady_triangle_collider {
	float v[3][3];
	float min[3], max[3];
};

struct shady_world {
	struct shady_box_collider colliders[SHADY_WORLD_MAX_COLLIDERS];
	size_t collider_count;
	struct shady_triangle_collider triangles[SHADY_WORLD_MAX_TRIANGLES];
	size_t triangle_count;
};

static inline bool shady_world_add_collider(struct shady_world *world,
		struct shady_box_collider collider) {
	if (world->collider_count >= SHADY_WORLD_MAX_COLLIDERS) return false;
	world->colliders[world->collider_count++] = collider;
	return true;
}

static inline bool shady_world_add_triangle(struct shady_world *world,
		const struct shady_triangle_collider *triangle) {
	if (world->triangle_count >= SHADY_WORLD_MAX_TRIANGLES) return false;
	world->triangles[world->triangle_count++] = *triangle;
	return true;
}

/* Build the current static world. Environment collision should register
 * authored/coarse colliders here; visual mesh bounds are intentionally not
 * treated as solid geometry because their empty volume causes trapping. */
static inline struct shady_world shady_world_default(void) {
	struct shady_world world = {0};
	struct shady_floor floor = shady_world_floor();
	shady_world_add_collider(&world, shady_floor_collider(&floor));
	return world;
}

/* Find the highest supporting surface under an object's XZ footprint. */
static inline bool shady_world_support_y(const struct shady_world *world,
		const struct shady_box_collider *body, float *y_out) {
	bool found = false;
	float best = 0.0f;
	for (size_t i = 0; i < world->collider_count; ++i) {
		float y;
		if (!shady_box_support_y(&world->colliders[i], body, &y))
			continue;
		if (!found || y > best) {
			best = y;
			found = true;
		}
	}
	if (found && y_out) *y_out = best;
	return found;
}
#endif
