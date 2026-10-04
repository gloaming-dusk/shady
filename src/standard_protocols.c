#include "standard_protocols.h"

#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_workspace_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "event/event.h"
#include "modules/desktop/state.h"
#include "modules/workspace/workspace.h"

struct shady_standard_protocols;

/* One mapped window, published through both toplevel protocols. */
struct window_entry {
	struct wl_list link;
	struct shady_standard_protocols *protocols;
	struct shady_toplevel *toplevel;
	struct wlr_foreign_toplevel_handle_v1 *wlr;
	struct wlr_ext_foreign_toplevel_handle_v1 *ext;
	struct wlr_output *output;
	char *title, *app_id;
	struct wl_listener request_activate;
	struct wl_listener request_close;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;
	struct wl_listener request_minimize;
};

struct workspace_entry {
	struct wl_list link;
	char *name;
	struct wlr_ext_workspace_handle_v1 *handle;
};

struct shady_standard_protocols {
	struct shady_server *server;
	struct wlr_foreign_toplevel_manager_v1 *wlr_toplevels;
	struct wlr_ext_foreign_toplevel_list_v1 *ext_toplevels;
	struct wlr_ext_workspace_manager_v1 *workspaces;
	struct wlr_ext_workspace_group_handle_v1 *group;
	struct wl_listener workspace_commit;
	struct wl_list windows; /* window_entry.link */
	struct wl_list workspace_entries; /* workspace_entry.link */
	struct shady_toplevel *focused;
};

static struct shady_standard_protocols *protocols_of(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	return desktop ? desktop->standard_protocols : NULL;
}

static bool same(const char *a, const char *b) {
	return (!a && !b) || (a && b && !strcmp(a, b));
}

static void replace(char **slot, const char *value) {
	free(*slot);
	*slot = value ? strdup(value) : NULL;
}

/* ---- workspaces ------------------------------------------------------- */

static struct workspace_entry *find_workspace(struct shady_standard_protocols *p,
		struct wlr_ext_workspace_handle_v1 *handle, const char *name) {
	struct workspace_entry *w;
	wl_list_for_each(w, &p->workspace_entries, link)
		if ((handle && w->handle == handle) || (name && !strcmp(w->name, name))) return w;
	return NULL;
}

/* Shady creates workspaces on demand (switching to or moving a window to a
 * new name), so the list is reconciled after any change that might do so. */
static void sync_workspaces(struct shady_standard_protocols *p) {
	struct shady_server *server = p->server;
	const char *current = shady_workspace_current_name(server);
	size_t count = shady_workspace_count(server);
	for (size_t i = 0; i < count; i++) {
		const char *name = shady_workspace_name_at(server, i);
		if (!name || !*name) continue;
		struct workspace_entry *w = find_workspace(p, NULL, name);
		if (!w) {
			w = calloc(1, sizeof(*w));
			if (!w || !(w->name = strdup(name))) {
				free(w);
				continue;
			}
			w->handle = wlr_ext_workspace_handle_v1_create(p->workspaces, name,
				EXT_WORKSPACE_HANDLE_V1_WORKSPACE_CAPABILITIES_ACTIVATE);
			if (!w->handle) {
				free(w->name);
				free(w);
				continue;
			}
			w->handle->data = w;
			wlr_ext_workspace_handle_v1_set_name(w->handle, name);
			wlr_ext_workspace_handle_v1_set_group(w->handle, p->group);
			uint32_t coordinate = (uint32_t)i;
			wlr_ext_workspace_handle_v1_set_coordinates(w->handle, &coordinate, 1);
			wl_list_insert(p->workspace_entries.prev, &w->link);
		}
		wlr_ext_workspace_handle_v1_set_active(w->handle, current && !strcmp(current, name));
	}
}

