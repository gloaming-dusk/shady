#ifndef SHADY_PUBLIC_PLUGIN_H
#define SHADY_PUBLIC_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <shady/module.h>

#define SHADY_PLUGIN_ABI_V1 1u
#define SHADY_PLUGIN_ENTRY_V1 "shady_plugin_entry_v1"
#define SHADY_PLUGIN_ABI_V2 2u
#define SHADY_PLUGIN_ENTRY_V2 "shady_plugin_entry_v2"

/* Opaque host objects. Plugins must only inspect them through the API table. */
struct shady_host_handle;
struct shady_window_handle;
struct shady_output_handle;
struct shady_seat_handle;
struct shady_module_handle;

typedef struct shady_host_handle *shady_host;
typedef struct shady_window_handle *shady_window;
typedef struct shady_output_handle *shady_output;
typedef struct shady_seat_handle *shady_seat;
typedef struct shady_module_handle *shady_module_handle;
typedef uint64_t shady_subscription_id;
struct shady_event;
typedef void (*shady_event_callback)(
	shady_host host,
	const struct shady_event *event,
	void *user_data);

enum shady_plugin_log_level {
	SHADY_PLUGIN_LOG_DEBUG = 0,
	SHADY_PLUGIN_LOG_INFO = 1,
	SHADY_PLUGIN_LOG_ERROR = 2,
};

struct shady_plugin_api_v1 {
	uint32_t abi_version;
	uint32_t struct_size;

	void (*log)(enum shady_plugin_log_level level, const char *message);
	bool (*has_capability)(shady_host host, const char *capability);
	bool (*config_set)(shady_host host, const char *key, const char *value);
	void *(*module_state)(shady_host host, const char *module_name);
	void *(*window_state)(shady_window window, const char *module_name);

	size_t (*window_count)(shady_host host);
	shady_window (*window_at)(shady_host host, size_t index);
	const char *(*window_title)(shady_window window);
	const char *(*window_app_id)(shady_window window);
	bool (*window_valid)(shady_host host, shady_window window);
	bool (*window_mapped)(shady_window window);
	bool (*window_focus)(shady_host host, shady_window window);
	bool (*window_close)(shady_host host, shady_window window);

	size_t (*output_count)(shady_host host);
	shady_output (*output_at)(shady_host host, size_t index);
	const char *(*output_name)(shady_output output);
	bool (*output_valid)(shady_host host, shady_output output);
	void (*output_size)(shady_output output, int *width, int *height);
	float (*output_scale)(shady_output output);

	shady_seat (*seat)(shady_host host);
	const char *(*seat_name)(shady_seat seat);

	size_t (*module_count)(shady_host host);
	shady_module_handle (*module_at)(shady_host host, size_t index);
	const char *(*module_name)(shady_module_handle module);
	bool (*module_active)(shady_host host, shady_module_handle module);

	const char *(*event_name)(uint32_t event_type);
	bool (*subscribe_event)(shady_host host, uint32_t event_type,
		shady_event_callback callback, void *user_data);
	shady_subscription_id (*subscribe_event_handle)(shady_host host,
		uint32_t event_type, shady_event_callback callback, void *user_data);
	bool (*unsubscribe_event)(shady_host host, shady_subscription_id subscription);
	void (*schedule_render)(shady_host host);
	void (*terminate)(shady_host host);
};

typedef const struct shady_module *(*shady_plugin_entry_v1_fn)(
	uint32_t host_abi,
	const struct shady_plugin_api_v1 *api,
	shady_host host);

/*
 * V2 adds explicit state migration without changing struct shady_module.
 * Snapshot buffers are allocated and freed by the host. Plugins only write
 * into/read from those buffers, so snapshots remain valid across dlclose().
 */
struct shady_plugin_v2 {
	uint32_t struct_size;
	const struct shady_module *module;
	uint32_t state_schema_version;

	size_t (*module_snapshot_size)(shady_host host, const void *state);
	bool (*save_module_state)(shady_host host, const void *state,
		void *snapshot, size_t snapshot_size);
	bool (*restore_module_state)(shady_host host, void *state,
		const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version);

	size_t (*window_snapshot_size)(shady_host host, shady_window window,
		const void *state);
	bool (*save_window_state)(shady_host host, shady_window window,
		const void *state, void *snapshot, size_t snapshot_size);
	bool (*restore_window_state)(shady_host host, shady_window window,
		void *state, const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version);
};

typedef const struct shady_plugin_v2 *(*shady_plugin_entry_v2_fn)(
	uint32_t host_abi,
	const struct shady_plugin_api_v1 *api,
	shady_host host);

#endif
