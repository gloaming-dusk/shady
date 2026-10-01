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

static bool desktop_protocols_init(struct shady_server *server) {
	wl_list_init(&server->layer_surfaces);
	server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
	if (!server->layer_shell) return false;
	server->new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server->layer_shell->events.new_surface,
		&server->new_layer_surface);

	server->relative_pointer_manager =
		wlr_relative_pointer_manager_v1_create(server->wl_display);
	server->pointer_constraints =
		wlr_pointer_constraints_v1_create(server->wl_display);
	server->screencopy_manager =
		wlr_screencopy_manager_v1_create(server->wl_display);
	server->idle_notifier =
		wlr_idle_notifier_v1_create(server->wl_display);
	server->primary_selection_manager =
		wlr_primary_selection_v1_device_manager_create(server->wl_display);
	server->data_control_manager =
		wlr_data_control_manager_v1_create(server->wl_display);
	server->xdg_decoration_manager =
		wlr_xdg_decoration_manager_v1_create(server->wl_display);

	server->output_manager =
		wlr_output_manager_v1_create(server->wl_display);
	if (!server->output_manager) return false;
	server->output_manager_apply.notify = shady_output_manager_apply;
	wl_signal_add(&server->output_manager->events.apply,
		&server->output_manager_apply);
	server->output_manager_test.notify = shady_output_manager_test;
	wl_signal_add(&server->output_manager->events.test,
		&server->output_manager_test);

	server->session_lock_manager =
		wlr_session_lock_manager_v1_create(server->wl_display);
	if (!server->session_lock_manager) return false;
	server->new_session_lock.notify = server_new_session_lock;
	wl_signal_add(&server->session_lock_manager->events.new_lock,
		&server->new_session_lock);

	return server->relative_pointer_manager &&
		server->pointer_constraints &&
		server->screencopy_manager &&
		server->idle_notifier &&
		server->primary_selection_manager &&
		server->data_control_manager &&
		server->xdg_decoration_manager;
}

static void desktop_protocols_destroy(struct shady_server *server) {
	if (server->new_layer_surface.link.prev)
		wl_list_remove(&server->new_layer_surface.link);
	if (server->output_manager_apply.link.prev)
		wl_list_remove(&server->output_manager_apply.link);
	if (server->output_manager_test.link.prev)
		wl_list_remove(&server->output_manager_test.link);
	if (server->new_session_lock.link.prev)
		wl_list_remove(&server->new_session_lock.link);
}

static const struct shady_module desktop_protocols_module = {
	.name = "desktop-protocols",
	.init = desktop_protocols_init,
	.destroy = desktop_protocols_destroy,
};

const struct shady_module *shady_desktop_protocols_module(void) {
	return &desktop_protocols_module;
}
