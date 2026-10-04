#include "internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "../spatial/state.h"
#include "../../shady.h"

/* Visual geometry is uploaded as one GL buffer; keep it well below what a
 * GLsizei draw count and a single allocation can represent. */
#define SHADY_ENVIRONMENT_MAX_VERTICES (16u * 1024u * 1024u)

static bool finite3(const float v[3]) {
	return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static bool vertex_valid(const struct shady_environment_vertex *v) {
	return finite3(v->position) && finite3(v->normal) &&
		isfinite(v->uv[0]) && isfinite(v->uv[1]);
}

static bool box_valid(const struct shady_environment_box *b) {
	return finite3(b->min) && finite3(b->max) &&
		b->min[0] <= b->max[0] && b->min[1] <= b->max[1] && b->min[2] <= b->max[2];
}

static bool triangle_valid(const struct shady_environment_triangle *t) {
	return finite3(t->v[0]) && finite3(t->v[1]) && finite3(t->v[2]);
}

static void *dup_array(const void *src, size_t count, size_t size) {
	if (!count) return NULL;
	void *dst = malloc(count * size);
	if (dst) memcpy(dst, src, count * size);
	return dst;
}

size_t shady_environment_box_capacity(void) {
	struct shady_world world = shady_world_default();
	return SHADY_WORLD_MAX_COLLIDERS - world.collider_count;
}

size_t shady_environment_triangle_capacity(void) {
	struct shady_world world = shady_world_default();
	return SHADY_WORLD_MAX_TRIANGLES - world.triangle_count;
}

bool shady_environment_scene_copy(struct shady_environment_scene_data *out,
		const struct shady_environment_scene *scene, size_t max_boxes,
		size_t max_triangles, const char *label) {
	*out = (struct shady_environment_scene_data){0};
	if (!scene || scene->struct_size < sizeof(*scene)) {
		wlr_log(WLR_ERROR, "environment: %s: invalid scene descriptor", label);
		return false;
	}
	if ((scene->vertex_count && !scene->vertices) ||
			(scene->index_count && !scene->indices) ||
			(scene->box_count && !scene->boxes) ||
			(scene->triangle_count && !scene->triangles)) {
		wlr_log(WLR_ERROR, "environment: %s: array count without data", label);
		return false;
	}

	size_t draw_count = scene->indices ? scene->index_count : scene->vertex_count;
	if (draw_count % 3 || draw_count > SHADY_ENVIRONMENT_MAX_VERTICES ||
			scene->vertex_count > SHADY_ENVIRONMENT_MAX_VERTICES) {
		wlr_log(WLR_ERROR, "environment: %s: %zu vertices is not a valid triangle list",
			label, draw_count);
		return false;
	}
	if (scene->box_count > max_boxes || scene->triangle_count > max_triangles) {
		wlr_log(WLR_ERROR,
			"environment: %s: collision exceeds world capacity "
			"(%zu/%zu boxes, %zu/%zu triangles)", label,
			scene->box_count, max_boxes, scene->triangle_count, max_triangles);
		return false;
	}

	for (size_t i = 0; i < scene->vertex_count; ++i) {
		if (!vertex_valid(&scene->vertices[i])) {
			wlr_log(WLR_ERROR, "environment: %s: non-finite vertex %zu", label, i);
			return false;
		}
	}
	for (size_t i = 0; scene->indices && i < scene->index_count; ++i) {
		if (scene->indices[i] >= scene->vertex_count) {
			wlr_log(WLR_ERROR, "environment: %s: index %zu out of range", label, i);
			return false;
		}
	}
	for (size_t i = 0; i < scene->box_count; ++i) {
		if (!box_valid(&scene->boxes[i])) {
			wlr_log(WLR_ERROR, "environment: %s: invalid collision box %zu", label, i);
			return false;
		}
	}
	for (size_t i = 0; i < scene->triangle_count; ++i) {
		if (!triangle_valid(&scene->triangles[i])) {
			wlr_log(WLR_ERROR, "environment: %s: non-finite collision triangle %zu",
				label, i);
			return false;
		}
	}

	if (draw_count) {
		out->vertices = malloc(draw_count * sizeof(*out->vertices));
		if (!out->vertices) goto oom;
		for (size_t i = 0; i < draw_count; ++i) {
			size_t src = scene->indices ? scene->indices[i] : i;
			out->vertices[i] = scene->vertices[src];
		}
		out->vertex_count = draw_count;
	}
	out->boxes = dup_array(scene->boxes, scene->box_count, sizeof(*out->boxes));
	if (scene->box_count && !out->boxes) goto oom;
	out->box_count = scene->box_count;
	out->triangles = dup_array(scene->triangles, scene->triangle_count,
		sizeof(*out->triangles));
	if (scene->triangle_count && !out->triangles) goto oom;
	out->triangle_count = scene->triangle_count;
	return true;

oom:
	wlr_log(WLR_ERROR, "environment: %s: out of memory", label);
	shady_environment_scene_free(out);
	return false;
}

void shady_environment_scene_free(struct shady_environment_scene_data *scene) {
	free(scene->vertices);
	free(scene->boxes);
	free(scene->triangles);
	*scene = (struct shady_environment_scene_data){0};
}

static struct shady_box_collider to_box_collider(const struct shady_environment_box *b) {
	return (struct shady_box_collider){
		.min_x = b->min[0], .max_x = b->max[0],
		.min_y = b->min[1], .max_y = b->max[1],
		.min_z = b->min[2], .max_z = b->max[2],
	};
}

static struct shady_triangle_collider to_triangle_collider(
		const struct shady_environment_triangle *t) {
	struct shady_triangle_collider out;
	memcpy(out.v, t->v, sizeof(out.v));
	for (int a = 0; a < 3; ++a) {
		out.min[a] = out.max[a] = t->v[0][a];
		for (int j = 1; j < 3; ++j) {
			if (t->v[j][a] < out.min[a]) out.min[a] = t->v[j][a];
			if (t->v[j][a] > out.max[a]) out.max[a] = t->v[j][a];
		}
	}
	return out;
}

void shady_environment_apply_collision(struct shady_server *server,
		const struct shady_environment_scene_data *scene) {
	struct shady_spatial_state *spatial = shady_spatial_state(server);
	if (!spatial) return;
	struct shady_world *world = &spatial->runtime.world;
	*world = shady_world_default();
	/* Capacity was checked when the scene was copied. */
	for (size_t i = 0; i < scene->box_count; ++i)
		shady_world_add_collider(world, to_box_collider(&scene->boxes[i]));
	for (size_t i = 0; i < scene->triangle_count; ++i) {
		struct shady_triangle_collider t = to_triangle_collider(&scene->triangles[i]);
		shady_world_add_triangle(world, &t);
	}
}
