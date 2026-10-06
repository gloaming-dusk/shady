#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <shady/event.h>
#include <shady/plugin.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

static float clamp01(float v) {
	if (v < 0.f) return 0.f;
	if (v > 1.f) return 1.f;
	return v;
}

static bool depth_managed(shady_window window) {
	return window &&
		api->window_valid(host, window) &&
		api->window_mapped(window) &&
		api->window_visible(window) &&
		!api->window_maximized(window) &&
		!api->window_fullscreen(window);
}

static float target_depth(shady_window window, shady_window focused) {
	if (window == focused) return 0.08f;

	size_t rank = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window candidate = api->window_at(host, i);
		if (!depth_managed(candidate) || candidate == focused)
			continue;
		if (candidate == window)
			break;
		rank++;
	}
	return -0.20f - 0.09f * (float)rank;
}

static void request_animation(void) {
	if (api && host)
		api->schedule_render(host);
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;

	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
	case SHADY_EVENT_WINDOW_UNMAPPED:
	case SHADY_EVENT_WINDOW_FOCUSED:
	case SHADY_EVENT_WINDOW_DESTROYED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
	case SHADY_EVENT_WORKSPACE_CHANGED:
		request_animation();
		break;
	default:
		break;
	}
}

static void start(struct shady_server *server) {
	(void)server;
	api->log(SHADY_PLUGIN_LOG_INFO,
		"focus-depth: focused window floats forward; background windows recede");
	request_animation();
}

static void tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	(void)server;
	(void)logical_w;
	(void)logical_h;

	shady_window focused = api->focused_window(host);
	float alpha = clamp01(dt * 9.0f);
	bool moving = false;

	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!depth_managed(window))
			continue;

		double x = 0.0, y = 0.0;
		float z = 0.f;
		if (!api->window_position(window, &x, &y, &z))
			continue;

		float target = target_depth(window, focused);
		float next = z + (target - z) * alpha;
		float delta = target - next;
		if (delta < 0.f) delta = -delta;

		if (delta < 0.0005f)
			next = target;
		else
			moving = true;

		if (next != z)
			api->window_set_position(host, window, x, y, next);
	}

	if (moving)
		request_animation();
}

static const char *const provides[] = {
	"spatial.focus-depth",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "focus-depth",
	.provides = provides,
	.requires = requires,
	.start = start,
	.tick = tick,
};

const struct shady_module *shady_plugin_entry_v1(
		uint32_t host_abi,
		const struct shady_plugin_api_v1 *host_api,
		shady_host host_handle) {
	if (host_abi != SHADY_PLUGIN_ABI_V1 ||
			!host_api ||
			host_api->abi_version != SHADY_PLUGIN_ABI_V1 ||
			host_api->struct_size < sizeof(*host_api)) {
		return NULL;
	}

	api = host_api;
	host = host_handle;

	api->subscribe_event(host, SHADY_EVENT_WINDOW_MAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_UNMAPPED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_STATE_CHANGED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WORKSPACE_CHANGED, on_event, NULL);

	return &module;
}
