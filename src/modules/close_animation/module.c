#include "../../module/module.h"
#include <wlr/types/wlr_keyboard.h>
#include "../../shady.h"
#include "close_animation.h"
#include "state.h"

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return bind->sym == sym && bind->modifiers == (modifiers & mask);
}

static bool close_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return false;
	for (int i = 0; i < nsyms; i++) {
		if (bind_matches(&server->config.bind_close_window, syms[i], modifiers)) {
			shady_close_animation_begin(server);
			return true;
		}
	}
	return false;
}

static void close_tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	(void)logical_w; (void)logical_h;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		shady_close_animation_update_toplevel(toplevel, dt);
	}
}

static bool module_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static const char *const provides[] = { "spatial.close-animation", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };
static const char *const optional_requires[] = { "spatial.window-motion", NULL };

static const struct shady_module module = {
	.name = "close-animation",
	.provides = provides,
	.enabled = module_enabled,
	.requires = requires,
	.optional_requires = optional_requires,
	.toplevel_state_size = sizeof(struct shady_close_animation_state),
	.key = close_key,
	.tick = close_tick,
};

const struct shady_module *shady_close_animation_module(void) { return &module; }
