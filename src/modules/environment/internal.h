#ifndef SHADY_MODULE_ENVIRONMENT_INTERNAL_H
#define SHADY_MODULE_ENVIRONMENT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "environment.h"

#define SHADY_ENVIRONMENT_MAX_LOADERS 16
#define SHADY_ENVIRONMENT_PATH_MAX 512

struct shady_environment_loader_slot {
	shady_environment_loader_id id; /* 0 = free */
	void *owner;
	const struct shady_environment_loader *loader;
};

/* Host-owned copy of the active scene. Visual geometry is stored de-indexed
 * as a flat triangle list so GLES2 can draw it without 32-bit indices. */
struct shady_environment_scene_data {
	struct shady_environment_vertex *vertices;
	size_t vertex_count;
	struct shady_environment_box *boxes;
	size_t box_count;
	struct shady_environment_triangle *triangles;
	size_t triangle_count;
};

enum shady_environment_source {
	SHADY_ENVIRONMENT_SOURCE_NONE = 0,
	SHADY_ENVIRONMENT_SOURCE_CONFIG,     /* loaded from environment_path */
	SHADY_ENVIRONMENT_SOURCE_PROCEDURAL, /* set_scene() by a plugin */
};

struct shady_environment {
	struct shady_environment_loader_slot loaders[SHADY_ENVIRONMENT_MAX_LOADERS];
	shady_environment_loader_id next_loader_id;

	struct shady_environment_scene_data scene;
	enum shady_environment_source source;
	void *scene_owner;
	shady_environment_loader_id scene_loader;
	/* Bumped on every scene change so the renderer re-uploads. */
	uint64_t revision;

	/* Config path the last sync acted on; dirty forces a retry. */
	char synced_path[SHADY_ENVIRONMENT_PATH_MAX];
	bool dirty;
};

/* environment.c */
/* Release the active scene regardless of owner and restore the default world. */
void shady_environment_drop_scene(struct shady_server *server);

/* loaders.c */
const struct shady_environment_loader_slot *shady_environment_find_loader(
	const struct shady_environment *env, const char *path);

/* scene.c */
bool shady_environment_scene_copy(struct shady_environment_scene_data *out,
	const struct shady_environment_scene *scene, size_t max_boxes,
	size_t max_triangles, const char *label);
void shady_environment_scene_free(struct shady_environment_scene_data *scene);
/* Rebuild the spatial world: default floor + the scene's collision. */
void shady_environment_apply_collision(struct shady_server *server,
	const struct shady_environment_scene_data *scene);
size_t shady_environment_box_capacity(void);
size_t shady_environment_triangle_capacity(void);

/* sky.c — GL helpers shared with draw.c. Returns a linked GLuint program with
 * attributes bound to locations 0..attribute_count-1, or 0. */
unsigned int shady_environment_compile_program(const char *vertex,
	const char *fragment, const char *const *attributes, size_t attribute_count);
bool shady_environment_sky_gl_init(void);
void shady_environment_sky_gl_fini(void);

#endif