static void workspace_commit(struct wl_listener *listener, void *data) {
	struct shady_standard_protocols *p = wl_container_of(listener, p, workspace_commit);
	struct wlr_ext_workspace_v1_commit_event *event = data;
	struct wlr_ext_workspace_v1_request *request;
	wl_list_for_each(request, event->requests, link) {
		/* Only activation is advertised; anything else is ignored. */
		if (request->type != WLR_EXT_WORKSPACE_V1_REQUEST_ACTIVATE || !request->activate.workspace)
			continue;
		struct workspace_entry *w = find_workspace(p, request->activate.workspace, NULL);
		if (w) shady_workspace_switch(p->server, w->name);
	}
}

/* ---- toplevels -------------------------------------------------------- */

static struct window_entry *find_window(struct shady_standard_protocols *p,
		struct shady_toplevel *toplevel) {
	struct window_entry *w;
	wl_list_for_each(w, &p->windows, link)
		if (w->toplevel == toplevel) return w;
	return NULL;
}

static void request_activate(struct wl_listener *listener, void *data) {
	(void)data;
	struct window_entry *w = wl_container_of(listener, w, request_activate);
	struct shady_toplevel *t = w->toplevel;
	const char *workspace = shady_workspace_toplevel_name(t);
	if (workspace && *workspace &&
			strcmp(workspace, shady_workspace_current_name(t->server)) != 0)
		shady_workspace_switch(t->server, workspace);
	focus_toplevel(t);
}

static void request_close(struct wl_listener *listener, void *data) {
	(void)data;
	struct window_entry *w = wl_container_of(listener, w, request_close);
	wlr_xdg_toplevel_send_close(w->toplevel->xdg_toplevel);
}

static void request_maximize(struct wl_listener *listener, void *data) {
	struct window_entry *w = wl_container_of(listener, w, request_maximize);
	struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
	shady_toplevel_set_maximized(w->toplevel, event->maximized);
}

static void request_fullscreen(struct wl_listener *listener, void *data) {
	struct window_entry *w = wl_container_of(listener, w, request_fullscreen);
	struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
	shady_toplevel_set_fullscreen(w->toplevel, event->fullscreen);
}

/* Shady has no minimized state: keep reporting the window as shown. */
static void request_minimize(struct wl_listener *listener, void *data) {
	(void)data;
	struct window_entry *w = wl_container_of(listener, w, request_minimize);
	wlr_foreign_toplevel_handle_v1_set_minimized(w->wlr, false);
}

static struct wlr_output *window_output(struct shady_toplevel *t) {
	if (!t->scene_tree || !t->xdg_toplevel) return NULL;
	struct wlr_box geo = t->xdg_toplevel->base->geometry;
	double cx = t->scene_tree->node.x + geo.width / 2.0;
	double cy = t->scene_tree->node.y + geo.height / 2.0;
	struct wlr_output *output = wlr_output_layout_output_at(t->server->output_layout, cx, cy);
	if (!output && !wl_list_empty(&t->server->outputs)) {
		struct shady_output *first = wl_container_of(t->server->outputs.next, first, link);
		output = first->wlr_output;
	}
	return output;
}

static void update_window(struct shady_standard_protocols *p, struct window_entry *w) {
	struct shady_toplevel *t = w->toplevel;
	const char *title = t->xdg_toplevel ? t->xdg_toplevel->title : NULL;
	const char *app_id = t->xdg_toplevel ? t->xdg_toplevel->app_id : NULL;
	if (!same(title, w->title) || !same(app_id, w->app_id)) {
		replace(&w->title, title);
		replace(&w->app_id, app_id);
		wlr_foreign_toplevel_handle_v1_set_title(w->wlr, title ? title : "");
		wlr_foreign_toplevel_handle_v1_set_app_id(w->wlr, app_id ? app_id : "");
		const struct wlr_ext_foreign_toplevel_handle_v1_state state = {
			.title = title ? title : "", .app_id = app_id ? app_id : "",
		};
		wlr_ext_foreign_toplevel_handle_v1_update_state(w->ext, &state);
	}
	struct wlr_output *output = window_output(t);
	if (output != w->output) {
		if (w->output) wlr_foreign_toplevel_handle_v1_output_leave(w->wlr, w->output);
		if (output) wlr_foreign_toplevel_handle_v1_output_enter(w->wlr, output);
		w->output = output;
	}
	/* The setters only notify clients when the state really changes. */
	wlr_foreign_toplevel_handle_v1_set_maximized(w->wlr, t->maximized);
	wlr_foreign_toplevel_handle_v1_set_fullscreen(w->wlr, t->fullscreen);
	wlr_foreign_toplevel_handle_v1_set_activated(w->wlr, p->focused == t);
}

