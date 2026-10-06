#include "internal.h"
#include "representation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../shady.h"
#include "../render/render.h"
#include "../render/math3d.h"
#define HOST(h) ((struct shady_server *)(h))
#define WINDOW(w) ((struct shady_toplevel *)(w))
#define SHADY_MAX_REPRESENTATION_STATE (1024u * 1024u)
#define SHADY_MAX_REPRESENTATION_VERTICES 16384u
#define SHADY_MAX_REPRESENTATION_INDICES 49152u
#define SHADY_MAX_COLLISION_HULL_VERTICES 256u
#define SHADY_MAX_COLLISION_HULL_INDICES 1536u
static bool plugin_callback_owned_by(void *owner, void *callback) {
    return owner && callback && shady_plugin_owner_from_address(callback) == owner;
}
#include "representation_state.h"
static bool plugin_representation_provider_callbacks_valid(
		void *owner, const struct shady_window_representation_provider *provider) {
	if (!owner || !provider) return false;
	if (provider->state_init &&
			!plugin_callback_owned_by(owner, (void *)provider->state_init)) return false;
	if (provider->state_destroy &&
			!plugin_callback_owned_by(owner, (void *)provider->state_destroy)) return false;
	if (provider->update &&
			!plugin_callback_owned_by(owner, (void *)provider->update)) return false;
	if (provider->model &&
			!plugin_callback_owned_by(owner, (void *)provider->model)) return false;
	if (provider->collision &&
			!plugin_callback_owned_by(owner, (void *)provider->collision)) return false;
	if (provider->mesh &&
			!plugin_callback_owned_by(owner, (void *)provider->mesh)) return false;
	if (provider->collision_hull &&
			!plugin_callback_owned_by(owner, (void *)provider->collision_hull)) return false;
	if (provider->collision_compound &&
			!plugin_callback_owned_by(owner, (void *)provider->collision_compound)) return false;
	return true;
}

static bool representation_cache_ensure(struct shady_toplevel *toplevel) {
    if (!toplevel->representation->cache)
        toplevel->representation->cache = calloc(1, sizeof(*toplevel->representation->cache));
    return toplevel->representation->cache != NULL;
}

static void plugin_representation_cache_clear(struct shady_toplevel *toplevel) {
    if (!toplevel || !toplevel->representation) return;
    free(toplevel->representation->cache);
    toplevel->representation->cache = NULL;
}

static void representation_release_if_unused(struct shady_toplevel *toplevel) {
    if (!toplevel->representation || toplevel->representation->override ||
            toplevel->representation->provider_active) return;
    plugin_representation_cache_clear(toplevel);
    free(toplevel->representation);
    toplevel->representation = NULL;
}

static void plugin_representation_provider_detach(struct shady_toplevel *toplevel) {
	if (!toplevel || !toplevel->representation || !toplevel->representation->provider_active) return;
	struct shady_window_representation_provider provider =
		toplevel->representation->provider;
	void *owner = toplevel->representation->provider_owner;
	void *state = toplevel->representation->state;
	if (provider.state_destroy &&
			plugin_callback_owned_by(owner, (void *)provider.state_destroy)) {
		provider.state_destroy((shady_host)toplevel->server, (shady_window)toplevel,
			state, provider.user_data);
	}
	free(state);
	memset(&toplevel->representation->provider, 0,
		sizeof(toplevel->representation->provider));
	toplevel->representation->provider_active = false;
	toplevel->representation->provider_anchor = NULL;
	toplevel->representation->provider_owner = NULL;
	toplevel->representation->state = NULL;
	toplevel->representation->state_size = 0;
	plugin_representation_cache_clear(toplevel);
}

void shady_plugin_representation_cleanup_owner(struct shady_server *server, void *owner) {
	if (!server || !owner) return;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (!toplevel->representation) continue;
		if (toplevel->representation->provider_owner == owner)
			plugin_representation_provider_detach(toplevel);
		if (toplevel->representation->owner == owner) {
			toplevel->representation->override = false;
			toplevel->representation->owner = NULL;
			memset(&toplevel->representation->base, 0, sizeof(toplevel->representation->base));
		}
		representation_release_if_unused(toplevel);
	}
}

void shady_plugin_window_cleanup(struct shady_toplevel *toplevel) {
	plugin_representation_provider_detach(toplevel);
	plugin_representation_cache_clear(toplevel);
	free(toplevel->representation);
	toplevel->representation = NULL;
}

