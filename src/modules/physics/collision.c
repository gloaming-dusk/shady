#include "collision.h"

#include <math.h>

static float dot3(const float a[3], const float b[3]) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool sat_axis(const float vertices[3][3], const float half[3],
		const float axis[3]) {
	float length_sq = dot3(axis, axis);
	if (length_sq < 1e-12f) return true;

	float p0 = dot3(vertices[0], axis);
	float p1 = dot3(vertices[1], axis);
	float p2 = dot3(vertices[2], axis);
	float min_projection = fminf(p0, fminf(p1, p2));
	float max_projection = fmaxf(p0, fmaxf(p1, p2));
	float radius = half[0] * fabsf(axis[0]) +
		half[1] * fabsf(axis[1]) +
		half[2] * fabsf(axis[2]);
	return !(min_projection > radius || max_projection < -radius);
}

bool shady_physics_triangle_cube_overlap(
		const struct shady_triangle_collider *triangle,
		const float center[3], const float half[3]) {
	float vertices[3][3];
	for (int i = 0; i < 3; i++) {
		for (int axis = 0; axis < 3; axis++) {
			vertices[i][axis] = triangle->v[i][axis] - center[axis];
		}
	}

	for (int axis = 0; axis < 3; axis++) {
		float min_value = fminf(vertices[0][axis],
			fminf(vertices[1][axis], vertices[2][axis]));
		float max_value = fmaxf(vertices[0][axis],
			fmaxf(vertices[1][axis], vertices[2][axis]));
		if (min_value > half[axis] || max_value < -half[axis])
			return false;
	}

	float edges[3][3];
	for (int i = 0; i < 3; i++) {
		for (int axis = 0; axis < 3; axis++) {
			edges[i][axis] =
				vertices[(i + 1) % 3][axis] - vertices[i][axis];
		}
	}

	float normal[3] = {
		edges[0][1] * edges[1][2] - edges[0][2] * edges[1][1],
		edges[0][2] * edges[1][0] - edges[0][0] * edges[1][2],
		edges[0][0] * edges[1][1] - edges[0][1] * edges[1][0],
	};
	if (!sat_axis(vertices, half, normal)) return false;

	const float box_axes[3][3] = {
		{1.f, 0.f, 0.f},
		{0.f, 1.f, 0.f},
		{0.f, 0.f, 1.f},
	};
	for (int edge = 0; edge < 3; edge++) {
		for (int box_axis = 0; box_axis < 3; box_axis++) {
			float axis[3] = {
				edges[edge][1] * box_axes[box_axis][2] -
					edges[edge][2] * box_axes[box_axis][1],
				edges[edge][2] * box_axes[box_axis][0] -
					edges[edge][0] * box_axes[box_axis][2],
				edges[edge][0] * box_axes[box_axis][1] -
					edges[edge][1] * box_axes[box_axis][0],
			};
			if (!sat_axis(vertices, half, axis)) return false;
		}
	}

	return true;
}

static bool convex_sat_axis(const struct shady_triangle_collider *triangle,
		const float (*vertices)[3], size_t vertex_count,
		const float offset[3], const float axis[3]) {
	float length_sq = dot3(axis, axis);
	if (length_sq < 1e-12f) return true;
	float tri_min = dot3(triangle->v[0], axis);
	float tri_max = tri_min;
	for (int i = 1; i < 3; ++i) {
		float p = dot3(triangle->v[i], axis);
		if (p < tri_min) tri_min = p;
		if (p > tri_max) tri_max = p;
	}
	float first[3] = {
		vertices[0][0] + offset[0],
		vertices[0][1] + offset[1],
		vertices[0][2] + offset[2],
	};
	float hull_min = dot3(first, axis);
	float hull_max = hull_min;
	for (size_t i = 1; i < vertex_count; ++i) {
		float p3[3] = {
			vertices[i][0] + offset[0],
			vertices[i][1] + offset[1],
			vertices[i][2] + offset[2],
		};
		float p = dot3(p3, axis);
		if (p < hull_min) hull_min = p;
		if (p > hull_max) hull_max = p;
	}
	return !(hull_min > tri_max || tri_min > hull_max);
}

