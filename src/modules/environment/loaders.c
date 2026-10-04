#include "internal.h"

#include <string.h>
#include <strings.h>

#include <wlr/util/log.h>

#include "../../plugin/internal.h"
#include "../../shady.h"

#define SHADY_ENVIRONMENT_EXTENSION_MAX 16

static bool owned_by(void *owner, const void *address) {
	return address && shady_plugin_owner_from_address((void *)address) == owner;
}

static const char *path_extension(const char *path) {
	const char *slash = strrchr(path, '/');
	const char *dot = strrchr(path, '.');
	if (!dot || (slash && dot < slash) || !dot[1]) return NULL;
	return dot + 1;
}

static bool loader_claims(const struct shady_environment_loader *loader,
		const char *extension) {
	for (const char *const *ext = loader->extensions; *ext; ++ext) {
		if (!strcasecmp(*ext, extension)) return true;
	}
	return false;
}

static bool extensions_valid(const char *const *extensions) {
	if (!extensions || !extensions[0]) return false;
	for (const char *const *ext = extensions; *ext; ++ext) {
		size_t len = strlen(*ext);
		if (!len || len >= SHADY_ENVIRONMENT_EXTENSION_MAX || strchr(*ext, '.') ||
				strchr(*ext, '/'))
			return false;
	}
	return true;
}

static const struct shady_environment_loader_slot *find_by_extension(
		const struct shady_environment *env, const char *extension) {
	for (size_t i = 0; i < SHADY_ENVIRONMENT_MAX_LOADERS; ++i) {
		const struct shady_environment_loader_slot *slot = &env->loaders[i];
		if (slot->id && loader_claims(slot->loader, extension)) return slot;
	}
	return NULL;
}

const struct shady_environment_loader_slot *shady_environment_find_loader(
		const struct shady_environment *env, const char *path) {
	const char *extension = path ? path_extension(path) : NULL;
	return extension ? find_by_extension(env, extension) : NULL;
}

shady_environment_loader_id shady_environment_register_loader(
		struct shady_server *server, void *owner,
		const struct shady_environment_loader *loader) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env || !owner || !loader || loader->struct_size < sizeof(*loader) ||
			!loader->name || !loader->load || !extensions_valid(loader->extensions))
		return 0;
	/* The descriptor and callbacks are referenced after this call returns, so
	 * they must live in the registering plugin and die with it. */
	if (!owned_by(owner, loader) || !owned_by(owner, (const void *)loader->load) ||
			(loader->release && !owned_by(owner, (const void *)loader->release)))
		return 0;

	struct shady_environment_loader_slot *free_slot = NULL;
	for (size_t i = 0; i < SHADY_ENVIRONMENT_MAX_LOADERS; ++i) {
		struct shady_environment_loader_slot *slot = &env->loaders[i];
		if (!slot->id) {
			if (!free_slot) free_slot = slot;
			continue;
		}
		for (const char *const *ext = loader->extensions; *ext; ++ext) {
			if (loader_claims(slot->loader, *ext)) {
				wlr_log(WLR_ERROR,
					"environment: loader '%s' cannot claim .%s, already handled by '%s'",
					loader->name, *ext, slot->loader->name);
				return 0;
			}
		}
	}
	if (!free_slot) {
		wlr_log(WLR_ERROR, "environment: loader table full, rejecting '%s'", loader->name);
		return 0;
	}

	if (!++env->next_loader_id) ++env->next_loader_id;
	*free_slot = (struct shady_environment_loader_slot){
		.id = env->next_loader_id,
		.owner = owner,
		.loader = loader,
	};
	wlr_log(WLR_INFO, "environment: registered loader '%s'", loader->name);

	/* A path that was waiting for a loader may now be loadable. */
	env->dirty = true;
	shady_environment_sync(server);
	return free_slot->id;
}

bool shady_environment_unregister_loader(struct shady_server *server,
		void *owner, shady_environment_loader_id id) {
	struct shady_environment *env = server ? server->environment : NULL;
	if (!env || !owner || !id) return false;
	for (size_t i = 0; i < SHADY_ENVIRONMENT_MAX_LOADERS; ++i) {
		struct shady_environment_loader_slot *slot = &env->loaders[i];
		if (slot->id != id) continue;
		if (slot->owner != owner) return false;
		wlr_log(WLR_INFO, "environment: unregistered loader '%s'", slot->loader->name);
		*slot = (struct shady_environment_loader_slot){0};
		if (env->scene_loader == id) {
			/* The scene stays copied, but it would no longer be reproducible
			 * from config; drop it and let another loader pick the path up. */
			shady_environment_drop_scene(server);
		}
		env->dirty = true;
		return true;
	}
	return false;
}