void shady_plugin_representation_tick(struct shady_server *server, float dt) {
	if (!server || dt <= 0.f) return;
	bool needs_render = false;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (!toplevel->mapped || !toplevel->representation || !toplevel->representation->provider_active)
			continue;
		shady_representation_update_callback callback =
			toplevel->representation->provider.update;
		if (!callback || !plugin_callback_owned_by(
				toplevel->representation->provider_owner, (void *)callback))
			continue;
		if (callback((shady_host)server, (shady_window)toplevel, dt,
				toplevel->representation->state,
				toplevel->representation->provider.user_data))
			needs_render = true;
	}
	if (needs_render && server->renderer)
		shady_render_schedule_all_outputs(server);
}

bool shady_toplevel_representation_base(const struct shady_toplevel *toplevel,
		struct shady_window_representation *representation) {
	if (!toplevel || !representation) return false;
	if (!toplevel->representation) return false;
	if (toplevel->representation->provider_active) {
		*representation = toplevel->representation->provider.base;
		return representation->kind == SHADY_WINDOW_REPRESENTATION_BOX ||
			representation->kind == SHADY_WINDOW_REPRESENTATION_MESH;
	}
	if (!toplevel->representation->override) return false;
	*representation = toplevel->representation->base;
	return representation->kind == SHADY_WINDOW_REPRESENTATION_BOX ||
		representation->kind == SHADY_WINDOW_REPRESENTATION_MESH;
}

bool shady_toplevel_representation_model(const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		struct shady_representation_model *model) {
	if (!toplevel || !context || !model) return false;
	struct shady_window_representation base;
	if (!shady_toplevel_representation_base(toplevel, &base)) return false;
	*model = (struct shady_representation_model){
		.struct_size = sizeof(*model),
		.center_x = context->center_x,
		.center_y = context->center_y,
		.center_z = context->center_z,
		.width = base.width,
		.height = base.height,
		.depth = base.depth,
		.tilt_x = context->tilt_x,
		.tilt_y = context->tilt_y,
		.hide_titlebar = base.hide_titlebar,
	};
	shady_representation_model_callback callback =
		toplevel->representation->provider.model;
	if (toplevel->representation->provider_active && callback &&
			plugin_callback_owned_by(toplevel->representation->provider_owner,
				(void *)callback)) {
		struct shady_representation_model custom = *model;
		if (callback((shady_host)toplevel->server, (shady_window)toplevel,
				context, &custom, toplevel->representation->state,
				toplevel->representation->provider.user_data) &&
				custom.struct_size >= sizeof(custom) && custom.width > 0.f &&
				custom.height > 0.f && custom.depth > 0.f)
			*model = custom;
	}
	return true;
}

