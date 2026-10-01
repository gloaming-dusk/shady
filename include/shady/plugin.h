#ifndef SHADY_PUBLIC_PLUGIN_H
#define SHADY_PUBLIC_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include <shady/module.h>

#define SHADY_PLUGIN_ABI_V1 1u
#define SHADY_PLUGIN_ENTRY_V1 "shady_plugin_entry_v1"

enum shady_plugin_log_level {
	SHADY_PLUGIN_LOG_DEBUG = 0,
	SHADY_PLUGIN_LOG_INFO = 1,
	SHADY_PLUGIN_LOG_ERROR = 2,
};

struct shady_plugin_api_v1 {
	uint32_t abi_version;
	uint32_t struct_size;
	void (*log)(enum shady_plugin_log_level level, const char *message);
	bool (*has_capability)(void *host, const char *capability);
	bool (*config_set)(void *host, const char *key, const char *value);
	void *(*module_state)(void *host, const char *module_name);
	void *(*window_state)(void *window, const char *module_name);
	const char *(*window_title)(void *window);
	const char *(*window_app_id)(void *window);
	void (*schedule_render)(void *host);
	void (*terminate)(void *host);
};

typedef const struct shady_module *(*shady_plugin_entry_v1_fn)(
	uint32_t host_abi,
	const struct shady_plugin_api_v1 *api,
	void *host);

#endif
