#ifndef SHADY_REPRESENTATION_STATE_H
#define SHADY_REPRESENTATION_STATE_H
#include <shady/plugin.h>
struct shady_representation_state {
	bool override;
	struct shady_window_representation base;
	void *owner;
	bool provider_active;
	struct shady_window_representation_provider provider;
	const struct shady_window_representation_provider *provider_anchor;
	void *provider_owner;
	void *state;
	size_t state_size;
	struct shady_representation_cache *cache;
};

struct shady_representation_cache {
	struct {
		bool initialized;
		bool valid;
		const void *vertices;
		const void *indices;
		size_t vertex_count;
		size_t index_count;
		uint64_t revision;
	} mesh_validation_cache;
	struct {
		bool initialized;
		bool valid;
		const void *vertices;
		const void *indices;
		size_t vertex_count;
		size_t index_count;
		uint64_t revision;
	} hull_validation_cache;
	struct {
		bool valid;
		uint64_t revision;
		const void *vertices;
		const void *indices;
		size_t vertex_count;
		size_t index_count;
		float shape_key[5];
		float vertices_world[256][3];
		float center_offset[3];
		float half[3];
	} hull_world_cache;
	struct {
		bool initialized;
		bool valid;
		const void *parts;
		size_t part_count;
		uint64_t revision;
	} compound_validation_cache;
	struct {
		bool valid;
		uint64_t revision;
		const void *parts;
		size_t part_count;
		float shape_key[5];
		float vertices_world[256][3];
		uint16_t indices[1536];
		size_t part_vertex_offset[8];
		size_t part_vertex_count[8];
		size_t part_index_offset[8];
		size_t part_index_count[8];
		float center_offset[3];
		float half[3];
	} compound_world_cache;
};
#endif
