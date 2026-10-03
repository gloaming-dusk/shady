#ifndef SHADY_PUBLIC_REPRESENTATION_H
#define SHADY_PUBLIC_REPRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <shady/types.h>
#define SHADY_REPRESENTATION_API "shady.window-representation"
#define SHADY_REPRESENTATION_API_VERSION 1u

enum shady_window_representation_kind {
	SHADY_WINDOW_REPRESENTATION_DEFAULT = 0,
	SHADY_WINDOW_REPRESENTATION_BOX = 1,
	SHADY_WINDOW_REPRESENTATION_MESH = 2,
};

struct shady_representation_vertex {
	float x;
	float y;
	float z;
	float u;
	float v;
};

struct shady_representation_mesh {
	uint32_t struct_size;
	const struct shady_representation_vertex *vertices;
	size_t vertex_count;
	const uint16_t *indices;
	size_t index_count;
	uint64_t revision;
};

struct shady_window_representation {
	uint32_t struct_size;
	uint32_t kind;
	float width;
	float height;
	float depth;
	bool hide_titlebar;
};

struct shady_representation_context {
	uint32_t struct_size;
	float logical_width;
	float logical_height;
	float window_width;
	float window_height;
	float center_x;
	float center_y;
	float center_z;
	float tilt_x;
	float tilt_y;
	bool first_person;
	bool folded;
	bool held;
	bool focused;
};

struct shady_representation_model {
	uint32_t struct_size;
	float center_x;
	float center_y;
	float center_z;
	float width;
	float height;
	float depth;
	float tilt_x;
	float tilt_y;
	bool hide_titlebar;
};

struct shady_collision_box {
	uint32_t struct_size;
	float center[3];
	float half[3];
};

struct shady_collision_vertex {
	float x;
	float y;
	float z;
};

struct shady_collision_hull {
	uint32_t struct_size;
	const struct shady_collision_vertex *vertices;
	size_t vertex_count;
	const uint16_t *indices;
	size_t index_count;
	uint64_t revision;
};

struct shady_collision_compound {
	uint32_t struct_size;
	const struct shady_collision_hull *parts;
	size_t part_count;
	uint64_t revision;
};

typedef bool (*shady_representation_state_init_callback)(
	shady_host host, shady_window window, void *state, void *user_data);

typedef void (*shady_representation_state_destroy_callback)(
	shady_host host, shady_window window, void *state, void *user_data);

typedef bool (*shady_representation_update_callback)(
	shady_host host, shady_window window, float dt,
	void *state, void *user_data);

typedef bool (*shady_representation_model_callback)(
	shady_host host, shady_window window,
	const struct shady_representation_context *context,
	struct shady_representation_model *model,
	void *state, void *user_data);

typedef bool (*shady_representation_collision_callback)(
	shady_host host, shady_window window,
	const struct shady_representation_context *context,
	struct shady_collision_box *box,
	void *state, void *user_data);

typedef bool (*shady_representation_mesh_callback)(
	shady_host host, shady_window window,
	const struct shady_representation_context *context,
	struct shady_representation_mesh *mesh,
	void *state, void *user_data);

typedef bool (*shady_representation_collision_hull_callback)(
	shady_host host, shady_window window,
	const struct shady_representation_context *context,
	struct shady_collision_hull *hull,
	void *state, void *user_data);

typedef bool (*shady_representation_collision_compound_callback)(
	shady_host host, shady_window window,
	const struct shady_representation_context *context,
	struct shady_collision_compound *compound,
	void *state, void *user_data);

struct shady_window_representation_provider {
	uint32_t struct_size;
	struct shady_window_representation base;
	size_t state_size;
	shady_representation_state_init_callback state_init;
	shady_representation_state_destroy_callback state_destroy;
	shady_representation_update_callback update;
	shady_representation_model_callback model;
	shady_representation_collision_callback collision;
	shady_representation_mesh_callback mesh;
	void *user_data;
	shady_representation_collision_hull_callback collision_hull;
	shady_representation_collision_compound_callback collision_compound;
};


/* Same ownership and fallback behavior as the legacy umbrella API. The table
 * is host-owned, immutable, and valid for the lifetime of the compositor. */
struct shady_representation_api_v1 {
    uint32_t struct_size;
    bool (*set)(shady_host, shady_window, const struct shady_window_representation *);
    bool (*reset)(shady_host, shady_window);
    bool (*get)(shady_window, struct shady_window_representation *, bool *overridden);
    bool (*attach_provider)(shady_host, shady_window,
        const struct shady_window_representation_provider *);
    bool (*detach_provider)(shady_host, shady_window,
        const struct shady_window_representation_provider *);
    void *(*provider_state)(shady_host, shady_window,
        const struct shady_window_representation_provider *, size_t *state_size);
};
#endif
