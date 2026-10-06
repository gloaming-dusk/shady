#ifndef SHADY_MODULE_ENVIRONMENT_H
#define SHADY_MODULE_ENVIRONMENT_H

/*
 * Core environment service.
 *
 * Owns the active static scene (visual mesh + collision), the registry of
 * plugin-provided asset loaders, and the sky. It never parses model formats:
 * the configured `environment_path` is dispatched to the loader registered
 * for its extension. See docs/ENVIRONMENT_API.md.
 */

#include <stdbool.h>
#include <shady/environment.h>

struct shady_server;

/* Lifetime follows the spatial module. Safe to call when already created or
 * destroyed. */
bool shady_environment_create(struct shady_server *server);
void shady_environment_destroy(struct shady_server *server);

/* Reconcile the active scene with `environment`/`environment_path`. Cheap when
 * nothing changed; called once per spatial frame and after registry changes. */
void shady_environment_sync(struct shady_server *server);

/* Loader registry. owner is the plugin's load base. */
shady_environment_loader_id shady_environment_register_loader(
	struct shady_server *server, void *owner,
	const struct shady_environment_loader *loader);
bool shady_environment_unregister_loader(struct shady_server *server,
	void *owner, shady_environment_loader_id id);

/* Procedural scenes submitted directly by a plugin. */
bool shady_environment_set_scene(struct shady_server *server, void *owner,
	const struct shady_environment_scene *scene);
bool shady_environment_clear_scene(struct shady_server *server, void *owner);

/* Drop every loader and scene owned by an unloading plugin. */
void shady_environment_cleanup_owner(struct shady_server *server, void *owner);

/* GL resources. Requires the renderer's EGL context to be current. */
bool shady_environment_gl_init(void);
void shady_environment_gl_fini(void);
void shady_environment_draw_sky(struct shady_server *server);
void shady_environment_draw_scene(struct shady_server *server, const float vp[16]);

#endif
