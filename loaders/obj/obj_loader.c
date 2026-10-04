/*
 * Wavefront OBJ environment loader.
 *
 * Registers itself for ".obj" with the host's shady.environment API. The host
 * decides when to load (config `environment_path`), copies the scene, and owns
 * rendering and collision; this plugin only parses.
 *
 * Supported: v, vt, vn, f with v | v/vt | v//vn | v/vt/vn references,
 * positive and negative indices, polygons (fan-triangulated). Faces get a flat
 * normal when the file has none. Groups/objects named collision_* are also
 * exported as collision: each group contributes one bounding box and its
 * triangles. Collision faces stay visible, matching authored test rooms.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <shady/environment.h>
#include <shady/plugin.h>

#define COLLISION_PREFIX "collision_"

struct vec2 { float x, y; };
struct vec3 { float x, y, z; };
struct obj_ref { int v, vt, vn; };

/* Growable array. All parser storage uses this so cleanup is uniform. */
struct array {
	void *data;
	size_t count;
	size_t capacity;
};

static bool array_push(struct array *a, const void *item, size_t size) {
	if (a->count == a->capacity) {
		size_t next = a->capacity ? a->capacity * 2 : 256;
		if (next < a->capacity || next > SIZE_MAX / size) return false;
		void *p = realloc(a->data, next * size);
		if (!p) return false;
		a->data = p;
		a->capacity = next;
	}
	memcpy((char *)a->data + a->count * size, item, size);
	a->count++;
	return true;
}

#define ARRAY_AT(a, type, i) (((type *)(a).data)[i])

struct parser {
	unsigned line;

	struct array positions; /* struct vec3 */
	struct array uvs;       /* struct vec2 */
	struct array normals;   /* struct vec3 */

	struct array vertices;  /* struct shady_environment_vertex */
	struct array boxes;     /* struct shady_environment_box */
	struct array triangles; /* struct shady_environment_triangle */

	bool in_collision_group;
	bool group_has_bounds;
	struct shady_environment_box group_box;
};

static const struct shady_plugin_api_v1 *api;
static const struct shady_environment_api_v1 *environment;
static shady_host host;
static shady_environment_loader_id loader_id;


static void log_message(enum shady_plugin_log_level level, const char *fmt, ...) {
	char message[768];
	va_list args;
	va_start(args, fmt);
	vsnprintf(message, sizeof(message), fmt, args);
	va_end(args);
	api->log(level, message);
}

static int resolve_index(int index, size_t count) {
	if (index > 0) return (size_t)index <= count ? index - 1 : -1;
	if (index < 0) {
		long n = (long)count + index;
		return n >= 0 && n < (long)count ? (int)n : -1;
	}
	return -1;
}

static bool parse_int(const char *s, char **end, int *out) {
	errno = 0;
	long v = strtol(s, end, 10);
	if (errno || *end == s || v < INT_MIN || v > INT_MAX) return false;
	*out = (int)v;
	return true;
}

/* v | v/vt | v//vn | v/vt/vn */
static bool parse_ref(const char *s, struct obj_ref *out) {
	char *end;
	*out = (struct obj_ref){0};
	if (!parse_int(s, &end, &out->v)) return false;
	if (*end != '/') return *end == '\0';
	const char *p = end + 1;
	if (*p != '/') {
		if (!parse_int(p, &end, &out->vt)) return false;
		p = end;
	}
	if (*p != '/') return *p == '\0';
	p++;
	if (!*p) return true;
	return parse_int(p, &end, &out->vn) && *end == '\0';
}

static struct vec3 face_normal(struct vec3 a, struct vec3 b, struct vec3 c) {
	float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
	float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
	struct vec3 n = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
	float length = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
	if (length > 1e-8f) {
		n.x /= length;
		n.y /= length;
		n.z /= length;
	}
	return n;
}

static void grow_group_box(struct parser *p, struct vec3 v) {
	struct shady_environment_box *b = &p->group_box;
	if (!p->group_has_bounds) {
		*b = (struct shady_environment_box){ { v.x, v.y, v.z }, { v.x, v.y, v.z } };
		p->group_has_bounds = true;
		return;
	}
	const float c[3] = { v.x, v.y, v.z };
	for (int i = 0; i < 3; ++i) {
		if (c[i] < b->min[i]) b->min[i] = c[i];
		if (c[i] > b->max[i]) b->max[i] = c[i];
	}
}