bool shady_physics_triangle_convex_overlap(
		const struct shady_triangle_collider *triangle,
		const float (*vertices)[3], size_t vertex_count,
		const uint16_t *indices, size_t index_count,
		const float offset[3]) {
	if (!triangle || !vertices || vertex_count < 4 || !indices ||
			index_count < 12 || index_count % 3 != 0)
		return false;
	const float zero_offset[3] = {0.f, 0.f, 0.f};
	if (!offset) offset = zero_offset;

	float tri_edges[3][3];
	for (int i = 0; i < 3; ++i) {
		for (int axis = 0; axis < 3; ++axis)
			tri_edges[i][axis] = triangle->v[(i + 1) % 3][axis] -
				triangle->v[i][axis];
	}
	float tri_normal[3] = {
		tri_edges[0][1] * tri_edges[1][2] - tri_edges[0][2] * tri_edges[1][1],
		tri_edges[0][2] * tri_edges[1][0] - tri_edges[0][0] * tri_edges[1][2],
		tri_edges[0][0] * tri_edges[1][1] - tri_edges[0][1] * tri_edges[1][0],
	};
	if (!convex_sat_axis(triangle, vertices, vertex_count, offset, tri_normal))
		return false;

	for (size_t i = 0; i < index_count; i += 3) {
		uint16_t ia = indices[i + 0], ib = indices[i + 1], ic = indices[i + 2];
		if (ia >= vertex_count || ib >= vertex_count || ic >= vertex_count)
			return false;
		float e0[3], e1[3], face_normal[3];
		for (int axis = 0; axis < 3; ++axis) {
			e0[axis] = vertices[ib][axis] - vertices[ia][axis];
			e1[axis] = vertices[ic][axis] - vertices[ia][axis];
		}
		face_normal[0] = e0[1] * e1[2] - e0[2] * e1[1];
		face_normal[1] = e0[2] * e1[0] - e0[0] * e1[2];
		face_normal[2] = e0[0] * e1[1] - e0[1] * e1[0];
		if (!convex_sat_axis(triangle, vertices, vertex_count, offset, face_normal))
			return false;

		const uint16_t edge_indices[3][2] = {
			{ia, ib}, {ib, ic}, {ic, ia},
		};
		for (int edge = 0; edge < 3; ++edge) {
			float hull_edge[3];
			for (int axis = 0; axis < 3; ++axis)
				hull_edge[axis] =
					vertices[edge_indices[edge][1]][axis] -
					vertices[edge_indices[edge][0]][axis];
			for (int tri_edge = 0; tri_edge < 3; ++tri_edge) {
				float axis[3] = {
					hull_edge[1] * tri_edges[tri_edge][2] - hull_edge[2] * tri_edges[tri_edge][1],
					hull_edge[2] * tri_edges[tri_edge][0] - hull_edge[0] * tri_edges[tri_edge][2],
					hull_edge[0] * tri_edges[tri_edge][1] - hull_edge[1] * tri_edges[tri_edge][0],
				};
				if (!convex_sat_axis(triangle, vertices, vertex_count, offset, axis))
					return false;
			}
		}
	}
	return true;
}

static bool world_has_triangle_contact(const struct shady_world *world,
		const float center[3], const float half[3]) {
	for (size_t i = 0; i < world->triangle_count; i++) {
		const struct shady_triangle_collider *triangle = &world->triangles[i];
		bool broad_phase = true;
		for (int axis = 0; axis < 3; axis++) {
			if (triangle->max[axis] < center[axis] - half[axis] ||
					triangle->min[axis] > center[axis] + half[axis]) {
				broad_phase = false;
				break;
			}
		}
		if (broad_phase &&
				shady_physics_triangle_cube_overlap(triangle, center, half))
			return true;
	}
	return false;
}

