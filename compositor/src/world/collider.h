#ifndef SHADY_WORLD_COLLIDER_H
#define SHADY_WORLD_COLLIDER_H
#include <stdbool.h>

struct shady_box_collider {
	float min_x, max_x;
	float min_y, max_y;
	float min_z, max_z;
};

static inline bool shady_box_overlap_xz(const struct shady_box_collider *a,
		const struct shady_box_collider *b) {
	return a->max_x >= b->min_x && a->min_x <= b->max_x &&
		a->max_z >= b->min_z && a->min_z <= b->max_z;
}

/* Returns the supporting top plane when the body's horizontal footprint
 * overlaps the collider. This is the first building block for platforms. */
static inline bool shady_box_support_y(const struct shady_box_collider *surface,
		const struct shady_box_collider *body, float *y_out) {
	if (!shady_box_overlap_xz(surface, body)) return false;
	if (y_out) *y_out = surface->max_y;
	return true;
}
#endif