bool shady_toplevel_representation_mesh(const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		struct shady_representation_mesh *mesh) {
	if (!toplevel || !context || !mesh || !toplevel->representation ||
			!toplevel->representation->provider_active ||
			toplevel->representation->provider.base.kind !=
				SHADY_WINDOW_REPRESENTATION_MESH)
		return false;
	shady_representation_mesh_callback callback =
		toplevel->representation->provider.mesh;
	if (!callback || !plugin_callback_owned_by(
			toplevel->representation->provider_owner, (void *)callback))
		return false;
	struct shady_representation_mesh resolved = {
		.struct_size = sizeof(resolved),
	};
	if (!callback((shady_host)toplevel->server, (shady_window)toplevel,
			context, &resolved, toplevel->representation->state,
			toplevel->representation->provider.user_data))
		return false;
	if (resolved.struct_size < sizeof(resolved) || !resolved.vertices ||
			resolved.vertex_count < 3 ||
			resolved.vertex_count > SHADY_MAX_REPRESENTATION_VERTICES)
		return false;
	struct shady_toplevel *mutable_toplevel = (struct shady_toplevel *)toplevel;
	if (!representation_cache_ensure(mutable_toplevel)) return false;
	bool same_topology =
		mutable_toplevel->representation->cache->mesh_validation_cache.initialized &&
		mutable_toplevel->representation->cache->mesh_validation_cache.vertices == resolved.vertices &&
		mutable_toplevel->representation->cache->mesh_validation_cache.indices == resolved.indices &&
		mutable_toplevel->representation->cache->mesh_validation_cache.vertex_count == resolved.vertex_count &&
		mutable_toplevel->representation->cache->mesh_validation_cache.index_count == resolved.index_count &&
		mutable_toplevel->representation->cache->mesh_validation_cache.revision == resolved.revision;
	if (same_topology) {
		if (!mutable_toplevel->representation->cache->mesh_validation_cache.valid)
			return false;
	} else {
		bool valid = true;
		if (resolved.indices) {
			if (resolved.index_count < 3 ||
					resolved.index_count > SHADY_MAX_REPRESENTATION_INDICES ||
					resolved.index_count % 3 != 0) {
				valid = false;
			} else {
				for (size_t i = 0; i < resolved.index_count; ++i) {
					if (resolved.indices[i] >= resolved.vertex_count) {
						valid = false;
						break;
					}
				}
			}
		} else if (resolved.index_count != 0 || resolved.vertex_count % 3 != 0) {
			valid = false;
		}
		mutable_toplevel->representation->cache->mesh_validation_cache.initialized = true;
		mutable_toplevel->representation->cache->mesh_validation_cache.valid = valid;
		mutable_toplevel->representation->cache->mesh_validation_cache.vertices = resolved.vertices;
		mutable_toplevel->representation->cache->mesh_validation_cache.indices = resolved.indices;
		mutable_toplevel->representation->cache->mesh_validation_cache.vertex_count = resolved.vertex_count;
		mutable_toplevel->representation->cache->mesh_validation_cache.index_count = resolved.index_count;
		mutable_toplevel->representation->cache->mesh_validation_cache.revision = resolved.revision;
		if (!valid) return false;
	}
	*mesh = resolved;
	return true;
}

bool shady_toplevel_representation_collision_hull(
		const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		struct shady_collision_hull *hull) {
	if (!toplevel || !toplevel->representation || !context || !hull ||
			!toplevel->representation->provider_active)
		return false;
	shady_representation_collision_hull_callback callback =
		toplevel->representation->provider.collision_hull;
	if (!callback || !plugin_callback_owned_by(
			toplevel->representation->provider_owner, (void *)callback))
		return false;
	struct shady_collision_hull resolved = {.struct_size = sizeof(resolved)};
	if (!callback((shady_host)toplevel->server, (shady_window)toplevel,
			context, &resolved, toplevel->representation->state,
			toplevel->representation->provider.user_data))
		return false;
	if (resolved.struct_size < sizeof(resolved) || !resolved.vertices ||
			!resolved.indices || resolved.vertex_count < 4 ||
			resolved.vertex_count > SHADY_MAX_COLLISION_HULL_VERTICES ||
			resolved.index_count < 12 ||
			resolved.index_count > SHADY_MAX_COLLISION_HULL_INDICES ||
			resolved.index_count % 3 != 0)
		return false;
	struct shady_toplevel *mutable_toplevel = (struct shady_toplevel *)toplevel;
	if (!representation_cache_ensure(mutable_toplevel)) return false;
	bool same_topology =
		mutable_toplevel->representation->cache->hull_validation_cache.initialized &&
		mutable_toplevel->representation->cache->hull_validation_cache.vertices == resolved.vertices &&
		mutable_toplevel->representation->cache->hull_validation_cache.indices == resolved.indices &&
		mutable_toplevel->representation->cache->hull_validation_cache.vertex_count == resolved.vertex_count &&
		mutable_toplevel->representation->cache->hull_validation_cache.index_count == resolved.index_count &&
		mutable_toplevel->representation->cache->hull_validation_cache.revision == resolved.revision;
	if (same_topology) {
		if (!mutable_toplevel->representation->cache->hull_validation_cache.valid)
			return false;
	} else {
		bool valid = true;
		for (size_t i = 0; i < resolved.index_count; ++i) {
			if (resolved.indices[i] >= resolved.vertex_count) {
				valid = false;
				break;
			}
		}
		mutable_toplevel->representation->cache->hull_validation_cache.initialized = true;
		mutable_toplevel->representation->cache->hull_validation_cache.valid = valid;
		mutable_toplevel->representation->cache->hull_validation_cache.vertices = resolved.vertices;
		mutable_toplevel->representation->cache->hull_validation_cache.indices = resolved.indices;
		mutable_toplevel->representation->cache->hull_validation_cache.vertex_count = resolved.vertex_count;
		mutable_toplevel->representation->cache->hull_validation_cache.index_count = resolved.index_count;
		mutable_toplevel->representation->cache->hull_validation_cache.revision = resolved.revision;
		if (!valid) return false;
	}
	*hull = resolved;
	return true;
}

