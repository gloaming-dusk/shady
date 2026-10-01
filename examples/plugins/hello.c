#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *host_api;
static void *host_context;

static void hello_start(struct shady_server *server) {
	(void)server;
	host_api->log(SHADY_PLUGIN_LOG_INFO, "hello plugin started");
}

static const char *const provides[] = {
	"example.hello",
	NULL,
};

static const struct shady_module hello_module = {
	.name = "hello-plugin",
	.provides = provides,
	.start = hello_start,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *api,
		void *host) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!api || api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			api->struct_size < sizeof(*api)) {
		return NULL;
	}
	host_api = api;
	host_context = host;
	(void)host_context;
	return &hello_module;
}
