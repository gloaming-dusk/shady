#include "../../module/module.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "../../shady.h"
#include "../../render/pick3d.h"
#include "../../render/render.h"
#include "../environment/environment.h"
#include "../desktop/state.h"
#include "state.h"

#define CAMERA_ORBIT_SENS 0.005f
#define CAMERA_PAN_SENS 0.0025f
#define CAMERA_KEY_PAN 0.05f
#define CAMERA_KEY_ORBIT 0.08f
#define CAMERA_ZOOM_STEP 0.15f
#define CAMERA_PITCH_MAX 1.4f
#define CAMERA_DIST_MIN 0.4f
#define CAMERA_DIST_MAX 12.0f
#define WINDOW_Z_STEP 0.055f
#define WINDOW_Z_MIN -1.5f
#define WINDOW_Z_MAX 0.75f

static bool spatial_enabled(struct shady_server *server) {
	return server->config.spatial_mode;
}

static bool bind_matches(const struct shady_keybind *bind,
		xkb_keysym_t sym, uint32_t modifiers) {
	const uint32_t mask = WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT |
		WLR_MODIFIER_CTRL | WLR_MODIFIER_LOGO;
	return xkb_keysym_to_lower(bind->sym) == xkb_keysym_to_lower(sym) &&
		bind->modifiers == (modifiers & mask);
}

static void clamp_camera(struct shady_camera *cam) {
	if (cam->pitch > CAMERA_PITCH_MAX) cam->pitch = CAMERA_PITCH_MAX;
	if (cam->pitch < -CAMERA_PITCH_MAX) cam->pitch = -CAMERA_PITCH_MAX;
	if (cam->distance < CAMERA_DIST_MIN) cam->distance = CAMERA_DIST_MIN;
	if (cam->distance > CAMERA_DIST_MAX) cam->distance = CAMERA_DIST_MAX;
}

static struct shady_toplevel *focused_toplevel(struct shady_server *server) {
	struct wlr_surface *surface = server->seat->keyboard_state.focused_surface;
	if (!surface) return NULL;
	struct wlr_xdg_toplevel *xdg =
		wlr_xdg_toplevel_try_from_wlr_surface(surface);
	if (!xdg) return NULL;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (toplevel->xdg_toplevel == xdg) return toplevel;
	}
	return NULL;
}

static bool spatial_init(struct shady_server *server) {
	shady_spatial_state(server)->runtime.world = shady_world_default();
	if (!shady_environment_load_colliders(server)) {
		wlr_log(WLR_ERROR, "spatial: failed to load environment collision groups");
	}
	shady_camera_reset(&shady_spatial_state(server)->runtime.camera);
	return shady_render_init(server->renderer);
}

static void spatial_destroy(struct shady_server *server) {
	(void)server;
	shady_render_fini();
}

static void spatial_toplevel_unmap(struct shady_toplevel *toplevel) {
	shady_render_toplevel_unmap(toplevel);
}

static void spatial_toplevel_commit(struct shady_toplevel *toplevel) {
	shady_render_toplevel_commit(toplevel);
}

static void spatial_toplevel_destroy(struct shady_toplevel *toplevel) {
	shady_render_toplevel_destroy(toplevel);
}

static bool spatial_pick_surface(struct shady_server *server,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy, struct shady_toplevel **toplevel) {
	if (shady_desktop_state(server)->session_locked) return false;
	*toplevel = shady_toplevel_at_3d(server, lx, ly, surface, sx, sy);
	return *surface != NULL;
}

