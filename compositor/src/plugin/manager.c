#include "manager.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "plugin.h"
#include "../module/module.h"
#include "../shady.h"

#define PLUGIN_PATH_MAX 4096
#define PLUGIN_NAME_MAX 128

static struct shady_plugin_manager *manager_of(struct shady_server *server) {
	return &server->plugins;
}

static char *dup_string(const char *s) {
	return s ? strdup(s) : NULL;
}

/* "~/x" -> "$HOME/x". Other paths are copied unchanged. */
static bool expand_home(const char *path, char *out, size_t size) {
	const char *home = getenv("HOME");
	int n;
	if (path[0] == '~' && path[1] == '/' && home && *home)
		n = snprintf(out, size, "%s%s", home, path + 1);
	else
		n = snprintf(out, size, "%s", path);
	return n > 0 && (size_t)n < size;
}

static bool is_path_spec(const char *spec) {
	return strchr(spec, '/') != NULL;
}

/* Plugin names become file names, so keep them to a boring character set. */
static bool name_valid(const char *name) {
	size_t len = strlen(name);
	if (!len || len >= PLUGIN_NAME_MAX || name[0] == '.') return false;
	for (const char *p = name; *p; ++p) {
		if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.')
			return false;
	}
	return true;
}

static bool ends_with(const char *s, const char *suffix) {
	size_t a = strlen(s), b = strlen(suffix);
	return a >= b && !strcmp(s + a - b, suffix);
}

static struct shady_plugin_record *find_record(struct shady_server *server,
		const char *name) {
	struct shady_plugin_manager *pm = manager_of(server);
	for (size_t i = 0; i < pm->record_count; ++i) {
		struct shady_plugin_record *r = &pm->records[i];
		if (!strcmp(r->spec, name) ||
				(r->module_name && !strcmp(r->module_name, name)) ||
				(r->path && !strcmp(r->path, name)))
			return r;
	}
	return NULL;
}

static struct shady_plugin_record *add_record(struct shady_server *server,
		const char *spec, bool builtin) {
	struct shady_plugin_manager *pm = manager_of(server);
	if (pm->record_count >= SHADY_PLUGIN_RECORD_MAX) {
		wlr_log(WLR_ERROR, "plugin manager: too many plugins, ignoring %s", spec);
		return NULL;
	}
	char *copy = dup_string(spec);
	if (!copy) return NULL;
	struct shady_plugin_record *r = &pm->records[pm->record_count++];
	*r = (struct shady_plugin_record){ .spec = copy, .builtin = builtin };
	return r;
}

/* Search order: user paths, $SHADY_PLUGIN_PATH, the executable's directory
 * (build tree), $XDG_DATA_HOME/shady/plugins, then the install directory. */
typedef bool (*search_visit)(const char *dir, void *data);

static bool visit_colon_list(const char *list, search_visit visit, void *data) {
	if (!list) return false;
	char dir[PLUGIN_PATH_MAX];
	while (*list) {
		const char *end = strchr(list, ':');
		size_t len = end ? (size_t)(end - list) : strlen(list);
		if (len && len < sizeof(dir)) {
			memcpy(dir, list, len);
			dir[len] = '\0';
			if (visit(dir, data)) return true;
		}
		if (!end) break;
		list = end + 1;
	}
	return false;
}