bool shady_toplevel_representation_collision_hull_world(
		struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		const struct shady_representation_model *model,
		const float (**vertices_world)[3], size_t *vertex_count,
		const uint16_t **indices, size_t *index_count,
		float center[3], float half[3]) {
#if SHADY_HAS_SPATIAL
	if (!toplevel || !context || !model || !vertices_world || !vertex_count ||
			!indices || !index_count || !center || !half)
		return false;
	struct shady_collision_hull hull = {0};
	if (!shady_toplevel_representation_collision_hull(toplevel, context, &hull))
		return false;
	float key[5] = {
		model->width, model->height, model->depth,
		model->tilt_x, model->tilt_y,
	};
	bool cache_hit = toplevel->representation->cache->hull_world_cache.valid &&
		toplevel->representation->cache->hull_world_cache.revision == hull.revision &&
		toplevel->representation->cache->hull_world_cache.vertices == hull.vertices &&
		toplevel->representation->cache->hull_world_cache.indices == hull.indices &&
		toplevel->representation->cache->hull_world_cache.vertex_count == hull.vertex_count &&
		toplevel->representation->cache->hull_world_cache.index_count == hull.index_count &&
		memcmp(toplevel->representation->cache->hull_world_cache.shape_key,
			key, sizeof(key)) == 0;
	if (!cache_hit) {
		float transform[16];
		shady_window_box_model(transform,
			0.f, 0.f, 0.f,
			model->width, model->height, model->depth,
			model->tilt_x, model->tilt_y);
		float minv[3] = {INFINITY, INFINITY, INFINITY};
		float maxv[3] = {-INFINITY, -INFINITY, -INFINITY};
		for (size_t i = 0; i < hull.vertex_count; ++i) {
			const struct shady_collision_vertex *v = &hull.vertices[i];
			float *world = toplevel->representation->cache->hull_world_cache.vertices_world[i];
			world[0] = transform[0] * v->x + transform[4] * v->y +
				transform[8] * v->z + transform[12];
			world[1] = transform[1] * v->x + transform[5] * v->y +
				transform[9] * v->z + transform[13];
			world[2] = transform[2] * v->x + transform[6] * v->y +
				transform[10] * v->z + transform[14];
			for (int axis = 0; axis < 3; ++axis) {
				if (world[axis] < minv[axis]) minv[axis] = world[axis];
				if (world[axis] > maxv[axis]) maxv[axis] = world[axis];
			}
		}
		for (int axis = 0; axis < 3; ++axis) {
			toplevel->representation->cache->hull_world_cache.center_offset[axis] =
				(minv[axis] + maxv[axis]) * .5f;
			toplevel->representation->cache->hull_world_cache.half[axis] =
				fmaxf((maxv[axis] - minv[axis]) * .5f, .001f);
			for (size_t i = 0; i < hull.vertex_count; ++i)
				toplevel->representation->cache->hull_world_cache.vertices_world[i][axis] -=
					toplevel->representation->cache->hull_world_cache.center_offset[axis];
		}
		toplevel->representation->cache->hull_world_cache.valid = true;
		toplevel->representation->cache->hull_world_cache.revision = hull.revision;
		toplevel->representation->cache->hull_world_cache.vertices = hull.vertices;
		toplevel->representation->cache->hull_world_cache.indices = hull.indices;
		toplevel->representation->cache->hull_world_cache.vertex_count = hull.vertex_count;
		toplevel->representation->cache->hull_world_cache.index_count = hull.index_count;
		memcpy(toplevel->representation->cache->hull_world_cache.shape_key,
			key, sizeof(key));
	}
	*vertices_world = (const float (*)[3])
		toplevel->representation->cache->hull_world_cache.vertices_world;
	*vertex_count = toplevel->representation->cache->hull_world_cache.vertex_count;
	*indices = (const uint16_t *)
		toplevel->representation->cache->hull_world_cache.indices;
	*index_count = toplevel->representation->cache->hull_world_cache.index_count;
	center[0] = model->center_x +
		toplevel->representation->cache->hull_world_cache.center_offset[0];
	center[1] = model->center_y +
		toplevel->representation->cache->hull_world_cache.center_offset[1];
	center[2] = model->center_z +
		toplevel->representation->cache->hull_world_cache.center_offset[2];
	memcpy(half, toplevel->representation->cache->hull_world_cache.half,
		3 * sizeof(float));
	return true;
#else
	(void)toplevel; (void)context; (void)model; (void)vertices_world;
	(void)vertex_count; (void)indices; (void)index_count; (void)center; (void)half;
	return false;
#endif
}