static bool spatial_pointer_motion(struct shady_server *server,
		struct wlr_pointer_motion_event *event) {
	if (shady_spatial_state(server)->runtime.camera.first_person) return false;
	if (server->cursor_mode == SHADY_CURSOR_CAMERA_ORBIT) {
		shady_spatial_state(server)->runtime.camera.yaw -=
			(float)event->delta_x * CAMERA_ORBIT_SENS;
		shady_spatial_state(server)->runtime.camera.pitch -=
			(float)event->delta_y * CAMERA_ORBIT_SENS;
		clamp_camera(&shady_spatial_state(server)->runtime.camera);
		shady_render_schedule_all_outputs(server);
		return true;
	}
	if (server->cursor_mode == SHADY_CURSOR_CAMERA_PAN) {
		struct shady_vec3 right, up;
		shady_camera_basis(&shady_spatial_state(server)->runtime.camera, &right, &up, NULL);
		float scale = shady_spatial_state(server)->runtime.camera.distance * CAMERA_PAN_SENS;
		shady_spatial_state(server)->runtime.camera.target_x +=
			-right.x * (float)event->delta_x * scale +
			up.x * (float)event->delta_y * scale;
		shady_spatial_state(server)->runtime.camera.target_y +=
			-right.y * (float)event->delta_x * scale +
			up.y * (float)event->delta_y * scale;
		shady_spatial_state(server)->runtime.camera.target_z +=
			-right.z * (float)event->delta_x * scale +
			up.z * (float)event->delta_y * scale;
		shady_render_schedule_all_outputs(server);
		return true;
	}
	return false;
}

static bool spatial_pointer_button(struct shady_server *server,
		struct wlr_pointer_button_event *event, uint32_t modifiers) {
	if (event->button == BTN_LEFT) {
		if (event->state == WL_POINTER_BUTTON_STATE_RELEASED &&
				server->cursor_mode == SHADY_CURSOR_MOVE && server->grabbed_toplevel) {
			reset_cursor_mode(server);
			return true;
		}
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED &&
				!shady_spatial_state(server)->runtime.camera.first_person) {
			struct shady_toplevel *titlebar = shady_titlebar_at_3d(server,
				server->cursor->x, server->cursor->y);
			if (titlebar) {
				focus_toplevel(titlebar);
				shady_toplevel_begin_interactive(titlebar, SHADY_CURSOR_MOVE, 0);
				wlr_seat_pointer_clear_focus(server->seat);
				return true;
			}
		}
	}
	if (shady_spatial_state(server)->runtime.camera.first_person) return false;
	if (event->button == BTN_RIGHT ||
			(event->button == BTN_MIDDLE && (modifiers & WLR_MODIFIER_ALT))) {
		if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
			server->cursor_mode = event->button == BTN_RIGHT
				? SHADY_CURSOR_CAMERA_ORBIT : SHADY_CURSOR_CAMERA_PAN;
			wlr_seat_pointer_clear_focus(server->seat);
		} else if (server->cursor_mode == SHADY_CURSOR_CAMERA_ORBIT ||
				server->cursor_mode == SHADY_CURSOR_CAMERA_PAN) {
			reset_cursor_mode(server);
		}
		return true;
	}
	return false;
}

static bool spatial_pointer_axis(struct shady_server *server,
		struct wlr_pointer_axis_event *event, uint32_t modifiers) {
	if (shady_spatial_state(server)->runtime.camera.first_person) return false;
	if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL ||
			!(modifiers & WLR_MODIFIER_ALT)) return false;

	if (modifiers & WLR_MODIFIER_SHIFT) {
		struct shady_toplevel *toplevel = focused_toplevel(server);
		if (toplevel) {
			float direction = event->delta < 0.0 ? 1.0f : -1.0f;
			shady_spatial_toplevel_state(toplevel)->z += direction * WINDOW_Z_STEP;
			if (shady_spatial_toplevel_state(toplevel)->z < WINDOW_Z_MIN)
				shady_spatial_toplevel_state(toplevel)->z = WINDOW_Z_MIN;
			if (shady_spatial_toplevel_state(toplevel)->z > WINDOW_Z_MAX)
				shady_spatial_toplevel_state(toplevel)->z = WINDOW_Z_MAX;
			shady_render_schedule_all_outputs(server);
		}
		return true;
	}

	shady_spatial_state(server)->runtime.camera.distance += (float)(event->delta * 0.01);
	clamp_camera(&shady_spatial_state(server)->runtime.camera);
	shady_render_schedule_all_outputs(server);
	return true;
}