static bool world_has_convex_triangle_contact(const struct shady_world *world,
		const float center[3], const float half[3],
		const float (*vertices)[3], size_t vertex_count,
		const uint16_t *indices, size_t index_count) {
	for (size_t i = 0; i < world->triangle_count; ++i) {
		const struct shady_triangle_collider *triangle = &world->triangles[i];
		bool broad_phase = true;
		for (int axis = 0; axis < 3; ++axis) {
			if (triangle->max[axis] < center[axis] - half[axis] ||
					triangle->min[axis] > center[axis] + half[axis]) {
				broad_phase = false;
				break;
			}
		}
		if (broad_phase && shady_physics_triangle_convex_overlap(triangle,
				vertices, vertex_count, indices, index_count, center))
			return true;
	}
	return false;
}

static bool world_has_compound_triangle_contact(const struct shady_world *world,
		const float center[3], const float half[3],
		const struct shady_physics_convex_part *parts, size_t part_count) {
	if (!parts || part_count == 0) return false;
	for (size_t i = 0; i < world->triangle_count; ++i) {
		const struct shady_triangle_collider *triangle = &world->triangles[i];
		bool broad_phase = true;
		for (int axis = 0; axis < 3; ++axis) {
			if (triangle->max[axis] < center[axis] - half[axis] ||
					triangle->min[axis] > center[axis] + half[axis]) {
				broad_phase = false;
				break;
			}
		}
		if (!broad_phase) continue;
		for (size_t part = 0; part < part_count; ++part) {
			const struct shady_physics_convex_part *p = &parts[part];
			if (shady_physics_triangle_convex_overlap(triangle,
					p->vertices, p->vertex_count, p->indices, p->index_count,
					center))
				return true;
		}
	}
	return false;
}

bool shady_physics_sweep_cube_axis(const struct shady_world *world,
		float center[3], const float half[3], int axis, float delta,
		float *velocity, float restitution) {
	if (fabsf(delta) < 1e-8f) return false;

	int a = (axis + 1) % 3;
	int b = (axis + 2) % 3;
	float start = center[axis];
	float next = start + delta;
	float best = next;
	bool hit = false;

	/* Slot zero is Shady's built-in floor. OBJ boxes follow it and are
	 * broad-phase/debug bounds only when authored triangle collision exists. */
	size_t box_count = world->triangle_count ? 1 : world->collider_count;
	for (size_t i = 0; i < box_count; i++) {
		const struct shady_box_collider *collider = &world->colliders[i];
		const float min[3] = {
			collider->min_x, collider->min_y, collider->min_z,
		};
		const float max[3] = {
			collider->max_x, collider->max_y, collider->max_z,
		};
		if (center[a] + half[a] <= min[a] ||
				center[a] - half[a] >= max[a] ||
				center[b] + half[b] <= min[b] ||
				center[b] - half[b] >= max[b])
			continue;

		if (delta > 0.f &&
				start + half[axis] <= min[axis] &&
				next + half[axis] >= min[axis]) {
			float contact = min[axis] - half[axis];
			if (!hit || contact < best) {
				best = contact;
				hit = true;
			}
		} else if (delta < 0.f &&
				start - half[axis] >= max[axis] &&
				next - half[axis] <= max[axis]) {
			float contact = max[axis] + half[axis];
			if (!hit || contact > best) {
				best = contact;
				hit = true;
			}
		}
	}

	if (!hit && world->triangle_count) {
		float probe[3] = {center[0], center[1], center[2]};
		probe[axis] = next;
		if (world_has_triangle_contact(world, probe, half)) {
			best = start;
			hit = true;
		}
	}

	center[axis] = hit ? best : next;
	if (hit && velocity) *velocity = -*velocity * restitution;
	return hit;
}