static bool end_group(struct parser *p) {
	bool ok = true;
	if (p->in_collision_group && p->group_has_bounds)
		ok = array_push(&p->boxes, &p->group_box, sizeof(p->group_box));
	p->group_has_bounds = false;
	return ok;
}

static bool emit_triangle(struct parser *p, const struct obj_ref refs[3]) {
	int pi[3], ti[3], ni[3];
	for (int i = 0; i < 3; ++i) {
		pi[i] = resolve_index(refs[i].v, p->positions.count);
		ti[i] = refs[i].vt ? resolve_index(refs[i].vt, p->uvs.count) : -1;
		ni[i] = refs[i].vn ? resolve_index(refs[i].vn, p->normals.count) : -1;
		if (pi[i] < 0 || (refs[i].vt && ti[i] < 0) || (refs[i].vn && ni[i] < 0))
			return false;
	}

	struct vec3 pos[3];
	for (int i = 0; i < 3; ++i) pos[i] = ARRAY_AT(p->positions, struct vec3, pi[i]);
	struct vec3 flat = face_normal(pos[0], pos[1], pos[2]);

	for (int i = 0; i < 3; ++i) {
		struct vec3 n = ni[i] >= 0 ? ARRAY_AT(p->normals, struct vec3, ni[i]) : flat;
		struct vec2 t = ti[i] >= 0 ? ARRAY_AT(p->uvs, struct vec2, ti[i]) : (struct vec2){0};
		struct shady_environment_vertex vertex = {
			.position = { pos[i].x, pos[i].y, pos[i].z },
			.normal = { n.x, n.y, n.z },
			.uv = { t.x, t.y },
		};
		if (!array_push(&p->vertices, &vertex, sizeof(vertex))) return false;
	}

	if (p->in_collision_group) {
		struct shady_environment_triangle tri;
		for (int i = 0; i < 3; ++i) {
			tri.v[i][0] = pos[i].x;
			tri.v[i][1] = pos[i].y;
			tri.v[i][2] = pos[i].z;
			grow_group_box(p, pos[i]);
		}
		if (!array_push(&p->triangles, &tri, sizeof(tri))) return false;
	}
	return true;
}

static bool parse_face(struct parser *p, char *args) {
	struct obj_ref refs[3];
	size_t n = 0;
	char *save = NULL;
	/* Fan-triangulate while streaming: refs[0] is the pivot, refs[1] the
	 * previous vertex, refs[2] the current one. */
	for (char *tok = strtok_r(args, " \t\r\n", &save); tok;
			tok = strtok_r(NULL, " \t\r\n", &save)) {
		struct obj_ref ref;
		if (!parse_ref(tok, &ref)) return false;
		if (n < 3) {
			refs[n++] = ref;
			if (n == 3 && !emit_triangle(p, refs)) return false;
		} else {
			refs[1] = refs[2];
			refs[2] = ref;
			if (!emit_triangle(p, refs)) return false;
			n++;
		}
	}
	return n >= 3;
}

static bool parse_line(struct parser *p, char *line) {
	char *comment = strchr(line, '#');
	if (comment) *comment = '\0';
	while (isspace((unsigned char)*line)) line++;
	if (!*line) return true;

	if (line[0] == 'v' && isspace((unsigned char)line[1])) {
		struct vec3 v;
		return sscanf(line + 1, "%f %f %f", &v.x, &v.y, &v.z) == 3 &&
			array_push(&p->positions, &v, sizeof(v));
	}
	if (!strncmp(line, "vt", 2) && isspace((unsigned char)line[2])) {
		struct vec2 t;
		return sscanf(line + 2, "%f %f", &t.x, &t.y) == 2 &&
			array_push(&p->uvs, &t, sizeof(t));
	}
	if (!strncmp(line, "vn", 2) && isspace((unsigned char)line[2])) {
		struct vec3 n;
		return sscanf(line + 2, "%f %f %f", &n.x, &n.y, &n.z) == 3 &&
			array_push(&p->normals, &n, sizeof(n));
	}
	if ((line[0] == 'g' || line[0] == 'o') && isspace((unsigned char)line[1])) {
		if (!end_group(p)) return false;
		char name[256] = {0};
		p->in_collision_group = sscanf(line + 1, "%255s", name) == 1 &&
			!strncmp(name, COLLISION_PREFIX, strlen(COLLISION_PREFIX));
		return true;
	}
	if (line[0] == 'f' && isspace((unsigned char)line[1]))
		return parse_face(p, line + 1);
	/* mtllib, usemtl, s, l, p, ...: not needed for the environment. */
	return true;
}

