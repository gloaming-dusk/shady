#include <stdbool.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static const struct shady_close_effect slide_effect = {
	.style = SHADY_CLOSE_EFFECT_SLIDE_FADE,
	.duration = 0.52f,
	.strength = 1.0f,
	.direction_x = 0.95f,
	.direction_y = 0.22f,
};

static void apply_window(shady_window window) {
	if (!window || !api->window_valid(host, window) ||
			!api->window_mapped(window)) return;
	if (!api->window_set_close_effect(host, window, &slide_effect)) return;

	struct shady_close_effect got = {0};
	bool overridden = false;
	if (!api->window_close_effect(window, &got, &overridden) ||
			!overridden || got.style != SHADY_CLOSE_EFFECT_SLIDE_FADE) {
		api->log(SHADY_PLUGIN_LOG_ERROR,
			"close-slide-fade: close effect verification failed");
	}
}

static void apply_all(void) {
	for (size_t i = 0; i < api->window_count(host); i++)
		apply_window(api->window_at(host, i));
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;
	if (event->type == SHADY_EVENT_WINDOW_MAPPED)
		apply_all();
}

static void start(struct shady_server *server) {
	(void)server;
	apply_all();
	api->log(SHADY_PLUGIN_LOG_INFO,
		"close-slide-fade: active (plugin close effect override)");
}

static void stop(struct shady_server *server) {
	(void)server;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (api->window_valid(host, window))
			api->window_reset_close_effect(host, window);
	}
}

static const char *const provides[] = {
	"spatial.close-slide-fade",
	NULL,
};

static const char *const requires[] = {
	"spatial.close-animation",
	NULL,
};

static const struct shady_module module = {
	.name = "close-slide-fade",
	.provides = provides,
	.requires = requires,
	.start = start,
	.stop = stop,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api) ||
			!host_api->window_set_close_effect ||
			!host_api->window_close_effect ||
			!host_api->window_reset_close_effect ||
			!host_api->window_close)
		return NULL;

	api = host_api;
	host = host_handle;
	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	return &module;
}
