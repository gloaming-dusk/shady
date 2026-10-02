#include "../../module/module.h"

#include <wayland-server-core.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "../../shady.h"
#include "../../shell_protocol.h"
#include "state.h"

static bool desktop_protocols_init(struct shady_server *server) {
	struct shady_desktop_state *state = shady_desktop_state(server);
	state->server = server;
	wl_list_init(&state->layer_surfaces);
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
		state->xdg_decoration_manager;
}

static void desktop_protocols_destroy(struct shady_server *server) {
	struct shady_desktop_state *state = shady_desktop_state(server);
	shady_shell_protocol_finish(server);
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