static void add_window(struct shady_standard_protocols *p, struct shady_toplevel *t) {
	if (find_window(p, t)) return;
	struct window_entry *w = calloc(1, sizeof(*w));
	if (!w) return;
	w->protocols = p;
	w->toplevel = t;
	w->wlr = wlr_foreign_toplevel_handle_v1_create(p->wlr_toplevels);
	const struct wlr_ext_foreign_toplevel_handle_v1_state state = {
		.title = t->xdg_toplevel && t->xdg_toplevel->title ? t->xdg_toplevel->title : "",
		.app_id = t->xdg_toplevel && t->xdg_toplevel->app_id ? t->xdg_toplevel->app_id : "",
	};
	w->ext = wlr_ext_foreign_toplevel_handle_v1_create(p->ext_toplevels, &state);
	if (!w->wlr || !w->ext) {
		if (w->wlr) wlr_foreign_toplevel_handle_v1_destroy(w->wlr);
		if (w->ext) wlr_ext_foreign_toplevel_handle_v1_destroy(w->ext);
		free(w);
		return;
	}
	w->request_activate.notify = request_activate;
	wl_signal_add(&w->wlr->events.request_activate, &w->request_activate);
	w->request_close.notify = request_close;
	wl_signal_add(&w->wlr->events.request_close, &w->request_close);
	w->request_maximize.notify = request_maximize;
	wl_signal_add(&w->wlr->events.request_maximize, &w->request_maximize);
	w->request_fullscreen.notify = request_fullscreen;
	wl_signal_add(&w->wlr->events.request_fullscreen, &w->request_fullscreen);
	w->request_minimize.notify = request_minimize;
	wl_signal_add(&w->wlr->events.request_minimize, &w->request_minimize);
	wl_list_insert(p->windows.prev, &w->link);
	update_window(p, w);
}

static void remove_window(struct window_entry *w) {
	wl_list_remove(&w->request_activate.link);
	wl_list_remove(&w->request_close.link);
	wl_list_remove(&w->request_maximize.link);
	wl_list_remove(&w->request_fullscreen.link);
	wl_list_remove(&w->request_minimize.link);
	wlr_foreign_toplevel_handle_v1_destroy(w->wlr);
	wlr_ext_foreign_toplevel_handle_v1_destroy(w->ext);
	wl_list_remove(&w->link);
	free(w->title);
	free(w->app_id);
	free(w);
}

/* ---- events ----------------------------------------------------------- */

static void on_event(shady_host host, const struct shady_event *event, void *user_data) {
	(void)host;
	struct shady_standard_protocols *p = user_data;
	struct shady_toplevel *t = (struct shady_toplevel *)event->object.window;
	switch (event->type) {
	case SHADY_EVENT_WINDOW_MAPPED:
		add_window(p, t);
		break;
	case SHADY_EVENT_WINDOW_UNMAPPED:
	case SHADY_EVENT_WINDOW_DESTROYED: {
		if (p->focused == t) p->focused = NULL;
		struct window_entry *w = find_window(p, t);
		if (w) remove_window(w);
		break;
	}
	case SHADY_EVENT_WINDOW_FOCUSED: {
		struct shady_toplevel *previous = p->focused;
		p->focused = t;
		struct window_entry *w = previous ? find_window(p, previous) : NULL;
		if (w) update_window(p, w);
		if ((w = find_window(p, t))) update_window(p, w);
		break;
	}
	case SHADY_EVENT_WINDOW_RESIZED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
	case SHADY_EVENT_WINDOW_TITLE_CHANGED: {
		struct window_entry *w = find_window(p, t);
		if (w) update_window(p, w);
		break;
	}
	case SHADY_EVENT_OUTPUT_ADDED: {
		struct shady_output *output = (struct shady_output *)event->object.output;
		wlr_ext_workspace_group_handle_v1_output_enter(p->group, output->wlr_output);
		break;
	}
	case SHADY_EVENT_OUTPUT_REMOVED: {
		struct shady_output *output = (struct shady_output *)event->object.output;
		wlr_ext_workspace_group_handle_v1_output_leave(p->group, output->wlr_output);
		struct window_entry *w;
		wl_list_for_each(w, &p->windows, link) {
			if (w->output != output->wlr_output) continue;
			wlr_foreign_toplevel_handle_v1_output_leave(w->wlr, w->output);
			w->output = NULL;
		}
		break;
	}
	default:
		break;
	}
	sync_workspaces(p);
}