static bool visit_search_dirs(struct shady_server *server, search_visit visit, void *data) {
	struct shady_plugin_manager *pm = manager_of(server);
	for (size_t i = 0; i < pm->search_path_count; ++i) {
		if (visit(pm->search_paths[i], data)) return true;
	}
	if (visit_colon_list(getenv("SHADY_PLUGIN_PATH"), visit, data)) return true;

	char dir[PLUGIN_PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", dir, sizeof(dir) - 1);
	if (n > 0) {
		dir[n] = '\0';
		char *slash = strrchr(dir, '/');
		if (slash) {
			*slash = '\0';
			if (visit(dir, data)) return true;
		}
	}

	const char *xdg = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	int len = -1;
	if (xdg && *xdg) len = snprintf(dir, sizeof(dir), "%s/shady/plugins", xdg);
	else if (home && *home) len = snprintf(dir, sizeof(dir), "%s/.local/share/shady/plugins", home);
	if (len > 0 && (size_t)len < sizeof(dir) && visit(dir, data)) return true;

	return visit(SHADY_PLUGIN_DIR, data);
}

struct find_file {
	const char *file;
	char *out;
	size_t size;
};

static bool try_dir(const char *dir, void *data) {
	struct find_file *find = data;
	int n = snprintf(find->out, find->size, "%s/%s", dir, find->file);
	return n > 0 && (size_t)n < find->size && access(find->out, R_OK) == 0;
}

static bool log_dir(const char *dir, void *data) {
	(void)data;
	wlr_log(WLR_ERROR, "plugin manager:   searched %s", dir);
	return false;
}

static bool resolve(struct shady_server *server, const char *spec, char *out, size_t size) {
	if (is_path_spec(spec)) {
		if (!expand_home(spec, out, size)) return false;
		if (access(out, R_OK) == 0) return true;
		wlr_log(WLR_ERROR, "plugin manager: %s not found", out);
		return false;
	}
	if (!name_valid(spec)) {
		wlr_log(WLR_ERROR, "plugin manager: invalid plugin name '%s'", spec);
		return false;
	}
	char file[PLUGIN_NAME_MAX + 32];
	if (ends_with(spec, ".so")) snprintf(file, sizeof(file), "%s", spec);
	else snprintf(file, sizeof(file), "libshady-plugin-%s.so", spec);

	struct find_file find = { .file = file, .out = out, .size = size };
	if (visit_search_dirs(server, try_dir, &find)) return true;
	wlr_log(WLR_ERROR, "plugin manager: %s not found (%s)", spec, file);
	visit_search_dirs(server, log_dir, NULL);
	return false;
}

static bool load_record(struct shady_server *server, struct shady_plugin_record *r) {
	char path[PLUGIN_PATH_MAX];
	r->failed = true;
	if (!resolve(server, r->spec, path, sizeof(path))) return false;
	free(r->path);
	r->path = dup_string(path);
	if (!r->path) return false;

	if (!shady_plugin_load(server, path)) return false;
	struct shady_module_manager *modules = &server->modules;
	free(r->module_name);
	r->module_name = dup_string(modules->plugin_name[modules->count - 1]);
	if (!r->module_name) return false;
	r->loaded = true;
	r->failed = false;
	/* A decision made before the plugin existed applies now. */
	if (r->enable) shady_modules_set_enabled(modules, r->module_name, r->enable > 0);
	return true;
}

bool shady_plugin_manager_add_default(struct shady_server *server, const char *name) {
	if (find_record(server, name)) return true;
	return add_record(server, name, true) != NULL;
}

bool shady_plugin_manager_add_search_path(struct shady_server *server, const char *path) {
	struct shady_plugin_manager *pm = manager_of(server);
	char expanded[PLUGIN_PATH_MAX];
	if (!path || !*path || !expand_home(path, expanded, sizeof(expanded))) return false;
	for (size_t i = 0; i < pm->search_path_count; ++i) {
		if (!strcmp(pm->search_paths[i], expanded)) return true;
	}
	if (pm->search_path_count >= SHADY_PLUGIN_SEARCH_PATH_MAX) {
		wlr_log(WLR_ERROR, "plugin manager: too many search paths, ignoring %s", path);
		return false;
	}
	char *copy = dup_string(expanded);
	if (!copy) return false;
	pm->search_paths[pm->search_path_count++] = copy;
	return true;
}

bool shady_plugin_manager_load(struct shady_server *server, const char *spec) {
	if (!spec || !*spec) return false;
	struct shady_plugin_record *r = find_record(server, spec);
	if (r && r->loaded) return true;
	if (!r) r = add_record(server, spec, false);
	if (!r) return false;
	/* Loading explicitly overrides an earlier disable. */
	if (r->enable < 0) r->enable = 0;
	return load_record(server, r);
}

bool shady_plugin_manager_set_enabled(struct shady_server *server,
		const char *name, bool enabled) {
	struct shady_plugin_record *r = find_record(server, name);
	if (!r) return false;
	r->enable = enabled ? 1 : -1;
	if (r->loaded) return shady_modules_set_enabled(&server->modules, r->module_name, enabled);
	return true;
}

bool shady_plugin_manager_knows(struct shady_server *server, const char *name) {
	return find_record(server, name) != NULL;
}

void shady_plugin_manager_disable_defaults(struct shady_server *server) {
	struct shady_plugin_manager *pm = manager_of(server);
	for (size_t i = 0; i < pm->record_count; ++i) {
		if (pm->records[i].builtin)
			shady_plugin_manager_set_enabled(server, pm->records[i].spec, false);
	}
}

bool shady_plugin_manager_load_defaults(struct shady_server *server) {
	struct shady_plugin_manager *pm = manager_of(server);
	if (pm->defaults_loaded) return true;
	pm->defaults_loaded = true;
	for (size_t i = 0; i < pm->record_count; ++i) {
		struct shady_plugin_record *r = &pm->records[i];
		if (!r->builtin || r->loaded) continue;
		/* config.lua loaded its own build of this plugin by path. */
		if (shady_modules_has_registered(&server->modules, r->spec)) {
			wlr_log(WLR_INFO, "plugin manager: default plugin %s replaced by config", r->spec);
			free(r->spec);
			free(r->path);
			free(r->module_name);
			memmove(r, r + 1, (pm->record_count - i - 1) * sizeof(*r));
			pm->record_count--;
			i--;
			continue;
		}
		if (r->enable < 0) {
			wlr_log(WLR_INFO, "plugin manager: default plugin %s disabled", r->spec);
			continue;
		}
		if (!load_record(server, r)) {
			wlr_log(WLR_ERROR, "plugin manager: failed to load default plugin %s", r->spec);
			return false;
		}
	}
	return true;
}

const char *shady_plugin_manager_module_name(struct shady_server *server,
		const char *name) {
	struct shady_plugin_record *r = find_record(server, name);
	return r && r->module_name ? r->module_name : name;
}

enum shady_plugin_status shady_plugin_manager_status(struct shady_server *server,
		const struct shady_plugin_record *r) {
	if (!r->loaded) {
		if (r->builtin && r->enable < 0) return SHADY_PLUGIN_DISABLED;
		if (r->builtin && !r->failed) return SHADY_PLUGIN_PENDING;
		return SHADY_PLUGIN_FAILED;
	}
	ssize_t index = shady_module_index_by_name(server, r->module_name);
	if (index < 0 || !server->modules.plugin_handle[index]) return SHADY_PLUGIN_UNLOADED;
	return server->modules.active[index] ? SHADY_PLUGIN_ACTIVE : SHADY_PLUGIN_INACTIVE;
}

const char *shady_plugin_status_name(enum shady_plugin_status status) {
	switch (status) {
	case SHADY_PLUGIN_PENDING: return "pending";
	case SHADY_PLUGIN_DISABLED: return "disabled";
	case SHADY_PLUGIN_FAILED: return "failed";
	case SHADY_PLUGIN_ACTIVE: return "active";
	case SHADY_PLUGIN_INACTIVE: return "inactive";
	case SHADY_PLUGIN_UNLOADED: return "unloaded";
	}
	return "unknown";
}

void shady_plugin_manager_finish(struct shady_server *server) {
	struct shady_plugin_manager *pm = manager_of(server);
	for (size_t i = 0; i < pm->search_path_count; ++i) free(pm->search_paths[i]);
	for (size_t i = 0; i < pm->record_count; ++i) {
		free(pm->records[i].spec);
		free(pm->records[i].path);
		free(pm->records[i].module_name);
	}
	*pm = (struct shady_plugin_manager){0};
}
