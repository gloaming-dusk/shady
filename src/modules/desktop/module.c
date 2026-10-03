#include "../../module/module.h"

#include <wayland-server-core.h>
#include <stdlib.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>

#include "../../shady.h"
#include "../../shell_protocol.h"
#include "ime.h"
#include "state.h"

static void cursor_shape_request(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, cursor_shape_request);
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER)
		return;
	if (state->server->seat->pointer_state.focused_client != event->seat_client)
		return;
	const char *name = wlr_cursor_shape_v1_name(event->shape);
	if (name) wlr_cursor_set_xcursor(state->server->cursor,
		state->server->cursor_mgr, name);
}

static void xdg_activation_request(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, xdg_activation_request);
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	if (state->session_locked || !event->surface) return;
	if (event->token && event->token->seat && event->token->seat != state->server->seat)
		return;

	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &state->server->all_toplevels, all_link) {
		if (toplevel->xdg_toplevel &&
				toplevel->xdg_toplevel->base->surface == event->surface &&
				event->surface->mapped) {
			focus_toplevel(toplevel);
			return;
		}
	}
}

struct pending_virtual_keyboard {
	struct wl_list link;
	struct shady_desktop_state *state;
	struct wlr_virtual_keyboard_v1 *virtual_keyboard;
	struct wl_listener keymap;
	struct wl_listener destroy;
};

static void pending_virtual_keyboard_destroy(struct pending_virtual_keyboard *pending) {
	wl_list_remove(&pending->keymap.link);
	wl_list_remove(&pending->destroy.link);
	wl_list_remove(&pending->link);
	free(pending);
}

static void pending_virtual_keyboard_keymap(struct wl_listener *listener, void *data) {
	(void)data;
	struct pending_virtual_keyboard *pending =
		wl_container_of(listener, pending, keymap);
	struct shady_server *server = pending->state->server;
	struct wlr_input_device *device = &pending->virtual_keyboard->keyboard.base;
	pending_virtual_keyboard_destroy(pending);
	shady_input_add_keyboard(server, device);
}

static void pending_virtual_keyboard_gone(struct wl_listener *listener, void *data) {
	(void)data;
	struct pending_virtual_keyboard *pending =
		wl_container_of(listener, pending, destroy);
	pending_virtual_keyboard_destroy(pending);
}

static void new_virtual_keyboard(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, new_virtual_keyboard);
	struct wlr_virtual_keyboard_v1 *virtual_keyboard = data;
	if (!virtual_keyboard || virtual_keyboard->seat != state->server->seat) return;
	if (virtual_keyboard->has_keymap) {
		shady_input_add_keyboard(state->server, &virtual_keyboard->keyboard.base);
		return;
	}

	struct pending_virtual_keyboard *pending = calloc(1, sizeof(*pending));
	if (!pending) return;
	pending->state = state;
	pending->virtual_keyboard = virtual_keyboard;
	pending->keymap.notify = pending_virtual_keyboard_keymap;
	wl_signal_add(&virtual_keyboard->keyboard.events.keymap, &pending->keymap);
	pending->destroy.notify = pending_virtual_keyboard_gone;
	wl_signal_add(&virtual_keyboard->keyboard.base.events.destroy, &pending->destroy);
	wl_list_insert(&state->pending_virtual_keyboards, &pending->link);
}

