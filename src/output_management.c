#include <stdbool.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>

#include "shady.h"
#include "modules/desktop/state.h"

void shady_output_manager_publish(struct shady_server *server) {
	if (!shady_desktop_state(server)->output_manager) {
		return;
	}
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		struct wlr_output_configuration_head_v1 *head =
			wlr_output_configuration_head_v1_create(
				config, output->wlr_output);
		double lx = 0, ly = 0;
		wlr_output_layout_output_coords(
			server->output_layout, output->wlr_output, &lx, &ly);
		head->state.x = (int32_t)lx;
		head->state.y = (int32_t)ly;
	}
	wlr_output_manager_v1_set_configuration(shady_desktop_state(server)->output_manager, config);
}

static bool handle_configuration(struct shady_server *server,
		struct wlr_output_configuration_v1 *config, bool apply) {
	bool ok = true;
	struct wlr_output_configuration_head_v1 *head;

	wl_list_for_each(head, &config->heads, link) {
		struct wlr_output_state state;
		wlr_output_state_init(&state);
		wlr_output_head_v1_state_apply(&head->state, &state);

		if (apply) {
			if (!wlr_output_commit_state(head->state.output, &state)) {
				ok = false;
			} else if (head->state.enabled) {
				wlr_output_layout_add(
					server->output_layout, head->state.output,
					head->state.x, head->state.y);
			} else {
				wlr_output_layout_remove(
					server->output_layout, head->state.output);
			}
		} else if (!wlr_output_test_state(head->state.output, &state)) {
			ok = false;
		}

		wlr_output_state_finish(&state);
	}

	if (ok) {
		wlr_output_configuration_v1_send_succeeded(config);
		if (apply) {
			shady_recover_toplevels_to_outputs(server);
			shady_output_manager_publish(server);
		}
	} else {
		wlr_output_configuration_v1_send_failed(config);
	}
	wlr_output_configuration_v1_destroy(config);
	return ok;
}

void shady_output_manager_apply(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, output_manager_apply);
	struct shady_server *server = state->server;
	handle_configuration(server, data, true);
}

void shady_output_manager_test(struct wl_listener *listener, void *data) {
	struct shady_desktop_state *state =
		wl_container_of(listener, state, output_manager_test);
	struct shady_server *server = state->server;
	handle_configuration(server, data, false);
}
