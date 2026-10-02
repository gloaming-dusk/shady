#include <string.h>
#include <shady/plugin.h>

struct counter_state { unsigned starts; };
struct counter_window_state { unsigned marker; };

static const struct shady_plugin_api_v1 *host_api;
static shady_host host_context;

static void counter_start(struct shady_server *server) {
	(void)server;
	struct counter_state *state =
		host_api->module_state(host_context, "counter-plugin");
	state->starts++;
}

static const char *const provides[] = {
	"example.counter",
	NULL,
};

static const struct shady_module counter_module = {
	.name = "counter-plugin",
	.provides = provides,
	.state_size = sizeof(struct counter_state),
	.toplevel_state_size = sizeof(struct counter_window_state),
	.start = counter_start,
};

static size_t module_snapshot_size(shady_host host, const void *state) {
	(void)host; (void)state;
	return sizeof(struct counter_state);
}

static bool save_module_state(shady_host host, const void *state,
		void *snapshot, size_t snapshot_size) {
	(void)host;
	if (!state || !snapshot || snapshot_size != sizeof(struct counter_state))
		return false;
	memcpy(snapshot, state, sizeof(struct counter_state));
	return true;
}

static bool restore_module_state(shady_host host, void *state,
		const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version) {
	(void)host; (void)state; (void)snapshot; (void)snapshot_size;
	(void)previous_schema_version;
	return false;
}

static size_t window_snapshot_size(shady_host host, shady_window window,
		const void *state) {
	(void)host; (void)window; (void)state;
	return sizeof(struct counter_window_state);
}

static bool save_window_state(shady_host host, shady_window window,
		const void *state, void *snapshot, size_t snapshot_size) {
	(void)host; (void)window;
	if (!state || !snapshot ||
			snapshot_size != sizeof(struct counter_window_state))
		return false;
	memcpy(snapshot, state, sizeof(struct counter_window_state));
	return true;
}

static bool restore_window_state(shady_host host, shady_window window,
		void *state, const void *snapshot, size_t snapshot_size,
		uint32_t previous_schema_version) {
	(void)host; (void)window; (void)state; (void)snapshot;
	(void)snapshot_size; (void)previous_schema_version;
	return false;
}

static const struct shady_plugin_v2 counter_plugin = {
	.struct_size = sizeof(struct shady_plugin_v2),
	.module = &counter_module,
	.state_schema_version = 2,
	.module_snapshot_size = module_snapshot_size,
	.save_module_state = save_module_state,
	.restore_module_state = restore_module_state,
	.window_snapshot_size = window_snapshot_size,
	.save_window_state = save_window_state,
	.restore_window_state = restore_window_state,
};

const struct shady_plugin_v2 *shady_plugin_entry_v2(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *api,
		shady_host host) {
	if (host_abi != SHADY_PLUGIN_ABI_V2 ||
			!api || api->abi_version != SHADY_PLUGIN_ABI_V1)
		return NULL;
	host_api = api;
	host_context = host;
	return &counter_plugin;
}
