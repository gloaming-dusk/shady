/* Compositor plugin for tests/headless-published-values.sh: publishes
 * "probe.greeting" when it starts. Unloading it must remove the value. */
#include <stddef.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static void start(struct shady_server *server) {
	(void)server;
	if (!api->publish_value(host, "probe.greeting", "hello from a plugin"))
		api->log(SHADY_PLUGIN_LOG_ERROR, "publish-probe: publish failed");
	/* Invalid keys are refused. */
	if (api->publish_value(host, "bad key!", "x"))
		api->log(SHADY_PLUGIN_LOG_ERROR, "publish-probe: an invalid key was accepted");
	api->log(SHADY_PLUGIN_LOG_INFO, "publish-probe: published");
}

static const struct shady_module module = {
	.name = "publish-probe",
	.start = start,
};

const struct shady_module *shady_plugin_entry_v1(uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api, shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 || !SHADY_API_HAS(host_api, published_value))
		return NULL;
	api = host_api;
	host = host_handle;
	return &module;
}
