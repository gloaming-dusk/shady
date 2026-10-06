#include "internal.h"

#include "../modules/environment/environment.h"
#include "../shady.h"

/* Thin adapter: resolve the calling plugin, then defer to the core
 * environment service, which owns validation and storage. */

#define CALLER_OWNER() shady_plugin_owner_from_address(__builtin_return_address(0))

static bool spatial_available(shady_host host) {
	struct shady_server *server = (struct shady_server *)host;
	return server && server->environment &&
		shady_module_has_capability(server, "spatial");
}

static shady_environment_loader_id register_loader(shady_host host,
		const struct shady_environment_loader *loader) {
	if (!spatial_available(host)) return 0;
	return shady_environment_register_loader((struct shady_server *)host,
		CALLER_OWNER(), loader);
}

static bool unregister_loader(shady_host host, shady_environment_loader_id id) {
	if (!host) return false;
	return shady_environment_unregister_loader((struct shady_server *)host,
		CALLER_OWNER(), id);
}

static bool set_scene(shady_host host, const struct shady_environment_scene *scene) {
	if (!spatial_available(host)) return false;
	return shady_environment_set_scene((struct shady_server *)host,
		CALLER_OWNER(), scene);
}

static bool clear_scene(shady_host host) {
	if (!host) return false;
	return shady_environment_clear_scene((struct shady_server *)host, CALLER_OWNER());
}

void shady_plugin_environment_cleanup_owner(struct shady_server *server, void *owner) {
	shady_environment_cleanup_owner(server, owner);
}

const struct shady_environment_api_v1 shady_environment_api = {
	.struct_size = sizeof(shady_environment_api),
	.register_loader = register_loader,
	.unregister_loader = unregister_loader,
	.set_scene = set_scene,
	.clear_scene = clear_scene,
};
