#include <stdio.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *host_api;
static shady_host host_context;

static void hello_start(struct shady_server *server) {
	(void)server;
	host_api->log(SHADY_PLUGIN_LOG_INFO, "hello plugin started");
	shady_seat seat = host_api->seat(host_context);
	char message[256];
	snprintf(message, sizeof(message), "seat=%s outputs=%zu modules=%zu",
		host_api->seat_name(seat),
		host_api->output_count(host_context),
		host_api->module_count(host_context));
	host_api->log(SHADY_PLUGIN_LOG_INFO, message);
	for (size_t i = 0; i < host_api->output_count(host_context); i++) {
		shady_output output = host_api->output_at(host_context, i);
		int width = 0, height = 0;
		host_api->output_size(output, &width, &height);
		snprintf(message, sizeof(message), "output[%zu]=%s %dx%d scale=%.2f",
			i, host_api->output_name(output), width, height,
			host_api->output_scale(output));
		host_api->log(SHADY_PLUGIN_LOG_INFO, message);
	}
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
		shady_host host) {
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