static bool shady_toplevel_representation_collision_compound_raw(
		struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		struct shady_collision_compound *compound) {
	if (!toplevel || !toplevel->representation || !context || !compound ||
			!toplevel->representation->provider_active)
		return false;
	shady_representation_collision_compound_callback callback =
		toplevel->representation->provider.collision_compound;
	if (!callback || !plugin_callback_owned_by(
			toplevel->representation->provider_owner, (void *)callback))
		return false;
	struct shady_collision_compound resolved = {.struct_size = sizeof(resolved)};
	if (!callback((shady_host)toplevel->server, (shady_window)toplevel,
			context, &resolved, toplevel->representation->state,
			toplevel->representation->provider.user_data))
		return false;
	if (resolved.struct_size < sizeof(resolved) || !resolved.parts ||
			resolved.part_count < 1 || resolved.part_count > 8)
		return false;
	if (!representation_cache_ensure(toplevel)) return false;
	bool same_topology =
		toplevel->representation->cache->compound_validation_cache.initialized &&
		toplevel->representation->cache->compound_validation_cache.parts == resolved.parts &&
		toplevel->representation->cache->compound_validation_cache.part_count == resolved.part_count &&
		toplevel->representation->cache->compound_validation_cache.revision == resolved.revision;
	if (same_topology) {
		if (!toplevel->representation->cache->compound_validation_cache.valid)
			return false;
	} else {
		bool valid = true;
		size_t total_vertices = 0, total_indices = 0;
		for (size_t part = 0; part < resolved.part_count && valid; ++part) {
			const struct shady_collision_hull *hull = &resolved.parts[part];
			if (hull->struct_size < sizeof(*hull) || !hull->vertices ||
					!hull->indices || hull->vertex_count < 4 || hull->index_count < 12 ||
					hull->index_count % 3 != 0) {
				valid = false;
				break;
			}
			total_vertices += hull->vertex_count;
			total_indices += hull->index_count;
			if (total_vertices > SHADY_MAX_COLLISION_HULL_VERTICES ||
					total_indices > SHADY_MAX_COLLISION_HULL_INDICES) {
				valid = false;
				break;
			}
			for (size_t i = 0; i < hull->index_count; ++i) {
				if (hull->indices[i] >= hull->vertex_count) {
					valid = false;
					break;
				}
			}
		}
		toplevel->representation->cache->compound_validation_cache.initialized = true;
		toplevel->representation->cache->compound_validation_cache.valid = valid;
		toplevel->representation->cache->compound_validation_cache.parts = resolved.parts;
		toplevel->representation->cache->compound_validation_cache.part_count = resolved.part_count;
		toplevel->representation->cache->compound_validation_cache.revision = resolved.revision;
		if (!valid) return false;
	}
	*compound = resolved;
	return true;
}

