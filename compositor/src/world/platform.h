#ifndef SHADY_WORLD_PLATFORM_H
#define SHADY_WORLD_PLATFORM_H
#include "collider.h"

/* Temporary test platform proving that world collision supports multiple
 * elevated surfaces. Later this can be replaced by object/OBJ colliders. */
static inline struct shady_box_collider shady_world_test_platform(void) {
	return (struct shady_box_collider){
		0.85f, 2.25f,
		-0.62f, -0.20f,
		-0.85f, 0.85f
	};
}
#endif