bool shady_physics_sweep_convex_axis(const struct shady_world *world,
		float center[3], const float half[3],
		const float (*vertices)[3], size_t vertex_count,
		const uint16_t *indices, size_t index_count,
		int axis, float delta, float *velocity, float restitution) {
	if (!world || !center || !half || !vertices || !indices ||
			vertex_count < 4 || index_count < 12 || fabsf(delta) < 1e-8f)
		return false;

	int a = (axis + 1) % 3;
	int b = (axis + 2) % 3;
	float start = center[axis];
	float next = start + delta;
	float best = next;
	bool hit = false;

	size_t box_count = world->triangle_count ? 1 : world->collider_count;
	for (size_t i = 0; i < box_count; ++i) {
		const struct shady_box_collider *collider = &world->colliders[i];
		const float min[3] = {collider->min_x, collider->min_y, collider->min_z};
		const float max[3] = {collider->max_x, collider->max_y, collider->max_z};
		if (center[a] + half[a] <= min[a] || center[a] - half[a] >= max[a] ||
				center[b] + half[b] <= min[b] || center[b] - half[b] >= max[b])
			continue;
		if (delta > 0.f && start + half[axis] <= min[axis] &&
				next + half[axis] >= min[axis]) {
			float contact = min[axis] - half[axis];
			if (!hit || contact < best) { best = contact; hit = true; }
		} else if (delta < 0.f && start - half[axis] >= max[axis] &&
				next - half[axis] <= max[axis]) {
			float contact = max[axis] + half[axis];
			if (!hit || contact > best) { best = contact; hit = true; }
		}
	}

	if (!hit && world->triangle_count) {
		float probe[3] = {center[0], center[1], center[2]};
		probe[axis] = next;
		if (world_has_convex_triangle_contact(world, probe, half,
				vertices, vertex_count, indices, index_count)) {
			best = start;
			hit = true;
		}
	}

	center[axis] = hit ? best : next;
	if (hit && velocity) *velocity = -*velocity * restitution;
	return hit;
}

bool shady_physics_sweep_compound_axis(const struct shady_world *world,
		float center[3], const float half[3],
		const struct shady_physics_convex_part *parts, size_t part_count,
		int axis, float delta, float *velocity, float restitution) {
	if (!world || !center || !half || !parts || part_count == 0 ||
			fabsf(delta) < 1e-8f)
		return false;

	int a = (axis + 1) % 3;
	int b = (axis + 2) % 3;
	float start = center[axis];
	float next = start + delta;
	float best = next;
	bool hit = false;

	size_t box_count = world->triangle_count ? 1 : world->collider_count;
	for (size_t i = 0; i < box_count; ++i) {
		const struct shady_box_collider *collider = &world->colliders[i];
		const float min[3] = {collider->min_x, collider->min_y, collider->min_z};
		const float max[3] = {collider->max_x, collider->max_y, collider->max_z};
		if (center[a] + half[a] <= min[a] || center[a] - half[a] >= max[a] ||
				center[b] + half[b] <= min[b] || center[b] - half[b] >= max[b])
			continue;
		if (delta > 0.f && start + half[axis] <= min[axis] &&
				next + half[axis] >= min[axis]) {
			float contact = min[axis] - half[axis];
			if (!hit || contact < best) { best = contact; hit = true; }
		} else if (delta < 0.f && start - half[axis] >= max[axis] &&
				next - half[axis] <= max[axis]) {
			float contact = max[axis] + half[axis];
			if (!hit || contact > best) { best = contact; hit = true; }
		}
	}

	if (!hit && world->triangle_count) {
		float probe[3] = {center[0], center[1], center[2]};
		probe[axis] = next;
		if (world_has_compound_triangle_contact(world, probe, half,
				parts, part_count)) {
			best = start;
			hit = true;
		}
	}

	center[axis] = hit ? best : next;
	if (hit && velocity) *velocity = -*velocity * restitution;
	return hit;
}

