#ifndef SHADY_WORLD_FLOOR_H
#define SHADY_WORLD_FLOOR_H
#include <stdbool.h>
#include "collider.h"

/* Single source of truth for the rendered floor and its collider. */
struct shady_floor {
	float min_x, max_x;
	float y;
	float min_z, max_z;
};

static inline struct shady_floor shady_world_floor(void) {
	return (struct shady_floor){ -6.0f, 6.0f, -0.62f, -6.0f, 6.0f };
}

static inline struct shady_box_collider shady_floor_collider(
		const struct shady_floor *f) {
	return (struct shady_box_collider){
		f->min_x, f->max_x, f->y - 0.02f, f->y,
		f->min_z, f->max_z
	};
}

static inline bool shady_floor_contains_xz(const struct shady_floor *f,
		float x, float z) {
	return x >= f->min_x && x <= f->max_x &&
		z >= f->min_z && z <= f->max_z;
}
#endif
