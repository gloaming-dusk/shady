#ifndef SHADY_PLUGIN_MANAGER_H
#define SHADY_PLUGIN_MANAGER_H

/*
 * Plugin manager: the user-facing layer above src/plugin/plugin.c.
 *
 * It turns plugin names into shared-object paths (search paths + naming
 * convention), remembers every plugin it was asked for, and owns the list of
 * plugins shipped with Shady. Default plugins are loaded after config.lua,
 * so a disabled default is never dlopen'ed. Loading, ABI checks, reload and
 * rollback stay in plugin.c. See docs/PLUGIN_MANAGER.md.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct shady_server;

#define SHADY_PLUGIN_SEARCH_PATH_MAX 8
#define SHADY_PLUGIN_RECORD_MAX 32

enum shady_plugin_status {
	SHADY_PLUGIN_PENDING,   /* default plugin, loads after config.lua */
	SHADY_PLUGIN_DISABLED,  /* default plugin turned off; never loaded */
	SHADY_PLUGIN_FAILED,    /* could not be found or loaded */
	SHADY_PLUGIN_ACTIVE,    /* loaded and its module is running */
	SHADY_PLUGIN_INACTIVE,  /* loaded, module disabled or not initialized */
	SHADY_PLUGIN_UNLOADED,  /* loaded earlier, then hot-unloaded */
};

struct shady_plugin_record {
	char *spec;        /* name or path as requested, e.g. "obj-loader" */
	char *path;        /* resolved shared object, NULL until found */
	char *module_name; /* module name reported by the plugin once loaded */
	bool builtin;      /* shipped with Shady and loaded by default */
	bool loaded;       /* went through shady_plugin_load() successfully */
	bool failed;
	int8_t enable;     /* 0 = default, 1 = forced on, -1 = forced off */
};

struct shady_plugin_manager {
	char *search_paths[SHADY_PLUGIN_SEARCH_PATH_MAX];
	size_t search_path_count;
	struct shady_plugin_record records[SHADY_PLUGIN_RECORD_MAX];
	size_t record_count;
	bool defaults_loaded;
};

/* Register a plugin shipped with Shady. Loaded by load_defaults() unless
 * disabled first. */
bool shady_plugin_manager_add_default(struct shady_server *server, const char *name);

/* Add a user search path. User paths are searched before the built-in
 * locations, in the order they were added. A leading "~/" is expanded. */
bool shady_plugin_manager_add_search_path(struct shady_server *server, const char *path);

/* Load a plugin by name ("obj-loader" -> libshady-plugin-obj-loader.so on the
 * search path), by file name ("foo.so"), or by path (anything with a '/').
 * Loading something already loaded is a no-op that succeeds. */
bool shady_plugin_manager_load(struct shady_server *server, const char *spec);

/* Enable or disable a plugin by name. For a pending default this decides
 * whether it is loaded at all; for a loaded plugin it sets the module
 * override. Returns false for unknown names. */
bool shady_plugin_manager_set_enabled(struct shady_server *server,
	const char *name, bool enabled);
bool shady_plugin_manager_knows(struct shady_server *server, const char *name);

/* Safe mode: keep every default plugin out of the process. */
void shady_plugin_manager_disable_defaults(struct shady_server *server);

/* Load pending defaults. Called once, after config.lua. */
bool shady_plugin_manager_load_defaults(struct shady_server *server);

/* Map a plugin name or spec to its module name for reload/unload. Returns
 * name itself when the manager has no better answer. */
const char *shady_plugin_manager_module_name(struct shady_server *server,
	const char *name);

enum shady_plugin_status shady_plugin_manager_status(struct shady_server *server,
	const struct shady_plugin_record *record);
const char *shady_plugin_status_name(enum shady_plugin_status status);

void shady_plugin_manager_finish(struct shady_server *server);

#endif