static bool spatial_key(struct shady_server *server, const xkb_keysym_t *syms,
		int nsyms, uint32_t state, uint32_t modifiers) {
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return false;

	struct shady_config *c = &server->config;
	for (int i = 0; i < nsyms; i++) {
		xkb_keysym_t sym = syms[i];
		if (bind_matches(&c->bind_debug_ray, sym, modifiers)) {
			shady_spatial_state(server)->runtime.debug_ray = !shady_spatial_state(server)->runtime.debug_ray;
			shady_render_schedule_all_outputs(server);
			return true;
		}
		bool changed = false;
		struct shady_vec3 right, up, forward;
		if (bind_matches(&c->bind_camera_left, sym, modifiers) ||
				bind_matches(&c->bind_camera_right, sym, modifiers) ||
				bind_matches(&c->bind_camera_up, sym, modifiers) ||
				bind_matches(&c->bind_camera_down, sym, modifiers)) {
			shady_camera_basis(&shady_spatial_state(server)->runtime.camera,
				&right, &up, &forward);
			float sign = (bind_matches(&c->bind_camera_left, sym, modifiers) ||
				bind_matches(&c->bind_camera_down, sym, modifiers)) ? -1.f : 1.f;
			struct shady_vec3 v =
				(bind_matches(&c->bind_camera_left, sym, modifiers) ||
				bind_matches(&c->bind_camera_right, sym, modifiers)) ? right : up;
			shady_spatial_state(server)->runtime.camera.target_x += v.x * CAMERA_KEY_PAN * sign;
			shady_spatial_state(server)->runtime.camera.target_y += v.y * CAMERA_KEY_PAN * sign;
			shady_spatial_state(server)->runtime.camera.target_z += v.z * CAMERA_KEY_PAN * sign;
			changed = true;
		} else if (bind_matches(&c->bind_camera_yaw_left, sym, modifiers)) {
			shady_spatial_state(server)->runtime.camera.yaw += CAMERA_KEY_ORBIT; changed = true;
		} else if (bind_matches(&c->bind_camera_yaw_right, sym, modifiers)) {
			shady_spatial_state(server)->runtime.camera.yaw -= CAMERA_KEY_ORBIT; changed = true;
		} else if (bind_matches(&c->bind_camera_zoom_in, sym, modifiers)) {
			shady_spatial_state(server)->runtime.camera.distance -= CAMERA_ZOOM_STEP; changed = true;
		} else if (bind_matches(&c->bind_camera_zoom_out, sym, modifiers)) {
			shady_spatial_state(server)->runtime.camera.distance += CAMERA_ZOOM_STEP; changed = true;
		} else if (bind_matches(&c->bind_camera_reset, sym, modifiers)) {
			shady_camera_reset(&shady_spatial_state(server)->runtime.camera); changed = true;
		}
		if (changed) {
			clamp_camera(&shady_spatial_state(server)->runtime.camera);
			shady_render_schedule_all_outputs(server);
			return true;
		}
	}
	return false;
}

static const char *const spatial_provides[] = {
	"spatial",
	"spatial.window-state",
	NULL,
};

static const char *const spatial_requires[] = {
	"desktop.protocols",
	NULL,
};

static const struct shady_module spatial_module = {
	.name = "spatial",
	.provides = spatial_provides,
	.requires = spatial_requires,
	.state_size = sizeof(struct shady_spatial_state),
	.toplevel_state_size = sizeof(struct shady_toplevel_experimental_state),
	.enabled = spatial_enabled,
	.init = spatial_init,
	.destroy = spatial_destroy,
	.toplevel_unmap = spatial_toplevel_unmap,
	.toplevel_commit = spatial_toplevel_commit,
	.toplevel_destroy = spatial_toplevel_destroy,
	.key = spatial_key,
	.pointer_motion = spatial_pointer_motion,
	.pointer_button = spatial_pointer_button,
	.pointer_axis = spatial_pointer_axis,
	.pick_surface = spatial_pick_surface,
};

const struct shady_module *shady_spatial_module(void) {
	return &spatial_module;
}