void shady_physics_move_box(const struct shady_world *world, float center[3],
		const float target[3], const float half[3]) {
	float delta[3] = {
		target[0] - center[0],
		target[1] - center[1],
		target[2] - center[2],
	};
	float max_move = fmaxf(fabsf(delta[0]),
		fmaxf(fabsf(delta[1]), fabsf(delta[2])));
	float min_half = fminf(half[0], fminf(half[1], half[2]));
	float max_step = fmaxf(min_half * .5f, .002f);
	int steps = (int)ceilf(max_move / max_step);
	if (steps < 1) steps = 1;
	if (steps > 64) steps = 64;

	float step[3] = {
		delta[0] / steps,
		delta[1] / steps,
		delta[2] / steps,
	};
	float velocity = 0.f;

	for (int i = 0; i < steps; i++) {
		shady_physics_sweep_cube_axis(world, center, half, 1,
			step[1], &velocity, 0.f);
		shady_physics_sweep_cube_axis(world, center, half, 0,
			step[0], &velocity, 0.f);
		shady_physics_sweep_cube_axis(world, center, half, 2,
			step[2], &velocity, 0.f);
	}
}

void shady_physics_move_convex(const struct shady_world *world, float center[3],
		const float target[3], const float half[3],
		const float (*vertices)[3], size_t vertex_count,
		const uint16_t *indices, size_t index_count) {
	float delta[3] = {
		target[0] - center[0], target[1] - center[1], target[2] - center[2],
	};
	float max_move = fmaxf(fabsf(delta[0]),
		fmaxf(fabsf(delta[1]), fabsf(delta[2])));
	float min_half = fminf(half[0], fminf(half[1], half[2]));
	float max_step = fmaxf(min_half * .5f, .002f);
	int steps = (int)ceilf(max_move / max_step);
	if (steps < 1) steps = 1;
	if (steps > 64) steps = 64;
	float step[3] = {delta[0] / steps, delta[1] / steps, delta[2] / steps};
	float velocity = 0.f;
	for (int i = 0; i < steps; ++i) {
		shady_physics_sweep_convex_axis(world, center, half,
			vertices, vertex_count, indices, index_count,
			1, step[1], &velocity, 0.f);
		shady_physics_sweep_convex_axis(world, center, half,
			vertices, vertex_count, indices, index_count,
			0, step[0], &velocity, 0.f);
		shady_physics_sweep_convex_axis(world, center, half,
			vertices, vertex_count, indices, index_count,
			2, step[2], &velocity, 0.f);
	}
}

void shady_physics_move_compound(const struct shady_world *world, float center[3],
		const float target[3], const float half[3],
		const struct shady_physics_convex_part *parts, size_t part_count) {
	if (!world || !center || !target || !half || !parts || part_count == 0)
		return;
	float delta[3] = {
		target[0] - center[0], target[1] - center[1], target[2] - center[2],
	};
	float max_move = fmaxf(fabsf(delta[0]),
		fmaxf(fabsf(delta[1]), fabsf(delta[2])));
	float min_half = fminf(half[0], fminf(half[1], half[2]));
	float max_step = fmaxf(min_half * .5f, .002f);
	int steps = (int)ceilf(max_move / max_step);
	if (steps < 1) steps = 1;
	if (steps > 64) steps = 64;
	float step[3] = {delta[0] / steps, delta[1] / steps, delta[2] / steps};
	float velocity = 0.f;
	for (int i = 0; i < steps; ++i) {
		shady_physics_sweep_compound_axis(world, center, half, parts, part_count,
			1, step[1], &velocity, 0.f);
		shady_physics_sweep_compound_axis(world, center, half, parts, part_count,
			0, step[0], &velocity, 0.f);
		shady_physics_sweep_compound_axis(world, center, half, parts, part_count,
			2, step[2], &velocity, 0.f);
	}
}

void shady_physics_move_cube(const struct shady_world *world, float center[3],
		const float target[3], float half_size) {
	const float half[3] = {half_size, half_size, half_size};
	shady_physics_move_box(world, center, target, half);
}
