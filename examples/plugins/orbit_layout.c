#include <stddef.h>
#include <stdio.h>
#include <shady/plugin.h>
#include <shady/event.h>

static const struct shady_plugin_api_v1 *api;
static shady_host host;

struct slot {
	float x;
	float y;
	float z;
};

/* Deliberately asymmetric: it feels arranged rather than tiled. */
static const struct slot slots[] = {
	{ 0.00f,  0.00f, -0.52f },
	{-0.24f, -0.08f, -0.66f },
	{ 0.24f,  0.08f, -0.70f },
	{-0.34f,  0.24f, -0.76f },
	{ 0.34f, -0.22f, -0.80f },
	{-0.10f,  0.32f, -0.84f },
	{ 0.12f, -0.32f, -0.88f },
	{ 0.40f,  0.26f, -0.92f },
};

static void place_windows(shady_window focused) {
	if (!api->has_capability(host, "spatial.window-state"))
		return;

	size_t visible = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (window && api->window_mapped(window))
			visible++;
	}
	if (visible == 0)
		return;

	int width = 1280, height = 720;
	if (api->output_count(host) > 0) {
		shady_output output = api->output_at(host, 0);
		api->output_size(output, &width, &height);
	}

	size_t cursor = 0;
	for (size_t i = 0; i < api->window_count(host); i++) {
		shady_window window = api->window_at(host, i);
		if (!window || !api->window_mapped(window))
			continue;

		struct slot slot;
		if (visible == 1) {
			slot = slots[0];
		} else {
			slot = slots[1 + (cursor % (sizeof(slots) / sizeof(slots[0]) - 1))];
			if (cursor >= sizeof(slots) / sizeof(slots[0]) - 1) {
				float drift = 0.035f * (float)(cursor - 6);
				slot.x += drift;
				slot.y -= drift * 0.5f;
				slot.z -= drift * 0.4f;
			}
		}

		double x = width * (0.5 + slot.x) - 300.0;
		double y = height * (0.5 + slot.y) - 190.0;
		float z = slot.z;
		if (window == focused)
			z += 0.16f;

		api->window_set_position(host, window, x, y, z);
		cursor++;
	}
}

static void on_event(shady_host event_host,
		const struct shady_event *event, void *user_data) {
	(void)event_host;
	(void)user_data;

	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
		place_windows(event->object.window);
		break;
	case SHADY_EVENT_WINDOW_FOCUSED:
		place_windows(event->object.window);
		break;
	case SHADY_EVENT_WINDOW_DESTROYED:
		place_windows(NULL);
		break;
	default:
		break;
	}
}

static void start(struct shady_server *server) {
	(void)server;
	api->log(SHADY_PLUGIN_LOG_INFO,
		"orbit-layout: arranging windows as a loose constellation");
	place_windows(NULL);
}

static const char *const provides[] = {
	"rice.orbit-layout",
	NULL,
};

static const char *const requires[] = {
	"spatial.window-state",
	NULL,
};

static const struct shady_module module = {
	.name = "orbit-layout",
	.provides = provides,
	.requires = requires,
	.start = start,
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
	api->subscribe_event(host, SHADY_EVENT_WINDOW_FOCUSED, on_event, NULL);
	api->subscribe_event(host, SHADY_EVENT_WINDOW_DESTROYED, on_event, NULL);
	return &module;
}