bool shady_toplevel_representation_collision_compound_world(
		struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		const struct shady_representation_model *model,
		struct shady_resolved_collision_compound *compound) {
#if SHADY_HAS_SPATIAL
	if (!toplevel || !context || !model || !compound) return false;
	memset(compound, 0, sizeof(*compound));
	struct shady_collision_compound raw = {0};
	if (!shady_toplevel_representation_collision_compound_raw(toplevel, context, &raw)) {
		const float (*vertices)[3] = NULL;
		size_t vertex_count = 0, index_count = 0;
		const uint16_t *indices = NULL;
		if (!shady_toplevel_representation_collision_hull_world(toplevel,
				context, model, &vertices, &vertex_count, &indices, &index_count,
				compound->center, compound->half))
			return false;
		compound->part_count = 1;
		compound->parts[0] = (struct shady_resolved_collision_part){
			.vertices = vertices,
			.vertex_count = vertex_count,
			.indices = indices,
			.index_count = index_count,
		};
		return true;
	}

	float key[5] = {
		model->width, model->height, model->depth,
		model->tilt_x, model->tilt_y,
	};
	bool cache_hit = toplevel->representation->cache->compound_world_cache.valid &&
		toplevel->representation->cache->compound_world_cache.revision == raw.revision &&
		toplevel->representation->cache->compound_world_cache.parts == raw.parts &&
		toplevel->representation->cache->compound_world_cache.part_count == raw.part_count &&
		memcmp(toplevel->representation->cache->compound_world_cache.shape_key,
			key, sizeof(key)) == 0;
	if (!cache_hit) {
		float transform[16];
		shady_window_box_model(transform, 0.f, 0.f, 0.f,
			model->width, model->height, model->depth,
			model->tilt_x, model->tilt_y);
		float minv[3] = {INFINITY, INFINITY, INFINITY};
		float maxv[3] = {-INFINITY, -INFINITY, -INFINITY};
		size_t vertex_cursor = 0, index_cursor = 0;
		for (size_t part = 0; part < raw.part_count; ++part) {
			const struct shady_collision_hull *hull = &raw.parts[part];
			toplevel->representation->cache->compound_world_cache.part_vertex_offset[part] =
				vertex_cursor;
			toplevel->representation->cache->compound_world_cache.part_vertex_count[part] =
				hull->vertex_count;
			toplevel->representation->cache->compound_world_cache.part_index_offset[part] =
				index_cursor;
			toplevel->representation->cache->compound_world_cache.part_index_count[part] =
				hull->index_count;
			for (size_t i = 0; i < hull->vertex_count; ++i) {
				const struct shady_collision_vertex *v = &hull->vertices[i];
				float *world = toplevel->representation->cache->compound_world_cache
					.vertices_world[vertex_cursor + i];
				world[0] = transform[0] * v->x + transform[4] * v->y +
					transform[8] * v->z + transform[12];
				world[1] = transform[1] * v->x + transform[5] * v->y +
					transform[9] * v->z + transform[13];
				world[2] = transform[2] * v->x + transform[6] * v->y +
					transform[10] * v->z + transform[14];
				for (int axis = 0; axis < 3; ++axis) {
					if (world[axis] < minv[axis]) minv[axis] = world[axis];
					if (world[axis] > maxv[axis]) maxv[axis] = world[axis];
				}
			}
			memcpy(&toplevel->representation->cache->compound_world_cache.indices[index_cursor],
				hull->indices, hull->index_count * sizeof(uint16_t));
			vertex_cursor += hull->vertex_count;
			index_cursor += hull->index_count;
		}
		for (int axis = 0; axis < 3; ++axis) {
			toplevel->representation->cache->compound_world_cache.center_offset[axis] =
				(minv[axis] + maxv[axis]) * .5f;
			toplevel->representation->cache->compound_world_cache.half[axis] =
				fmaxf((maxv[axis] - minv[axis]) * .5f, .001f);
			for (size_t i = 0; i < vertex_cursor; ++i)
				toplevel->representation->cache->compound_world_cache.vertices_world[i][axis] -=
					toplevel->representation->cache->compound_world_cache.center_offset[axis];
		}
		toplevel->representation->cache->compound_world_cache.valid = true;
		toplevel->representation->cache->compound_world_cache.revision = raw.revision;
		toplevel->representation->cache->compound_world_cache.parts = raw.parts;
		toplevel->representation->cache->compound_world_cache.part_count = raw.part_count;
		memcpy(toplevel->representation->cache->compound_world_cache.shape_key,
			key, sizeof(key));
	}
	compound->part_count = toplevel->representation->cache->compound_world_cache.part_count;
	for (size_t part = 0; part < compound->part_count; ++part) {
		size_t vo = toplevel->representation->cache->compound_world_cache.part_vertex_offset[part];
		size_t io = toplevel->representation->cache->compound_world_cache.part_index_offset[part];
		compound->parts[part].vertices = (const float (*)[3])
			&toplevel->representation->cache->compound_world_cache.vertices_world[vo];
		compound->parts[part].vertex_count =
			toplevel->representation->cache->compound_world_cache.part_vertex_count[part];
		compound->parts[part].indices =
			&toplevel->representation->cache->compound_world_cache.indices[io];
		compound->parts[part].index_count =
			toplevel->representation->cache->compound_world_cache.part_index_count[part];
	}
	const float model_center[3] = {
		model->center_x, model->center_y, model->center_z,
	};
	for (int axis = 0; axis < 3; ++axis) {
		compound->center[axis] = model_center[axis] +
			toplevel->representation->cache->compound_world_cache.center_offset[axis];
		compound->half[axis] =
			toplevel->representation->cache->compound_world_cache.half[axis];
	}
	return true;
#else
	(void)toplevel; (void)context; (void)model; (void)compound;
	return false;
#endif
}

