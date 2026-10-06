#include "../../module/module.h"
#include <wlr/types/wlr_keyboard.h>
#include "../../shady.h"
#include "physics.h"
#include "state.h"

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return xkb_keysym_to_lower(bind->sym) == xkb_keysym_to_lower(sym) &&
		bind->modifiers == (modifiers & mask);
}

static bool physics_init_module(struct shady_server *server) {
	shady_physics_init(server);
	return true;
}

static void physics_tick(struct shady_server *server, float dt,
		float logical_w, float logical_h) {
	shady_physics_update(server, dt, logical_w, logical_h);
}

static bool physics_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return false;
	for (int i = 0; i < nsyms; i++) {
		if (bind_matches(&server->config.bind_gravity_toggle, syms[i], modifiers)) {
			shady_physics_toggle_gravity(server);
			return true;
		}
	}
	return false;
}

static bool module_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static const char *const provides[] = { "spatial.physics", NULL };
static const char *const requires[] = { "spatial.window-state", NULL };
static const char *const optional_requires[] = {
	"spatial.window-motion", NULL
};

static const struct shady_module module = {
	.name = "physics",
	.provides = provides,
	.enabled = module_enabled,
	.requires = requires,
	.optional_requires = optional_requires,
	.state_size = sizeof(struct shady_physics_state),
	.toplevel_state_size = sizeof(struct shady_window_physics_state),
	.init = physics_init_module,
	.key = physics_key,
	.tick = physics_tick,
};

const struct shady_module *shady_physics_module(void) { return &module; }