static void parser_free_inputs(struct parser *p) {
	free(p->positions.data);
	free(p->uvs.data);
	free(p->normals.data);
}

static void parser_free_outputs(struct parser *p) {
	free(p->vertices.data);
	free(p->boxes.data);
	free(p->triangles.data);
}

static bool obj_load(shady_host h, const char *path,
		struct shady_environment_scene *scene, void *user_data) {
	(void)h;
	(void)user_data;
	FILE *file = fopen(path, "r");
	if (!file) {
		log_message(SHADY_PLUGIN_LOG_ERROR, "obj-loader: cannot open %s: %s",
			path, strerror(errno));
		return false;
	}

	struct parser p = {0};
	char *line = NULL;
	size_t line_capacity = 0;
	bool ok = true;
	while (getline(&line, &line_capacity, file) >= 0) {
		p.line++;
		if (!parse_line(&p, line)) {
			ok = false;
			break;
		}
	}
	if (ok) ok = end_group(&p);
	if (!ok) log_message(SHADY_PLUGIN_LOG_ERROR, "obj-loader: %s:%u: invalid or unsupported line",
		path, p.line);
	free(line);
	fclose(file);
	parser_free_inputs(&p);

	if (ok && !p.vertices.count) {
		log_message(SHADY_PLUGIN_LOG_ERROR, "obj-loader: %s contains no faces", path);
		ok = false;
	}
	if (!ok) {
		parser_free_outputs(&p);
		return false;
	}

	scene->vertices = p.vertices.data;
	scene->vertex_count = p.vertices.count;
	scene->indices = NULL;
	scene->index_count = 0;
	scene->boxes = p.boxes.data;
	scene->box_count = p.boxes.count;
	scene->triangles = p.triangles.data;
	scene->triangle_count = p.triangles.count;
	return true;
}

static void obj_release(shady_host h, struct shady_environment_scene *scene,
		void *user_data) {
	(void)h;
	(void)user_data;
	free((void *)scene->vertices);
	free((void *)scene->boxes);
	free((void *)scene->triangles);
	*scene = (struct shady_environment_scene){ .struct_size = sizeof(*scene) };
}

static const char *const obj_extensions[] = { "obj", NULL };

static const struct shady_environment_loader obj_loader = {
	.struct_size = sizeof(obj_loader),
	.name = "obj",
	.extensions = obj_extensions,
	.load = obj_load,
	.release = obj_release,
};

static bool obj_loader_init(struct shady_server *server) {
	(void)server;
	loader_id = environment->register_loader(host, &obj_loader);
	if (!loader_id) {
		api->log(SHADY_PLUGIN_LOG_ERROR, "obj-loader: host rejected the .obj loader");
		return false;
	}
	return true;
}

static void obj_loader_destroy(struct shady_server *server) {
	(void)server;
	if (loader_id) environment->unregister_loader(host, loader_id);
	loader_id = 0;
}

static const char *const provides[] = { "environment-loader.obj", NULL };
static const char *const requires[] = { "spatial", NULL };

static const struct shady_module obj_loader_module = {
	.name = "obj-loader",
	.provides = provides,
	.requires = requires,
	.init = obj_loader_init,
	.destroy = obj_loader_destroy,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api, shady_host host_context) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 || !host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			!SHADY_API_HAS(host_api, query_api))
		return NULL;

	const struct shady_environment_api_v1 *env = host_api->query_api(host_context,
		SHADY_ENVIRONMENT_API, SHADY_ENVIRONMENT_API_VERSION);
	if (!env || env->struct_size < sizeof(*env) ||
			!env->register_loader || !env->unregister_loader)
		return NULL;

	api = host_api;
	environment = env;
	host = host_context;
	return &obj_loader_module;
}