bool shady_toplevel_representation_collision(const struct shady_toplevel *toplevel,
		const struct shady_representation_context *context,
		const struct shady_representation_model *model,
		struct shady_collision_box *box) {
	if (!toplevel || !context || !model || !box) return false;
	*box = (struct shady_collision_box){
		.struct_size = sizeof(*box),
		.center = {model->center_x, model->center_y, model->center_z},
		.half = {model->width * .5f, model->height * .5f, model->depth * .5f},
	};

	if (!toplevel->representation) return true;

#if SHADY_HAS_SPATIAL
	struct shady_window_representation base = {0};
	if (shady_toplevel_representation_base(toplevel, &base) &&
			base.kind == SHADY_WINDOW_REPRESENTATION_MESH) {
		struct shady_representation_mesh mesh = {0};
		if (shady_toplevel_representation_mesh(toplevel, context, &mesh)) {
			float transform[16];
			shady_window_box_model(transform,
				model->center_x, model->center_y, model->center_z,
				model->width, model->height, model->depth,
				model->tilt_x, model->tilt_y);
			shady_mesh_bounds(transform, (const float *)mesh.vertices,
				mesh.vertex_count, mesh.indices, mesh.index_count,
				box->center, box->half);
		}
	}
#endif
	shady_representation_collision_callback callback =
		toplevel->representation->provider.collision;
	if (toplevel->representation->provider_active && callback &&
			plugin_callback_owned_by(toplevel->representation->provider_owner,
				(void *)callback)) {
		struct shady_collision_box custom = *box;
		if (callback((shady_host)toplevel->server, (shady_window)toplevel,
				context, &custom, toplevel->representation->state,
				toplevel->representation->provider.user_data) &&
				custom.struct_size >= sizeof(custom) && custom.half[0] > 0.f &&
				custom.half[1] > 0.f && custom.half[2] > 0.f)
			*box = custom;
	}
	return true;
}

bool host_window_set_representation(shady_host host, shady_window window,
		const struct shady_window_representation *representation) {
	if (!shady_plugin_window_valid(host, window) || !representation ||
			representation->struct_size < sizeof(*representation) ||
			representation->kind != SHADY_WINDOW_REPRESENTATION_BOX ||
			representation->width <= 0.f || representation->height <= 0.f ||
			representation->depth <= 0.f)
		return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	if (!owner) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->representation) {
		toplevel->representation = calloc(1, sizeof(*toplevel->representation));
		if (!toplevel->representation) return false;
	}

	if (toplevel->representation->provider_active &&
			toplevel->representation->provider_owner != owner)
		return false;
	if (toplevel->representation && toplevel->representation->provider_owner == owner)
		plugin_representation_provider_detach(toplevel);
	toplevel->representation->base = *representation;
	toplevel->representation->override = true;
	toplevel->representation->owner = owner;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

