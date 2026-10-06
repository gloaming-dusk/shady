#include "internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "../../plugin/manager.h"
#include "../../render/render.h"
#include "../../shady.h"

bool shady_environment_create(struct shady_server *server) {
	if (server->environment) return true;
	struct shady_environment *env = calloc(1, sizeof(*env));
	if (!env) return false;
	env->dirty = true;
	server->environment = env;
	return true;
}

void shady_environment_destroy(struct shady_server *server) {
	struct shady_environment *env = server->environment;
	if (!env) return;
	shady_environment_scene_free(&env->scene);
	free(env);
	server->environment = NULL;
}

static void replace_scene(struct shady_server *server,
		struct shady_environment_scene_data *scene,
		enum shady_environment_source source, void *owner,
		shady_environment_loader_id loader) {
	struct shady_environment *env = server->environment;
	shady_environment_scene_free(&env->scene);
	env->scene = *scene;
	env->source = source;
	env->scene_owner = owner;
	env->scene_loader = loader;
	env->revision++;
	shady_environment_apply_collision(server, &env->scene);
	if (server->renderer) shady_render_schedule_all_outputs(server);
}

void shady_environment_drop_scene(struct shady_server *server) {
	struct shady_environment *env = server->environment;
	if (!env || env->source == SHADY_ENVIRONMENT_SOURCE_NONE) return;
	struct shady_environment_scene_data empty = {0};
	replace_scene(server, &empty, SHADY_ENVIRONMENT_SOURCE_NONE, NULL, 0);
}

static const char *configured_path(const struct shady_server *server) {
	if (!server->config.environment || !server->config.environment_path[0])
		return "";
	return server->config.environment_path;
}

static void load_configured(struct shady_server *server, const char *path) {
	struct shady_environment *env = server->environment;
	const struct shady_environment_loader_slot *slot =
		shady_environment_find_loader(env, path);
	if (!slot) {
		/* Not an error yet: the loader plugin may register later. */
		wlr_log(WLR_INFO, "environment: no loader registered for %s", path);
		shady_environment_drop_scene(server);
		return;
	}

	const struct shady_environment_loader *loader = slot->loader;
	struct shady_environment_scene scene = { .struct_size = sizeof(scene) };
	if (!loader->load((shady_host)server, path, &scene, loader->user_data)) {
		wlr_log(WLR_ERROR, "environment: loader '%s' failed to load %s",
			loader->name, path);
		shady_environment_drop_scene(server);
		return;
	}

	struct shady_environment_scene_data copy;
	bool ok = shady_environment_scene_copy(&copy, &scene,
		shady_environment_box_capacity(), shady_environment_triangle_capacity(), path);
	if (loader->release) loader->release((shady_host)server, &scene, loader->user_data);
	if (!ok) {
		shady_environment_drop_scene(server);
		return;
	}

	replace_scene(server, &copy, SHADY_ENVIRONMENT_SOURCE_CONFIG,
		slot->owner, slot->id);
	wlr_log(WLR_INFO,
		"environment: '%s' loaded %s (%zu triangles, %zu collision boxes, %zu collision triangles)",
		loader->name, path, copy.vertex_count / 3, copy.box_count, copy.triangle_count);
}

void shady_environment_sync(struct shady_server *server) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env) return;
	const char *path = configured_path(server);
	if (!env->dirty && !strcmp(env->synced_path, path)) return;
	env->dirty = false;
	snprintf(env->synced_path, sizeof(env->synced_path), "%s", path);

	/* A procedural scene stays until its owner clears it. */
	if (env->source == SHADY_ENVIRONMENT_SOURCE_PROCEDURAL) return;
	if (!path[0]) {
		shady_environment_drop_scene(server);
		return;
	}
	load_configured(server, path);
}

bool shady_environment_set_scene(struct shady_server *server, void *owner,
		const struct shady_environment_scene *scene) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env || !owner) return false;
	if (env->source == SHADY_ENVIRONMENT_SOURCE_PROCEDURAL && env->scene_owner != owner)
		return false;

	struct shady_environment_scene_data copy;
	if (!shady_environment_scene_copy(&copy, scene, shady_environment_box_capacity(),
			shady_environment_triangle_capacity(), "procedural scene"))
		return false;
	replace_scene(server, &copy, SHADY_ENVIRONMENT_SOURCE_PROCEDURAL, owner, 0);
	return true;
}

bool shady_environment_clear_scene(struct shady_server *server, void *owner) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env || !owner || env->source != SHADY_ENVIRONMENT_SOURCE_PROCEDURAL ||
			env->scene_owner != owner)
		return false;
	shady_environment_drop_scene(server);
	/* Fall back to the configured environment, if any. */
	env->dirty = true;
	shady_environment_sync(server);
	return true;
}

void shady_environment_cleanup_owner(struct shady_server *server, void *owner) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env || !owner) return;
	bool changed = false;
	for (size_t i = 0; i < SHADY_ENVIRONMENT_MAX_LOADERS; ++i) {
		struct shady_environment_loader_slot *slot = &env->loaders[i];
		if (slot->id && slot->owner == owner) {
			*slot = (struct shady_environment_loader_slot){0};
			changed = true;
		}
	}
	if (env->scene_owner == owner) {
		shady_environment_drop_scene(server);
		changed = true;
	}
	/* Defer the retry to the next frame: the owner's code is about to be
	 * unmapped, and another loader may want the configured path. */
	if (changed) env->dirty = true;
}

void shady_environment_request_loader(struct shady_server *server) {
	const char *path = configured_path(server);
	const char *slash = strrchr(path, '/');
	const char *dot = strrchr(slash ? slash + 1 : path, '.');
	if (!dot || !dot[1]) return;
	static const char suffix[] = "-loader";
	char name[48];
	size_t length = strlen(dot + 1);
	if (length + sizeof(suffix) > sizeof(name)) return;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)dot[1 + i];
		if (!isalnum(c)) return;
		name[i] = (char)tolower(c);
	}
	memcpy(name + length, suffix, sizeof(suffix));
	if (!shady_plugin_manager_load(server, name))
		wlr_log(WLR_ERROR, "environment: no '%s' plugin to load %s", name, path);
}
