#ifndef SHADY_PUBLIC_ENVIRONMENT_H
#define SHADY_PUBLIC_ENVIRONMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <shady/types.h>

/*
 * Static 3D environment.
 *
 * The host owns the environment: it renders the visual mesh, registers the
 * collision geometry in the spatial world and decides which file is active
 * (config keys `environment` and `environment_path`). It does not parse any
 * file format. Asset loaders are plugins that register a
 * struct shady_environment_loader for one or more file extensions; the host
 * calls the loader whose extension matches the configured path and
 * deep-copies the scene it returns.
 *
 * Plugins may also submit a procedural scene with set_scene().
 */

#define SHADY_ENVIRONMENT_API "shady.environment"
#define SHADY_ENVIRONMENT_API_VERSION 1u

/* World-space vertex. Loaders flatten node transforms before submitting. */
struct shady_environment_vertex {
	float position[3];
	float normal[3];
	float uv[2];
};

/* Coarse axis-aligned collider, e.g. the bounds of one authored group. */
struct shady_environment_box {
	float min[3];
	float max[3];
};

/* Colour of one vertex, as displayed (sRGB, like CSS colours), each channel
 * 0..1. The mesh is drawn opaque: alpha says how strongly the scene's light
 * and shade fall on the vertex, 1 fully, 0 not at all (an ink line, a glow
 * is drawn in its colour as it is). */
struct shady_environment_color {
	float rgba[4];
};

/* Exact narrow-phase collision triangle. */
struct shady_environment_triangle {
	float v[3][3];
};

/*
 * File-format-neutral scene. All arrays are borrowed for the duration of the
 * call that receives them; the host validates and copies everything.
 *
 * vertices/indices describe a triangle list. indices may be NULL, in which
 * case vertex_count must be a multiple of three. Collision arrays are
 * optional and limited by the spatial world's fixed capacity.
 *
 * The struct is append-only: members added after the first version are at
 * the end and gated by struct_size. A loader must check that the struct the
 * host passed is large enough before writing one (see
 * SHADY_ENVIRONMENT_SCENE_HAS); the host reads one only when the plugin's
 * struct_size covers it.
 */
struct shady_environment_scene {
	uint32_t struct_size;

	const struct shady_environment_vertex *vertices;
	size_t vertex_count;
	const uint32_t *indices;
	size_t index_count;

	const struct shady_environment_box *boxes;
	size_t box_count;
	const struct shady_environment_triangle *triangles;
	size_t triangle_count;

	/* Appended: vertex_count colours, one per vertex, or NULL to draw the
	 * mesh in Shady's single environment colour. */
	const struct shady_environment_color *colors;

	/* Appended: SHADY_ENVIRONMENT_SCENE_* flags. */
	uint32_t flags;
	/* Appended: one RGBA8 image, rows top first, sampled at each vertex's uv
	 * (origin bottom-left, clamped) and multiplied into its colour. Texels
	 * with alpha under 0.3 are not drawn. NULL: no texture, uv is ignored.
	 * At most SHADY_ENVIRONMENT_TEXTURE_MAX texels a side. */
	const uint8_t *texture;
	uint32_t texture_width, texture_height;
	/* Non-zero: the texture holds the same pixels as the last scene given the
	 * same key and size, so the host keeps its copy instead of copying and
	 * uploading them again; a scene replaced every frame should set it. */
	uint64_t texture_key;
};

/* Cel shading: three bands of light, as three.js' MeshToonMaterial with a
 * [150, 215, 255] ramp, and a soft rim of light along the silhouette. */
#define SHADY_ENVIRONMENT_SCENE_TOON (1u << 0)
/* Draw only front faces, wound counter-clockwise. Faces meant to be seen
 * from both sides are then given twice. Needed for ink lines drawn as a
 * back-facing shell. */
#define SHADY_ENVIRONMENT_SCENE_CULL_BACK_FACES (1u << 1)

#define SHADY_ENVIRONMENT_TEXTURE_MAX 4096u

/* Whether a scene struct of this struct_size has the member. */
#define SHADY_ENVIRONMENT_SCENE_HAS(scene, member) \
	((scene)->struct_size >= offsetof(struct shady_environment_scene, member) + \
		sizeof((scene)->member))
/* The first version, without colors: the smallest struct the host accepts. */
#define SHADY_ENVIRONMENT_SCENE_V1_SIZE offsetof(struct shady_environment_scene, colors)

/*
 * Asset loader descriptor. The descriptor, its strings and its callbacks must
 * live in the registering plugin; they are referenced, not copied, until the
 * loader is unregistered or the plugin is unloaded.
 */
struct shady_environment_loader {
	uint32_t struct_size;

	/* Short identifier used in logs, e.g. "obj" or "gltf". */
	const char *name;
	/* NULL-terminated list of extensions without the dot, matched
	 * case-insensitively, e.g. { "gltf", "glb", NULL }. */
	const char *const *extensions;

	/* Parse path into *scene. Return false on any error; the host logs the
	 * failure and keeps no geometry. On success the host copies the scene and
	 * then calls release() with the same struct. */
	bool (*load)(shady_host host, const char *path,
		struct shady_environment_scene *scene, void *user_data);
	/* Free whatever load() allocated. Called only after a successful load. */
	void (*release)(shady_host host, struct shady_environment_scene *scene,
		void *user_data);

	void *user_data;
};

typedef uint64_t shady_environment_loader_id;

struct shady_environment_api_v1 {
	uint32_t struct_size;

	/* Returns 0 on failure: invalid descriptor, missing spatial module, or an
	 * extension already claimed by another loader. A successful registration
	 * makes the host retry the configured environment path. */
	shady_environment_loader_id (*register_loader)(shady_host host,
		const struct shady_environment_loader *loader);
	/* Only the registering plugin may unregister. A scene produced by the
	 * loader is cleared with it. */
	bool (*unregister_loader)(shady_host host, shady_environment_loader_id id);

	/* Replace the active scene with a procedural one owned by the caller.
	 * Fails if another plugin currently owns the scene. */
	bool (*set_scene)(shady_host host,
		const struct shady_environment_scene *scene);
	/* Clear a scene previously set by the caller. */
	bool (*clear_scene)(shady_host host);
};

#endif
