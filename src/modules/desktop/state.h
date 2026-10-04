#ifndef SHADY_MODULE_DESKTOP_STATE_H
#define SHADY_MODULE_DESKTOP_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <wayland-server-core.h>
#include "../../module/module.h"

struct wlr_layer_shell_v1;
struct wlr_relative_pointer_manager_v1;
struct wlr_pointer_constraints_v1;
struct wlr_pointer_constraint_v1;
struct wlr_screencopy_manager_v1;
struct wlr_idle_notifier_v1;
struct wlr_primary_selection_v1_device_manager;
struct wlr_data_control_manager_v1;
struct wlr_xdg_decoration_manager_v1;
struct wlr_xdg_output_manager_v1;
struct wlr_virtual_keyboard_manager_v1;
struct wlr_xdg_activation_v1;
struct wlr_fractional_scale_manager_v1;
struct wlr_viewporter;
struct wlr_cursor_shape_manager_v1;
struct wlr_output_manager_v1;
struct wlr_session_lock_manager_v1;
struct wlr_session_lock_v1;
struct wlr_scene_tree;

struct shady_server;
struct shady_shell_protocol_state;
struct shady_standard_protocols;
struct shady_ime_state;

struct shady_desktop_state {
	struct shady_server *server;
	struct wlr_layer_shell_v1 *layer_shell;
	struct shady_shell_protocol_state *shell_protocol;
	struct shady_standard_protocols *standard_protocols;
	struct wl_listener new_layer_surface;
	struct wl_list layer_surfaces;
	struct wl_listener output_layout_change;

	struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;
	struct wlr_pointer_constraints_v1 *pointer_constraints;
	struct wlr_pointer_constraint_v1 *active_pointer_constraint;
	struct wlr_screencopy_manager_v1 *screencopy_manager;
	struct wlr_idle_notifier_v1 *idle_notifier;
	struct wlr_primary_selection_v1_device_manager *primary_selection_manager;
	struct wlr_data_control_manager_v1 *data_control_manager;
	struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
	struct wlr_xdg_output_manager_v1 *xdg_output_manager;
	struct wlr_xdg_activation_v1 *xdg_activation;
	struct wl_listener xdg_activation_request;
	struct wlr_fractional_scale_manager_v1 *fractional_scale_manager;
	struct wlr_viewporter *viewporter;
	struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;
	struct wl_listener cursor_shape_request;
	struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;
	struct wl_listener new_virtual_keyboard;
	struct wl_list pending_virtual_keyboards;
	struct shady_ime_state *ime;

	struct wlr_output_manager_v1 *output_manager;
	struct wl_listener output_manager_apply;
	struct wl_listener output_manager_test;

	struct wlr_session_lock_manager_v1 *session_lock_manager;
	struct wl_listener new_session_lock;
	struct wlr_session_lock_v1 *session_lock;
	struct wlr_scene_tree *session_lock_tree;
	bool session_locked;
	bool session_lock_pending_locked_event;
	size_t session_lock_pending_outputs;
};

static inline struct shady_desktop_state *shady_desktop_state(struct shady_server *server) {
	return shady_module_state(server, "desktop-protocols");
}

#endif