bool host_window_reset_representation(shady_host host, shady_window window) {
	if (!shady_plugin_window_valid(host, window)) return false;
	void *owner = shady_plugin_owner_from_address(__builtin_return_address(0));
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->representation) return true;
	if (toplevel->representation->owner && owner &&
			toplevel->representation->owner != owner)
		return false;
	memset(&toplevel->representation->base, 0,
		sizeof(toplevel->representation->base));
	toplevel->representation->override = false;
	toplevel->representation->owner = NULL;
	representation_release_if_unused(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

bool host_window_set_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider) {
	const size_t provider_v1_size =
		offsetof(struct shady_window_representation_provider, user_data) +
		sizeof(provider->user_data);
	if (!shady_plugin_window_valid(host, window) || !provider ||
			provider->struct_size < provider_v1_size)
		return false;
	struct shady_window_representation_provider normalized = {0};
	size_t copy_size = provider->struct_size < sizeof(normalized)
		? provider->struct_size : sizeof(normalized);
	memcpy(&normalized, provider, copy_size);
	if (normalized.base.struct_size < sizeof(normalized.base) ||
			(normalized.base.kind != SHADY_WINDOW_REPRESENTATION_BOX &&
			 normalized.base.kind != SHADY_WINDOW_REPRESENTATION_MESH) ||
			normalized.base.width <= 0.f || normalized.base.height <= 0.f ||
			normalized.base.depth <= 0.f)
		return false;
	void *owner = shady_plugin_owner_from_address((void *)provider);
	if (!owner || normalized.state_size > SHADY_MAX_REPRESENTATION_STATE ||
			!plugin_representation_provider_callbacks_valid(owner, &normalized) ||
			(normalized.base.kind == SHADY_WINDOW_REPRESENTATION_MESH && !normalized.mesh))
		return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->representation) {
		toplevel->representation = calloc(1, sizeof(*toplevel->representation));
		if (!toplevel->representation) return false;
	}

	if (toplevel->representation->override &&
			toplevel->representation->owner != owner)
		return false;
	if (toplevel->representation->provider_active &&
			toplevel->representation->provider_owner != owner)
		return false;
	void *state = normalized.state_size > 0 ? calloc(1, normalized.state_size) : NULL;
	if (normalized.state_size > 0 && !state) {
		representation_release_if_unused(toplevel);
		return false;
	}
	if (normalized.state_init &&
			!normalized.state_init(host, window, state, normalized.user_data)) {
		free(state);
		representation_release_if_unused(toplevel);
		return false;
	}
	if (toplevel->representation->provider_active)
		plugin_representation_provider_detach(toplevel);
	if (toplevel->representation->owner == owner) {
		memset(&toplevel->representation->base, 0,
			sizeof(toplevel->representation->base));
		toplevel->representation->override = false;
		toplevel->representation->owner = NULL;
	}
	toplevel->representation->provider = normalized;
	toplevel->representation->provider_active = true;
	toplevel->representation->provider_anchor = provider;
	toplevel->representation->provider_owner = owner;
	toplevel->representation->state = state;
	toplevel->representation->state_size = normalized.state_size;
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

bool host_window_reset_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider) {
	if (!shady_plugin_window_valid(host, window) || !provider) return false;
	void *owner = shady_plugin_owner_from_address((void *)provider);
	if (!owner) return false;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->representation) return false;
	if (!toplevel->representation->provider_active ||
			toplevel->representation->provider_owner != owner ||
			toplevel->representation->provider_anchor != provider)
		return false;
	plugin_representation_provider_detach(toplevel);
	representation_release_if_unused(toplevel);
	if (HOST(host)->renderer) shady_render_schedule_all_outputs(HOST(host));
	return true;
}

void *host_window_representation_state(shady_host host, shady_window window,
		const struct shady_window_representation_provider *provider,
		size_t *state_size) {
	if (state_size) *state_size = 0;
	if (!shady_plugin_window_valid(host, window) || !provider) return NULL;
	void *owner = shady_plugin_owner_from_address((void *)provider);
	if (!owner) return NULL;
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel->representation) return NULL;
	if (!toplevel->representation->provider_active ||
			toplevel->representation->provider_owner != owner ||
			toplevel->representation->provider_anchor != provider)
		return NULL;
	if (state_size) *state_size = toplevel->representation->state_size;
	return toplevel->representation->state;
}

bool host_window_representation(shady_window window,
		struct shady_window_representation *representation, bool *overridden) {
	struct shady_toplevel *toplevel = WINDOW(window);
	if (!toplevel || !representation) return false;
	if (toplevel->representation && toplevel->representation->provider_active)
		*representation = toplevel->representation->provider.base;
	else if (toplevel->representation && toplevel->representation->override)
		*representation = toplevel->representation->base;
	else
		*representation = (struct shady_window_representation){
			.struct_size = sizeof(*representation),
			.kind = SHADY_WINDOW_REPRESENTATION_DEFAULT,
		};
	if (overridden) *overridden = toplevel->representation &&
		(toplevel->representation->override || toplevel->representation->provider_active);
	return true;
}


const struct shady_representation_api_v1 shady_representation_api = {
    .struct_size = sizeof(shady_representation_api),
    .set = host_window_set_representation,
    .reset = host_window_reset_representation,
    .get = host_window_representation,
    .attach_provider = host_window_set_representation_provider,
    .detach_provider = host_window_reset_representation_provider,
    .provider_state = host_window_representation_state,
};