static bool desktop_protocols_init(struct shady_server *server) {
	struct shady_desktop_state *state = shady_desktop_state(server);
	state->server = server;
	wl_list_init(&state->layer_surfaces);
	wl_list_init(&state->pending_virtual_keyboards);
	state->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
	if (!state->layer_shell) return false;
	state->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&state->layer_shell->events.new_surface,
		&state->new_layer_surface);
	if (!shady_shell_protocol_init(server)) return false;

	state->relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(server->wl_display);
	state->pointer_constraints =
		wlr_pointer_constraints_v1_create(server->wl_display);
	state->screencopy_manager =
		wlr_screencopy_manager_v1_create(server->wl_display);
	state->idle_notifier =
		wlr_idle_notifier_v1_create(server->wl_display);
	state->primary_selection_manager =
		wlr_primary_selection_v1_device_manager_create(server->wl_display);
	state->data_control_manager =
		wlr_data_control_manager_v1_create(server->wl_display);
	state->xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server->wl_display);
	state->xdg_output_manager =
		wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout);
	state->xdg_activation = wlr_xdg_activation_v1_create(server->wl_display);
	if (!state->xdg_activation) return false;
	state->xdg_activation_request.notify = xdg_activation_request;
	wl_signal_add(&state->xdg_activation->events.request_activate,
		&state->xdg_activation_request);
	state->fractional_scale_manager =
		wlr_fractional_scale_manager_v1_create(server->wl_display, 1);
	state->viewporter = wlr_viewporter_create(server->wl_display);
	state->cursor_shape_manager =
		wlr_cursor_shape_manager_v1_create(server->wl_display, 1);
	state->virtual_keyboard_manager =
		wlr_virtual_keyboard_manager_v1_create(server->wl_display);
	if (!state->fractional_scale_manager || !state->viewporter ||
			!state->cursor_shape_manager || !state->virtual_keyboard_manager) return false;
	state->cursor_shape_request.notify = cursor_shape_request;
	wl_signal_add(&state->cursor_shape_manager->events.request_set_shape,
		&state->cursor_shape_request);
	state->new_virtual_keyboard.notify = new_virtual_keyboard;
	wl_signal_add(&state->virtual_keyboard_manager->events.new_virtual_keyboard,
		&state->new_virtual_keyboard);
	if (!shady_ime_init(server)) return false;

	state->output_manager =
		wlr_output_manager_v1_create(server->wl_display);
	if (!state->output_manager) return false;
	state->output_manager_apply.notify = shady_output_manager_apply;
	wl_signal_add(&state->output_manager->events.apply,
		&state->output_manager_apply);
	state->output_manager_test.notify = shady_output_manager_test;
	wl_signal_add(&state->output_manager->events.test,
		&state->output_manager_test);

	state->session_lock_manager =
		wlr_session_lock_manager_v1_create(server->wl_display);
	if (!state->session_lock_manager) return false;
	state->new_session_lock.notify = server_new_session_lock;
	wl_signal_add(&state->session_lock_manager->events.new_lock,
		&state->new_session_lock);

	return state->relative_pointer_manager &&
		state->pointer_constraints &&
		state->screencopy_manager &&
		state->idle_notifier &&
		state->primary_selection_manager &&
		state->data_control_manager &&
		state->xdg_decoration_manager &&
		state->xdg_output_manager &&
		state->xdg_activation &&
		state->fractional_scale_manager &&
		state->viewporter &&
		state->cursor_shape_manager;
}

static void desktop_protocols_destroy(struct shady_server *server) {
	struct shady_desktop_state *state = shady_desktop_state(server);
	shady_ime_finish(server);
	shady_shell_protocol_finish(server);
	if (state->cursor_shape_request.link.prev)
		wl_list_remove(&state->cursor_shape_request.link);
	if (state->new_virtual_keyboard.link.prev)
		wl_list_remove(&state->new_virtual_keyboard.link);
	struct pending_virtual_keyboard *pending, *pending_tmp;
	wl_list_for_each_safe(pending, pending_tmp,
			&state->pending_virtual_keyboards, link) {
		pending_virtual_keyboard_destroy(pending);
	}
	if (state->xdg_activation_request.link.prev)
		wl_list_remove(&state->xdg_activation_request.link);
	if (state->new_layer_surface.link.prev)
		wl_list_remove(&state->new_layer_surface.link);
	if (state->output_manager_apply.link.prev)
		wl_list_remove(&state->output_manager_apply.link);
	if (state->output_manager_test.link.prev)
		wl_list_remove(&state->output_manager_test.link);
	if (state->new_session_lock.link.prev)
		wl_list_remove(&state->new_session_lock.link);
}

static const char *const desktop_provides[] = {
	"desktop.protocols",
	"desktop.overlay",
	"desktop.pointer-protocols",
	NULL,
};

static const struct shady_module desktop_protocols_module = {
	.name = "desktop-protocols",
	.provides = desktop_provides,
	.state_size = sizeof(struct shady_desktop_state),
	.init = desktop_protocols_init,
	.destroy = desktop_protocols_destroy,
};

const struct shady_module *shady_desktop_protocols_module(void) {
	return &desktop_protocols_module;
}
