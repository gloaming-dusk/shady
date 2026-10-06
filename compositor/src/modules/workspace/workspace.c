#include "workspace.h"

#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "../../shady.h"
#include "../../event/event.h"
#include "../../render/render.h"
#include "state.h"

static ssize_t find_workspace(struct shady_workspace_state *state,
		const char *name) {
	if (!state || !name || !*name) return -1;
	for (size_t i = 0; i < state->count; i++) {
		if (strcmp(state->names[i], name) == 0) return (ssize_t)i;
	}
	return -1;
}

static ssize_t ensure_workspace(struct shady_workspace_state *state,
		const char *name) {
	ssize_t found = find_workspace(state, name);
	if (found >= 0) return found;
	if (!state || !name || !*name || state->count >= SHADY_MAX_WORKSPACES)
		return -1;
	snprintf(state->names[state->count], SHADY_WORKSPACE_NAME_MAX, "%s", name);
	return (ssize_t)state->count++;
}

static bool on_current_workspace(struct shady_toplevel *toplevel) {
	struct shady_workspace_state *state =
		shady_workspace_state_for(toplevel->server);
	struct shady_workspace_toplevel_state *window =
		shady_workspace_toplevel_state_for(toplevel);
	return state && window && window->workspace == state->current;
}

static bool toplevel_live_on_workspace(struct shady_server *server,
		struct shady_toplevel *needle, size_t workspace) {
	if (!needle) return false;
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (toplevel != needle) continue;
		struct shady_workspace_toplevel_state *window =
			shady_workspace_toplevel_state_for(toplevel);
		return window && window->workspace == workspace &&
			toplevel->xdg_toplevel->base->surface->mapped;
	}
	return false;
}

static struct shady_toplevel *preferred_current_mapped(struct shady_server *server) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	if (state && state->current < state->count) {
		struct shady_toplevel *last = state->last_focused[state->current];
		if (toplevel_live_on_workspace(server, last, state->current)) return last;
		state->last_focused[state->current] = NULL;
	}
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->toplevels, link) {
		if (on_current_workspace(toplevel)) return toplevel;
	}
	return NULL;
}

static void refresh_visibility(struct shady_server *server) {
	struct shady_toplevel *toplevel;
	wl_list_for_each(toplevel, &server->all_toplevels, all_link) {
		if (!toplevel->scene_tree) continue;
		bool visible = on_current_workspace(toplevel);
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
	}
}

bool shady_workspace_switch(struct shady_server *server, const char *name) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	if (!state) return false;
	ssize_t index = ensure_workspace(state, name);
	if (index < 0) return false;
	if (state->current == (size_t)index) return true;

	struct wlr_surface *old_surface = server->seat->keyboard_state.focused_surface;
	if (old_surface) {
		struct wlr_xdg_toplevel *old_xdg =
			wlr_xdg_toplevel_try_from_wlr_surface(old_surface);
		if (old_xdg) wlr_xdg_toplevel_set_activated(old_xdg, false);
	}
	state->current = (size_t)index;
	refresh_visibility(server);
	wlr_seat_keyboard_clear_focus(server->seat);
	wlr_seat_pointer_clear_focus(server->seat);
	struct shady_toplevel *focus = preferred_current_mapped(server);
	if (focus) focus_toplevel(focus);
	shady_event_emit_workspace(server, SHADY_EVENT_WORKSPACE_CHANGED,
		state->names[state->current]);
	shady_render_schedule_all_outputs(server);
	return true;
}

bool shady_workspace_move_toplevel(struct shady_toplevel *toplevel,
		const char *name) {
	if (!toplevel) return false;
	struct shady_workspace_state *state =
		shady_workspace_state_for(toplevel->server);
	struct shady_workspace_toplevel_state *window =
		shady_workspace_toplevel_state_for(toplevel);
	if (!state || !window) return false;
	ssize_t index = ensure_workspace(state, name);
	if (index < 0) return false;

	size_t old_workspace = window->workspace;
	bool was_visible = old_workspace == state->current;
	if (old_workspace < state->count && state->last_focused[old_workspace] == toplevel)
		state->last_focused[old_workspace] = NULL;
	window->workspace = (size_t)index;
	window->assigned = true;
	bool visible = window->workspace == state->current;
	if (toplevel->scene_tree)
		wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);

	if (was_visible && !visible) {
		struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
		if (toplevel->server->seat->keyboard_state.focused_surface == surface) {
			wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, false);
			wlr_seat_keyboard_clear_focus(toplevel->server->seat);
			struct shady_toplevel *next =
				preferred_current_mapped(toplevel->server);
			if (next) focus_toplevel(next);
		}
	}
	shady_render_schedule_all_outputs(toplevel->server);
	return true;
}

void shady_workspace_refocus_current(struct shady_server *server) {
	if (!server) return;
	struct shady_toplevel *focus = preferred_current_mapped(server);
	if (focus) focus_toplevel(focus);
}

void shady_workspace_note_focus(struct shady_toplevel *toplevel) {
	if (!toplevel) return;
	struct shady_workspace_state *state = shady_workspace_state_for(toplevel->server);
	struct shady_workspace_toplevel_state *window =
		shady_workspace_toplevel_state_for(toplevel);
	if (!state || !window || window->workspace >= state->count) return;
	state->last_focused[window->workspace] = toplevel;
}

void shady_workspace_forget_toplevel(struct shady_toplevel *toplevel) {
	if (!toplevel) return;
	struct shady_workspace_state *state = shady_workspace_state_for(toplevel->server);
	if (!state) return;
	for (size_t i = 0; i < state->count; i++) {
		if (state->last_focused[i] == toplevel) state->last_focused[i] = NULL;
	}
}

const char *shady_workspace_current_name(struct shady_server *server) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	return state && state->count > 0 ? state->names[state->current] : "";
}

const char *shady_workspace_toplevel_name(struct shady_toplevel *toplevel) {
	if (!toplevel) return "";
	struct shady_workspace_state *state =
		shady_workspace_state_for(toplevel->server);
	struct shady_workspace_toplevel_state *window =
		shady_workspace_toplevel_state_for(toplevel);
	if (!state || !window || window->workspace >= state->count) return "";
	return state->names[window->workspace];
}

size_t shady_workspace_count(struct shady_server *server) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	return state ? state->count : 0;
}

const char *shady_workspace_name_at(struct shady_server *server, size_t index) {
	struct shady_workspace_state *state = shady_workspace_state_for(server);
	return state && index < state->count ? state->names[index] : "";
}
