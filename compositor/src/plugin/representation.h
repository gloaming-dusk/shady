#ifndef SHADY_PLUGIN_REPRESENTATION_H
#define SHADY_PLUGIN_REPRESENTATION_H
#include "plugin.h"
#include <shady/plugin.h>
bool host_window_set_representation(shady_host host, shady_window window,
		const struct shady_window_representation *representation);
bool host_window_reset_representation(shady_host host, shady_window window);
bool host_window_set_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider);
bool host_window_reset_representation_provider(shady_host host,
		shady_window window,
		const struct shady_window_representation_provider *provider);
void *host_window_representation_state(shady_host host, shady_window window,
		const struct shady_window_representation_provider *provider,
		size_t *state_size);
bool host_window_representation(shady_window window,
		struct shady_window_representation *representation, bool *overridden);
extern const struct shady_representation_api_v1 shady_representation_api;
#endif