bool shady_standard_protocols_init(struct shady_server *server) {
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (!desktop) return false;
	struct shady_standard_protocols *p = calloc(1, sizeof(*p));
	if (!p) return false;
	p->server = server;
	wl_list_init(&p->windows);
	wl_list_init(&p->workspace_entries);
	desktop->standard_protocols = p;

	p->wlr_toplevels = wlr_foreign_toplevel_manager_v1_create(server->wl_display);
	p->ext_toplevels = wlr_ext_foreign_toplevel_list_v1_create(server->wl_display, 1);
	p->workspaces = wlr_ext_workspace_manager_v1_create(server->wl_display, 1);
	p->group = p->workspaces ? wlr_ext_workspace_group_handle_v1_create(p->workspaces, 0) : NULL;
	if (!p->wlr_toplevels || !p->ext_toplevels || !p->group) {
		wlr_log(WLR_ERROR, "standard protocols: failed to create globals");
		shady_standard_protocols_finish(server);
		return false;
	}
	p->workspace_commit.notify = workspace_commit;
	wl_signal_add(&p->workspaces->events.commit, &p->workspace_commit);

	static const uint32_t events[] = {
		SHADY_EVENT_WINDOW_MAPPED, SHADY_EVENT_WINDOW_UNMAPPED, SHADY_EVENT_WINDOW_DESTROYED,
		SHADY_EVENT_WINDOW_FOCUSED, SHADY_EVENT_WINDOW_RESIZED, SHADY_EVENT_WINDOW_STATE_CHANGED,
		SHADY_EVENT_WINDOW_TITLE_CHANGED, SHADY_EVENT_WORKSPACE_CHANGED,
		SHADY_EVENT_OUTPUT_ADDED, SHADY_EVENT_OUTPUT_REMOVED,
	};
	for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
		if (!shady_event_subscribe_owned(server, events[i], on_event, p, p)) {
			shady_standard_protocols_finish(server);
			return false;
		}
	}
	/* Outputs that already exist; later ones arrive as events. */
	struct shady_output *output;
	wl_list_for_each(output, &server->outputs, link)
		wlr_ext_workspace_group_handle_v1_output_enter(p->group, output->wlr_output);
	sync_workspaces(p);
	return true;
}

void shady_standard_protocols_finish(struct shady_server *server) {
	struct shady_standard_protocols *p = protocols_of(server);
	if (!p) return;
	shady_event_unsubscribe_owner(server, p);
	struct window_entry *w, *wtmp;
	wl_list_for_each_safe(w, wtmp, &p->windows, link) remove_window(w);
	struct workspace_entry *ws, *wstmp;
	wl_list_for_each_safe(ws, wstmp, &p->workspace_entries, link) {
		wlr_ext_workspace_handle_v1_destroy(ws->handle);
		wl_list_remove(&ws->link);
		free(ws->name);
		free(ws);
	}
	if (p->workspace_commit.link.prev) wl_list_remove(&p->workspace_commit.link);
	if (p->group) wlr_ext_workspace_group_handle_v1_destroy(p->group);
	/* The globals belong to the display and go away with it. */
	free(p);
	shady_desktop_state(server)->standard_protocols = NULL;
}
