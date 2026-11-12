#define _GNU_SOURCE
#include "../automation.h"
#include "../capture.h"
#include "ipc.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "../shady.h"
#include "../event/event.h"
#include "../module/module.h"
#include "../modules/workspace/workspace.h"
#include "../plugin/manager.h"
#include "../plugin/plugin.h"
#include "json.h"
#include "shortcuts.h"
#include "../shortcut_overlay.h"
#include "values.h"
#if SHADY_HAS_LUA
#include "../modules/lua/lua.h"
#endif

/* A request line longer than this is refused and the client dropped. */
#define IPC_LINE_MAX (64 * 1024)
/* A subscriber that stops reading is dropped once this much output queues. */
#define IPC_OUT_MAX (4 * 1024 * 1024)
/* Subscription bit for published values, after the compositor events. */
#define IPC_VALUE_EVENT SHADY_EVENT_COUNT
#define IPC_VALUE_EVENT_NAME "value.changed"
/* Subscription bit for the shortcut overlay's toggle and list. */
#define IPC_SHORTCUTS_EVENT (SHADY_EVENT_COUNT + 1)
#define IPC_SHORTCUTS_EVENT_NAME "shortcuts.toggle"
#define IPC_LAST_EVENT IPC_SHORTCUTS_EVENT
#define IPC_ALL_EVENTS ((1u << (IPC_LAST_EVENT + 1)) - 1)

struct shady_ipc {
	struct shady_server *server;
	int listen_fd;
	struct wl_event_source *listen_source;
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	struct wl_list clients;
	struct shady_toplevel *focused;
	struct wl_event_source *reap_source; /* pending idle reap */
	struct wl_list pending; /* ipc_pending.link: requests answered later */
};

struct ipc_client {
	struct wl_list link;
	struct shady_ipc *ipc;
	int fd;
	struct wl_event_source *source;
	char in[IPC_LINE_MAX];
	size_t in_len;
	struct json_buf out;
	uint32_t subscriptions; /* bit per enum shady_event_type */
	bool dead;
	/* A request answered later (a screenshot, a drag, a wait): the lines
	 * after it wait for its answer, so answers stay in order. */
	struct ipc_pending *waiting;
	struct wl_event_source *resume; /* idle: go on with the lines after it */
};

/* A request whose answer comes once something has happened. */
struct ipc_pending {
	struct wl_list link; /* shady_ipc.pending */
	struct ipc_client *client;
	struct json_buf id; /* the response's "id":…, prefix, or empty */
	struct shady_automation_op *op;
	bool capture; /* waiting for the screenshot */
};

static void process_input(struct ipc_client *client);
static void cancel_pending(struct ipc_pending *pending);

static struct shady_ipc *ipc_of(struct shady_server *server) {
	return server->ipc;
}

/* ---- client lifetime -------------------------------------------------- */

static void reap(void *data) {
	struct shady_ipc *ipc = data;
	ipc->reap_source = NULL;
	struct ipc_client *client, *tmp;
	wl_list_for_each_safe(client, tmp, &ipc->clients, link) {
		if (!client->dead) continue;
		wl_list_remove(&client->link);
		if (client->resume) wl_event_source_remove(client->resume);
		if (client->source) wl_event_source_remove(client->source);
		close(client->fd);
		json_buf_free(&client->out);
		free(client);
	}
}

/* Clients are freed from an idle callback, never in the middle of event
 * dispatch or request handling that may still reference them. */
static void kill_client(struct ipc_client *client) {
	if (client->dead) return;
	client->dead = true;
	struct shady_ipc *ipc = client->ipc;
	/* Nobody is left to answer: a drag lets go, a wait just stops. */
	struct ipc_pending *pending, *tmp;
	wl_list_for_each_safe(pending, tmp, &ipc->pending, link)
		if (pending->client == client) cancel_pending(pending);
	client->waiting = NULL;
	if (!ipc->reap_source) {
		struct wl_event_loop *loop = wl_display_get_event_loop(ipc->server->wl_display);
		ipc->reap_source = wl_event_loop_add_idle(loop, reap, ipc);
	}
}

static void flush(struct ipc_client *client) {
	while (!client->dead && client->out.len > 0) {
		ssize_t n = send(client->fd, client->out.data, client->out.len, MSG_NOSIGNAL);
		if (n > 0) {
			json_consume(&client->out, (size_t)n);
			continue;
		}
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
		kill_client(client);
		return;
	}
	if (client->dead) return;
	if (client->out.len > IPC_OUT_MAX || client->out.failed) {
		wlr_log(WLR_INFO, "ipc: dropping a client that stopped reading");
		kill_client(client);
		return;
	}
	wl_event_source_fd_update(client->source,
		(client->waiting ? 0 : WL_EVENT_READABLE) | (client->out.len ? WL_EVENT_WRITABLE : 0));
}

static void send_line(struct ipc_client *client, const struct json_buf *line) {
	if (client->dead || line->failed) return;
	json_append(&client->out, line->data, line->len);
	json_append(&client->out, "\n", 1);
	flush(client);
}

/* ---- JSON views of compositor objects --------------------------------- */

static const char *toplevel_app_id(struct shady_toplevel *t) {
	return t->xdg_toplevel && t->xdg_toplevel->app_id ? t->xdg_toplevel->app_id : "";
}

static const char *toplevel_title(struct shady_toplevel *t) {
	return t->xdg_toplevel && t->xdg_toplevel->title ? t->xdg_toplevel->title : "";
}

static void write_window(struct json_buf *b, struct shady_ipc *ipc, struct shady_toplevel *t) {
	const char *workspace = shady_workspace_toplevel_name(t);
	int x = t->scene_tree ? t->scene_tree->node.x : 0;
	int y = t->scene_tree ? t->scene_tree->node.y : 0;
	int w = 0, h = 0;
	if (t->xdg_toplevel && t->xdg_toplevel->base) {
		w = t->xdg_toplevel->base->geometry.width;
		h = t->xdg_toplevel->base->geometry.height;
	}
	json_printf(b, "{\"id\":%u,\"app_id\":", t->shell_id);
	json_string(b, toplevel_app_id(t));
	json_puts(b, ",\"title\":");
	json_string(b, toplevel_title(t));
	json_puts(b, ",\"workspace\":");
	json_string(b, workspace ? workspace : "");
	json_printf(b, ",\"focused\":%s,\"mapped\":%s,\"maximized\":%s,\"fullscreen\":%s,"
		"\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
		ipc->focused == t ? "true" : "false",
		t->mapped ? "true" : "false",
		t->maximized ? "true" : "false",
		t->fullscreen ? "true" : "false",
		x, y, w, h);
}

static void write_output(struct json_buf *b, struct shady_output *o) {
	struct wlr_output *out = o->wlr_output;
	json_puts(b, "{\"name\":");
	json_string(b, out ? out->name : "");
	json_printf(b, ",\"width\":%d,\"height\":%d,\"scale\":%g,\"enabled\":%s}",
		out ? out->width : 0, out ? out->height : 0,
		out ? (double)out->scale : 1.0,
		out && out->enabled ? "true" : "false");
}

static void write_workspaces(struct json_buf *b, struct shady_server *server) {
	json_puts(b, "{\"current\":");
	json_string(b, shady_workspace_current_name(server));
	json_puts(b, ",\"list\":[");
	size_t count = shady_workspace_count(server);
	for (size_t i = 0; i < count; i++) {
		if (i) json_puts(b, ",");
		json_string(b, shady_workspace_name_at(server, i));
	}
	json_puts(b, "]}");
}

static struct shady_toplevel *find_window(struct shady_server *server, uint32_t id) {
	struct shady_toplevel *t;
	wl_list_for_each(t, &server->all_toplevels, all_link)
		if (t->shell_id == id) return t;
	return NULL;
}

/* ---- events ----------------------------------------------------------- */

static void on_event(shady_host host, const struct shady_event *event, void *user_data) {
	(void)host;
	struct shady_ipc *ipc = user_data;
	if (!event || event->type >= SHADY_EVENT_COUNT) return;

	struct shady_toplevel *window = NULL;
	switch (event->type) {
	case SHADY_EVENT_WINDOW_CREATED:
	case SHADY_EVENT_WINDOW_MAPPED:
	case SHADY_EVENT_WINDOW_UNMAPPED:
	case SHADY_EVENT_WINDOW_FOCUSED:
	case SHADY_EVENT_WINDOW_RESIZED:
	case SHADY_EVENT_WINDOW_STATE_CHANGED:
	case SHADY_EVENT_WINDOW_TITLE_CHANGED:
	case SHADY_EVENT_WINDOW_DESTROYED:
		window = (struct shady_toplevel *)event->object.window;
		break;
	default:
		break;
	}
	if (event->type == SHADY_EVENT_WINDOW_FOCUSED) ipc->focused = window;
	else if ((event->type == SHADY_EVENT_WINDOW_UNMAPPED ||
			event->type == SHADY_EVENT_WINDOW_DESTROYED) && ipc->focused == window)
		ipc->focused = NULL;

	uint32_t bit = 1u << event->type;
	bool wanted = false;
	struct ipc_client *client;
	wl_list_for_each(client, &ipc->clients, link)
		wanted |= !client->dead && (client->subscriptions & bit);
	if (!wanted) return;

	struct json_buf line = {0};
	json_puts(&line, "{\"event\":");
	json_string(&line, shady_event_name(event->type));
	if (window) {
		json_puts(&line, ",\"window\":");
		/* A window on its way out is described by its id alone. */
		if (event->type == SHADY_EVENT_WINDOW_UNMAPPED ||
				event->type == SHADY_EVENT_WINDOW_DESTROYED)
			json_printf(&line, "{\"id\":%u}", window->shell_id);
		else
			write_window(&line, ipc, window);
	} else if (event->type == SHADY_EVENT_WORKSPACE_CHANGED) {
		json_puts(&line, ",\"workspace\":");
		json_string(&line, event->object.workspace);
	} else if (event->type == SHADY_EVENT_OUTPUT_ADDED ||
			event->type == SHADY_EVENT_OUTPUT_REMOVED) {
		json_puts(&line, ",\"output\":");
		write_output(&line, (struct shady_output *)event->object.output);
	} else if (event->type == SHADY_EVENT_MODULE_STARTED ||
			event->type == SHADY_EVENT_MODULE_STOPPED) {
		const struct shady_module *module = (const struct shady_module *)event->object.module;
		json_puts(&line, ",\"module\":");
		json_string(&line, module ? module->name : "");
	}
	json_puts(&line, "}");

	wl_list_for_each(client, &ipc->clients, link)
		if (client->subscriptions & bit) send_line(client, &line);
	json_buf_free(&line);
}

static int event_type_from_name(const char *name) {
	for (int type = 0; type < SHADY_EVENT_COUNT; type++)
		if (!strcmp(shady_event_name(type), name)) return type;
	if (!strcmp(name, IPC_VALUE_EVENT_NAME)) return IPC_VALUE_EVENT;
	if (!strcmp(name, IPC_SHORTCUTS_EVENT_NAME)) return IPC_SHORTCUTS_EVENT;
	return -1;
}

static const char *event_name(int type) {
	if (type == IPC_VALUE_EVENT) return IPC_VALUE_EVENT_NAME;
	if (type == IPC_SHORTCUTS_EVENT) return IPC_SHORTCUTS_EVENT_NAME;
	return shady_event_name(type);
}

static void write_value_event(struct json_buf *line, const char *key, const char *value) {
	json_puts(line, "{\"event\":\"" IPC_VALUE_EVENT_NAME "\",\"key\":");
	json_string(line, key);
	json_puts(line, ",\"value\":");
	if (value) json_string(line, value);
	else json_puts(line, "null");
	json_puts(line, "}");
}

void shady_ipc_value_changed(struct shady_server *server, const char *key, const char *value) {
	struct shady_ipc *ipc = server ? server->ipc : NULL;
	if (!ipc) return;
	struct json_buf line = {0};
	write_value_event(&line, key, value);
	struct ipc_client *client;
	wl_list_for_each(client, &ipc->clients, link)
		if (client->subscriptions & (1u << IPC_VALUE_EVENT)) send_line(client, &line);
	json_buf_free(&line);
}

static void write_shortcut_fields(struct json_buf *b, const struct shady_shortcut *shortcut) {
	json_puts(b, "\"keys\":");
	json_string(b, shortcut->keys);
	json_puts(b, ",\"group\":");
	json_string(b, shortcut->group);
	json_puts(b, ",\"description\":");
	json_string(b, shortcut->description);
}

static void count_shortcut(const struct shady_shortcut *shortcut, void *data) {
	(void)shortcut;
	(*(size_t *)data)++;
}

static void write_shortcut_event(const struct shady_shortcut *shortcut, void *data) {
	struct json_buf *b = data;
	json_puts(b, "{\"event\":\"shortcut\",");
	write_shortcut_fields(b, shortcut);
	json_puts(b, "}\n");
}

bool shady_ipc_shortcuts_toggle(struct shady_server *server) {
	struct shady_ipc *ipc = server ? server->ipc : NULL;
	if (!ipc) return false;
	bool wanted = false;
	struct ipc_client *client;
	wl_list_for_each(client, &ipc->clients, link)
		wanted |= !client->dead && (client->subscriptions & (1u << IPC_SHORTCUTS_EVENT));
	if (!wanted) return false;
	/* The list follows the toggle, one line per shortcut, so a client
	 * whose JSON stays flat can still read it. */
	size_t count = 0;
	shady_shortcuts_for_each(server, count_shortcut, &count);
	struct json_buf line = {0};
	json_printf(&line, "{\"event\":\"" IPC_SHORTCUTS_EVENT_NAME "\",\"count\":%zu}\n", count);
	shady_shortcuts_for_each(server, write_shortcut_event, &line);
	/* send_line adds the final newline. */
	if (line.len) line.len--;
	wl_list_for_each(client, &ipc->clients, link)
		if (client->subscriptions & (1u << IPC_SHORTCUTS_EVENT)) send_line(client, &line);
	json_buf_free(&line);
	return true;
}

static void replay_value(const char *key, const char *value, void *data) {
	struct ipc_client *client = data;
	struct json_buf line = {0};
	write_value_event(&line, key, value);
	send_line(client, &line);
	json_buf_free(&line);
}

/* ---- commands --------------------------------------------------------- */

struct request {
	struct shady_ipc *ipc;
	struct ipc_client *client;
	const struct json_object *args;
	struct json_buf *data; /* the response's "data" value */
	const char *error;
	char lua_error[4096];
	char message[256]; /* room for an error naming an argument */
	bool replay_values; /* send current values after the response */
	const struct json_value *id;
	struct ipc_pending *pending; /* set by defer(): answered later */
};

static bool error(struct request *r, const char *message) {
	r->error = message;
	return false;
}

static void write_id(struct json_buf *b, const struct json_value *id) {
	if (!id) return;
	if (id->type == JSON_NUMBER) {
		json_puts(b, "\"id\":");
		json_append(b, id->raw, id->raw_len);
		json_puts(b, ",");
	} else if (id->type == JSON_STRING) {
		json_puts(b, "\"id\":");
		json_string(b, id->string);
		json_puts(b, ",");
	}
}

static void write_response(struct json_buf *b, const struct json_buf *id, bool ok,
		const struct json_buf *data, const char *message) {
	json_puts(b, "{");
	if (id && id->len) json_append(b, id->data, id->len);
	if (ok) {
		json_puts(b, "\"ok\":true");
		if (data && data->len) {
			json_puts(b, ",\"data\":");
			json_append(b, data->data, data->len);
		}
	} else {
		json_puts(b, "\"ok\":false,\"error\":");
		json_string(b, message ? message : "failed");
	}
	json_puts(b, "}");
}

/* ---- requests answered later ------------------------------------------ */

/* The command answers later: once it has, the client's next lines run. */
static struct ipc_pending *defer(struct request *r) {
	struct ipc_pending *pending = calloc(1, sizeof(*pending));
	if (!pending) {
		error(r, "out of memory");
		return NULL;
	}
	pending->client = r->client;
	write_id(&pending->id, r->id);
	wl_list_insert(&r->ipc->pending, &pending->link);
	r->pending = pending;
	return pending;
}

static void pending_free(struct ipc_pending *pending) {
	wl_list_remove(&pending->link);
	json_buf_free(&pending->id);
	free(pending);
}

static void cancel_pending(struct ipc_pending *pending) {
	if (pending->op) shady_automation_op_cancel(pending->op);
	if (pending->capture) shady_capture_forget(pending);
	pending_free(pending);
}

static void resume_client(void *data) {
	struct ipc_client *client = data;
	client->resume = NULL;
	if (client->dead || client->waiting) return;
	process_input(client);
	if (!client->dead) flush(client);
}

static void pending_reply(struct ipc_pending *pending, bool ok,
		const struct json_buf *data, const char *message) {
	/* Out of the list first: sending may drop the client, which cancels
	 * what it still waits for. */
	wl_list_remove(&pending->link);
	wl_list_init(&pending->link);
	struct ipc_client *client = pending->client;
	if (client && !client->dead && client->waiting == pending) {
		client->waiting = NULL;
		struct json_buf response = {0};
		write_response(&response, &pending->id, ok, data, message);
		send_line(client, &response);
		json_buf_free(&response);
		if (!client->dead && !client->resume) {
			struct wl_event_loop *loop =
				wl_display_get_event_loop(client->ipc->server->wl_display);
			client->resume = wl_event_loop_add_idle(loop, resume_client, client);
		}
	}
	pending_free(pending);
}

static struct shady_toplevel *request_window(struct request *r) {
	const struct json_value *id = json_get(r->args, "window");
	if (!id || id->type != JSON_NUMBER || id->number < 1 || id->number > UINT32_MAX) {
		error(r, "missing or invalid \"window\" id");
		return NULL;
	}
	struct shady_toplevel *t = find_window(r->ipc->server, (uint32_t)id->number);
	if (!t || !t->mapped) {
		error(r, "no such window");
		return NULL;
	}
	return t;
}

static const char *request_string(struct request *r, const char *key) {
	const char *value = json_get_string(r->args, key);
	if (!value || !*value) error(r, "missing string argument");
	return value && *value ? value : NULL;
}

/* "state": true/false sets; absent toggles. */
static bool request_state(struct request *r, bool current) {
	const struct json_value *v = json_get(r->args, "state");
	return v && v->type == JSON_BOOL ? v->boolean : !current;
}

static bool cmd_version(struct request *r) {
	json_printf(r->data, "{\"protocol\":%d,\"compositor\":\"shady\"}",
		SHADY_IPC_PROTOCOL_VERSION);
	return true;
}

static bool cmd_windows(struct request *r) {
	json_puts(r->data, "[");
	bool first = true;
	struct shady_toplevel *t;
	wl_list_for_each(t, &r->ipc->server->all_toplevels, all_link) {
		if (!t->mapped) continue;
		if (!first) json_puts(r->data, ",");
		first = false;
		write_window(r->data, r->ipc, t);
	}
	json_puts(r->data, "]");
	return true;
}

static bool cmd_focused(struct request *r) {
	if (r->ipc->focused && r->ipc->focused->mapped) write_window(r->data, r->ipc, r->ipc->focused);
	else json_puts(r->data, "null");
	return true;
}

static bool cmd_workspaces(struct request *r) {
	write_workspaces(r->data, r->ipc->server);
	return true;
}

static bool cmd_outputs(struct request *r) {
	json_puts(r->data, "[");
	bool first = true;
	struct shady_output *o;
	wl_list_for_each(o, &r->ipc->server->outputs, link) {
		if (!first) json_puts(r->data, ",");
		first = false;
		write_output(r->data, o);
	}
	json_puts(r->data, "]");
	return true;
}

static bool cmd_plugins(struct request *r) {
	struct shady_server *server = r->ipc->server;
	struct shady_plugin_manager *pm = &server->plugins;
	json_puts(r->data, "[");
	for (size_t i = 0; i < pm->record_count; i++) {
		const struct shady_plugin_record *rec = &pm->records[i];
		if (i) json_puts(r->data, ",");
		json_puts(r->data, "{\"name\":");
		json_string(r->data, rec->module_name ? rec->module_name : rec->spec);
		json_puts(r->data, ",\"spec\":");
		json_string(r->data, rec->spec);
		json_puts(r->data, ",\"state\":");
		json_string(r->data, shady_plugin_status_name(shady_plugin_manager_status(server, rec)));
		json_printf(r->data, ",\"builtin\":%s}", rec->builtin ? "true" : "false");
	}
	json_puts(r->data, "]");
	return true;
}

static bool cmd_window_focus(struct request *r) {
	struct shady_toplevel *t = request_window(r);
	if (!t) return false;
	const char *workspace = shady_workspace_toplevel_name(t);
	if (workspace && *workspace &&
			strcmp(workspace, shady_workspace_current_name(t->server)) != 0)
		shady_workspace_switch(t->server, workspace);
	focus_toplevel(t);
	return true;
}

static bool cmd_window_close(struct request *r) {
	struct shady_toplevel *t = request_window(r);
	if (!t) return false;
	wlr_xdg_toplevel_send_close(t->xdg_toplevel);
	return true;
}

static bool cmd_window_maximize(struct request *r) {
	struct shady_toplevel *t = request_window(r);
	if (!t) return false;
	shady_toplevel_set_maximized(t, request_state(r, t->maximized));
	return true;
}

static bool cmd_window_fullscreen(struct request *r) {
	struct shady_toplevel *t = request_window(r);
	if (!t) return false;
	shady_toplevel_set_fullscreen(t, request_state(r, t->fullscreen));
	return true;
}

static bool cmd_window_move(struct request *r) {
	struct shady_toplevel *t = request_window(r);
	if (!t) return false;
	const char *workspace = request_string(r, "workspace");
	if (!workspace) return false;
	return shady_workspace_move_toplevel(t, workspace) || error(r, "move failed");
}

static bool cmd_workspace_switch(struct request *r) {
	const char *name = request_string(r, "name");
	if (!name) return false;
	return shady_workspace_switch(r->ipc->server, name) || error(r, "switch failed");
}

static uint32_t modifier_from_name(const char *s) {
	if (!strcasecmp(s, "Shift")) return WLR_MODIFIER_SHIFT;
	if (!strcasecmp(s, "Ctrl") || !strcasecmp(s, "Control")) return WLR_MODIFIER_CTRL;
	if (!strcasecmp(s, "Alt")) return WLR_MODIFIER_ALT;
	if (!strcasecmp(s, "Super") || !strcasecmp(s, "Logo")) return WLR_MODIFIER_LOGO;
	return 0;
}

/* "Super+Shift+z": the same shortcut syntax as shady.bind. */
static bool request_keys(struct request *r, xkb_keysym_t *sym, uint32_t *mods) {
	const char *spec = request_string(r, "keys");
	if (!spec) return false;
	char buf[128];
	if (strlen(spec) >= sizeof(buf)) return error(r, "shortcut too long");
	strcpy(buf, spec);
	*mods = 0;
	const char *key = NULL;
	char *save = NULL;
	for (char *tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
		uint32_t m = modifier_from_name(tok);
		if (m) *mods |= m;
		else if (key) return error(r, "shortcut has more than one key");
		else key = tok;
	}
	if (!key) {
		/* A modifier on its own is its key: "Shift" is Shift_L. */
		static const struct { uint32_t mod; xkb_keysym_t sym; } alone[] = {
			{ WLR_MODIFIER_SHIFT, XKB_KEY_Shift_L }, { WLR_MODIFIER_CTRL, XKB_KEY_Control_L },
			{ WLR_MODIFIER_ALT, XKB_KEY_Alt_L }, { WLR_MODIFIER_LOGO, XKB_KEY_Super_L },
		};
		for (size_t i = 0; i < sizeof(alone) / sizeof(alone[0]); i++) {
			if (*mods != alone[i].mod) continue;
			*sym = alone[i].sym;
			*mods = 0;
			return true;
		}
		return error(r, "shortcut has no key");
	}
	*sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	if (*sym == XKB_KEY_NoSymbol) return error(r, "unknown key");
	return true;
}

static void write_key_result(struct request *r, const struct shady_automation_key_result *k) {
	json_printf(r->data, "{\"handled\":%s,\"sent\":%s}",
		k->handled ? "true" : "false", k->sent ? "true" : "false");
}

/* The key goes through module hooks, plugins and bindings, exactly like a
 * real press; one nothing takes is pressed for the focused window. */
static bool cmd_key(struct request *r) {
	xkb_keysym_t sym;
	uint32_t mods;
	if (!request_keys(r, &sym, &mods)) return false;
	struct shady_automation_key_result k;
	shady_automation_key(r->ipc->server, sym, mods, &k);
	write_key_result(r, &k);
	return true;
}

static bool key_state(struct request *r, bool pressed) {
	xkb_keysym_t sym;
	uint32_t mods;
	if (!request_keys(r, &sym, &mods)) return false;
	struct shady_automation_key_result k;
	shady_automation_key_state(r->ipc->server, sym, mods, pressed, &k);
	write_key_result(r, &k);
	return true;
}

static bool cmd_key_down(struct request *r) { return key_state(r, true); }
static bool cmd_key_up(struct request *r) { return key_state(r, false); }

static bool cmd_type(struct request *r) {
	const char *text = json_get_string(r->args, "text");
	if (!text) return error(r, "missing string argument: text");
	const char *message = NULL;
	int typed = shady_automation_type(r->ipc->server, text, &message);
	if (typed < 0) return error(r, message);
	json_printf(r->data, "{\"typed\":%d}", typed);
	return true;
}

/* ---- pointer ---------------------------------------------------------- */

#define COORD_MAX 1e6

static bool has_arg(struct request *r, const char *key) {
	const struct json_value *v = json_get(r->args, key);
	return v && v->type != JSON_NULL;
}

/* A number from lo to hi; absent and optional leaves *out as it is. */
static bool arg_number(struct request *r, const char *key, bool required,
		double lo, double hi, double *out) {
	const struct json_value *v = json_get(r->args, key);
	if (!v || v->type == JSON_NULL) {
		if (!required) return true;
		snprintf(r->message, sizeof(r->message), "missing number argument: %s", key);
		return error(r, r->message);
	}
	if (v->type != JSON_NUMBER || !isfinite(v->number) || v->number < lo || v->number > hi) {
		snprintf(r->message, sizeof(r->message), "%s must be a number from %g to %g",
			key, lo, hi);
		return error(r, r->message);
	}
	*out = v->number;
	return true;
}

static bool arg_int(struct request *r, const char *key, int lo, int hi, int *out) {
	double v = *out;
	if (!arg_number(r, key, false, lo, hi, &v)) return false;
	if (v != floor(v)) {
		snprintf(r->message, sizeof(r->message), "%s must be a whole number", key);
		return error(r, r->message);
	}
	*out = (int)v;
	return true;
}

/* "x" and "y" both, or neither: then *given is false. */
static bool arg_point(struct request *r, const char *xkey, const char *ykey,
		bool required, double *x, double *y, bool *given) {
	bool has_x = has_arg(r, xkey), has_y = has_arg(r, ykey);
	if (given) *given = has_x && has_y;
	if (has_x != has_y || (required && !has_x)) {
		snprintf(r->message, sizeof(r->message), "give both %s and %s", xkey, ykey);
		return error(r, r->message);
	}
	return arg_number(r, xkey, false, -COORD_MAX, COORD_MAX, x) &&
		arg_number(r, ykey, false, -COORD_MAX, COORD_MAX, y);
}

/* "button": a name such as "left", or a Linux button code; default left. */
static bool arg_button(struct request *r, uint32_t *button) {
	*button = BTN_LEFT;
	const struct json_value *v = json_get(r->args, "button");
	if (!v || v->type == JSON_NULL) return true;
	if (v->type == JSON_STRING) {
		if (shady_automation_button_from_name(v->string, button)) return true;
		return error(r, "unknown button");
	}
	if (v->type == JSON_NUMBER && v->number >= BTN_MISC && v->number <= KEY_MAX &&
			v->number == floor(v->number)) {
		*button = (uint32_t)v->number;
		return true;
	}
	return error(r, "button must be a name or a button code");
}

static bool cmd_pointer(struct request *r) {
	struct shady_server *server = r->ipc->server;
	struct shady_toplevel *t = shady_input_toplevel_under_cursor(server);
	json_printf(r->data, "{\"x\":%g,\"y\":%g,\"window\":", server->cursor->x, server->cursor->y);
	if (t && t->mapped) json_printf(r->data, "%u", t->shell_id);
	else json_puts(r->data, "null");
	json_puts(r->data, "}");
	return true;
}

static bool cmd_pointer_move(struct request *r) {
	double x = 0, y = 0;
	if (!arg_point(r, "x", "y", true, &x, &y, NULL)) return false;
	shady_automation_pointer_move(r->ipc->server, x, y);
	return true;
}

/* Optional "x"/"y": move there first. */
static bool move_first(struct request *r) {
	double x = 0, y = 0;
	bool given = false;
	if (!arg_point(r, "x", "y", false, &x, &y, &given)) return false;
	if (given) shady_automation_pointer_move(r->ipc->server, x, y);
	return true;
}

static bool cmd_pointer_click(struct request *r) {
	uint32_t button;
	int count = 1;
	if (!arg_button(r, &button) || !arg_int(r, "count", 1, 3, &count)) return false;
	if (!move_first(r)) return false;
	shady_automation_click(r->ipc->server, button, count);
	return true;
}

static bool pointer_button(struct request *r, bool pressed) {
	uint32_t button;
	if (!arg_button(r, &button) || !move_first(r)) return false;
	shady_automation_pointer_button(r->ipc->server, button, pressed);
	return true;
}

static bool cmd_pointer_down(struct request *r) { return pointer_button(r, true); }
static bool cmd_pointer_up(struct request *r) { return pointer_button(r, false); }

static bool cmd_pointer_scroll(struct request *r) {
	double dx = 0, dy = 0;
	if (!arg_number(r, "dx", false, -100, 100, &dx) ||
			!arg_number(r, "dy", false, -100, 100, &dy))
		return false;
	if (dx == 0 && dy == 0) return error(r, "give dx or dy, in wheel clicks");
	if (!move_first(r)) return false;
	shady_automation_scroll(r->ipc->server, dx, dy);
	return true;
}

static void op_done(void *data, const struct shady_automation_result *result) {
	struct ipc_pending *pending = data;
	pending->op = NULL; /* finished: freed by automation.c */
	struct json_buf out = {0};
	json_printf(&out, "{\"settled\":%s,\"elapsed\":%u}",
		result->settled ? "true" : "false", result->elapsed_ms);
	pending_reply(pending, true, &out, NULL);
	json_buf_free(&out);
}

static bool cmd_pointer_drag(struct request *r) {
	double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	uint32_t button;
	int steps = 10, duration = 300;
	if (!arg_point(r, "x1", "y1", true, &x1, &y1, NULL) ||
			!arg_point(r, "x2", "y2", true, &x2, &y2, NULL) ||
			!arg_button(r, &button) ||
			!arg_int(r, "steps", 1, 256, &steps) ||
			!arg_int(r, "duration", 0, 10000, &duration))
		return false;
	if (duration == 0) {
		bool now = false;
		shady_automation_drag(r->ipc->server, x1, y1, x2, y2, button, steps, 0,
			NULL, NULL, &now);
		return now || error(r, "drag failed");
	}
	struct ipc_pending *pending = defer(r);
	if (!pending) return false;
	pending->op = shady_automation_drag(r->ipc->server, x1, y1, x2, y2, button,
		steps, duration, op_done, pending, NULL);
	return pending->op || error(r, "drag failed");
}

static bool cmd_window_at(struct request *r) {
	double x = 0, y = 0;
	if (!arg_point(r, "x", "y", true, &x, &y, NULL)) return false;
	struct shady_toplevel *t = shady_input_toplevel_at(r->ipc->server, x, y);
	if (t && t->mapped) write_window(r->data, r->ipc, t);
	else json_puts(r->data, "null");
	return true;
}

/* wait: at least "ms", then until nothing new is drawn for "idle" ms, for
 * at most "timeout" ms. Answers {"settled": bool, "elapsed": ms}. */
static bool cmd_wait(struct request *r) {
	int ms = 0, idle = 0, timeout = 5000;
	if (!arg_int(r, "ms", 0, 60000, &ms) || !arg_int(r, "idle", 0, 10000, &idle) ||
			!arg_int(r, "timeout", 0, 60000, &timeout))
		return false;
	if (ms == 0 && idle == 0) {
		json_puts(r->data, "{\"settled\":true,\"elapsed\":0}");
		return true;
	}
	struct ipc_pending *pending = defer(r);
	if (!pending) return false;
	pending->op = shady_automation_wait(r->ipc->server, ms, idle, timeout, op_done, pending);
	return pending->op || error(r, "wait failed");
}

static bool cmd_plugin_unload(struct request *r) {
	const char *name = request_string(r, "name");
	if (!name) return false;
	struct shady_server *server = r->ipc->server;
	return shady_plugin_unload(server, shady_plugin_manager_module_name(server, name)) ||
		error(r, "unload failed");
}

static bool cmd_plugin_reload(struct request *r) {
	const char *name = request_string(r, "name");
	if (!name) return false;
	struct shady_server *server = r->ipc->server;
	return shady_plugin_reload(server, shady_plugin_manager_module_name(server, name)) ||
		error(r, "reload failed");
}

static bool cmd_subscribe(struct request *r) {
	const struct json_value *events = json_get(r->args, "events");
	uint32_t mask = 0;
	if (!events) {
		mask = IPC_ALL_EVENTS;
	} else if (events->type != JSON_ARRAY) {
		return error(r, "events must be an array of event names");
	} else {
		for (size_t i = 0; i < events->count; i++) {
			int type = event_type_from_name(events->items[i]);
			if (type < 0) return error(r, "unknown event name");
			mask |= 1u << type;
		}
	}
	/* New value subscribers first get every current value as an event. */
	r->replay_values = (mask & (1u << IPC_VALUE_EVENT)) &&
		!(r->client->subscriptions & (1u << IPC_VALUE_EVENT));
	r->client->subscriptions |= mask;
	json_puts(r->data, "[");
	bool first = true;
	for (int type = 0; type <= IPC_LAST_EVENT; type++) {
		if (!(r->client->subscriptions & (1u << type))) continue;
		if (!first) json_puts(r->data, ",");
		first = false;
		json_string(r->data, event_name(type));
	}
	json_puts(r->data, "]");
	return true;
}

static void write_value_field(const char *key, const char *value, void *data) {
	struct request *r = data;
	if (r->data->len > 1) json_puts(r->data, ",");
	json_string(r->data, key);
	json_puts(r->data, ":");
	json_string(r->data, value);
}

/* values: every published value, as {key: value}. */
static bool cmd_values(struct request *r) {
	json_puts(r->data, "{");
	shady_values_for_each(write_value_field, r);
	json_puts(r->data, "}");
	return true;
}

/* value {key}: one published value, or null. */
static bool cmd_value(struct request *r) {
	const char *key = request_string(r, "key");
	if (!key) return false;
	const char *value = shady_values_get(key);
	if (value) json_string(r->data, value);
	else json_puts(r->data, "null");
	return true;
}

static void write_shortcut_item(const struct shady_shortcut *shortcut, void *data) {
	struct json_buf *b = data;
	if (b->len > 1) json_puts(b, ",");
	json_puts(b, "{");
	write_shortcut_fields(b, shortcut);
	json_puts(b, "}");
}

/* shortcuts: every shortcut the overlay would list. */
static bool cmd_shortcuts(struct request *r) {
	json_puts(r->data, "[");
	shady_shortcuts_for_each(r->ipc->server, write_shortcut_item, r->data);
	json_puts(r->data, "]");
	return true;
}

static bool cmd_shortcuts_toggle(struct request *r) {
	shady_shortcuts_toggle(r->ipc->server);
	return true;
}

static void capture_done(const struct shady_capture_result *result, void *data) {
	struct ipc_pending *pending = data;
	pending->capture = false;
	if (!result->ok) {
		pending_reply(pending, false, NULL, "screenshot failed");
		return;
	}
	struct json_buf out = {0};
	json_puts(&out, "{\"path\":");
	json_string(&out, result->path);
	json_puts(&out, ",\"output\":");
	json_string(&out, result->output);
	json_printf(&out, ",\"width\":%d,\"height\":%d,\"x\":%g,\"y\":%g,"
		"\"logical_width\":%g,\"logical_height\":%g,\"scale\":%g}",
		result->width, result->height, result->x, result->y,
		result->logical_width, result->logical_height, result->scale);
	pending_reply(pending, true, &out, NULL);
	json_buf_free(&out);
}

/* screenshot: capture the next frame to "path" (default: a timestamped file
 * in ~/Pictures) after "delay" ms. Replies with the path; the file is
 * there once the value screenshot.saved names it. With "wait": true it
 * replies once the file is written, with the image's size and where it lies
 * in layout coordinates. "output", "window" or "x"/"y"/"width"/"height"
 * choose what is captured; "max_width"/"max_height" scale it down. */
static bool cmd_screenshot(struct request *r) {
	struct shady_capture_options options = {
		.path = json_get_string(r->args, "path"),
		.output = json_get_string(r->args, "output"),
	};
	if (!arg_int(r, "delay", 0, 10000, &options.delay_ms) ||
			!arg_int(r, "max_width", 0, 16384, &options.max_width) ||
			!arg_int(r, "max_height", 0, 16384, &options.max_height))
		return false;
	bool has_x = has_arg(r, "x"), has_y = has_arg(r, "y");
	bool has_w = has_arg(r, "width"), has_h = has_arg(r, "height");
	if (has_x || has_y || has_w || has_h) {
		if (!(has_x && has_y && has_w && has_h))
			return error(r, "a region needs x, y, width and height");
		if (!arg_number(r, "x", true, -COORD_MAX, COORD_MAX, &options.x) ||
				!arg_number(r, "y", true, -COORD_MAX, COORD_MAX, &options.y) ||
				!arg_number(r, "width", true, 1, COORD_MAX, &options.width) ||
				!arg_number(r, "height", true, 1, COORD_MAX, &options.height))
			return false;
		options.has_region = true;
	}
	if (has_arg(r, "window")) {
		if (options.has_region) return error(r, "give a window or a region, not both");
		struct shady_toplevel *t = request_window(r);
		if (!t) return false;
		if (!t->scene_tree || !t->scene_tree->node.enabled || !t->xdg_toplevel)
			return error(r, "the window is not shown");
		options.has_region = true;
		options.x = t->scene_tree->node.x;
		options.y = t->scene_tree->node.y;
		options.width = t->xdg_toplevel->base->geometry.width;
		options.height = t->xdg_toplevel->base->geometry.height;
	}
	const struct json_value *wait = json_get(r->args, "wait");
	if (wait && wait->type != JSON_BOOL && wait->type != JSON_NULL)
		return error(r, "wait must be true or false");
	bool waiting = wait && wait->type == JSON_BOOL && wait->boolean;

	struct ipc_pending *pending = NULL;
	if (waiting) {
		pending = defer(r);
		if (!pending) return false;
		options.done = capture_done;
		options.data = pending;
	}
	char chosen[4096];
	const char *message = NULL;
	if (!shady_capture_request_with(r->ipc->server, &options, chosen, sizeof(chosen),
			&message))
		return error(r, message);
	if (pending) {
		pending->capture = true;
		return true;
	}
	json_puts(r->data, "{\"path\":");
	json_string(r->data, chosen);
	json_puts(r->data, "}");
	return true;
}

static bool cmd_quit(struct request *r) {
	wl_display_terminate(r->ipc->server->wl_display);
	return true;
}

static bool cmd_lua(struct request *r) {
	const char *code = json_get_string(r->args, "code");
	if (!code) return error(r, "missing string argument: code");
#if SHADY_HAS_LUA
	const char *name = json_get_string(r->args, "name");
	if (!shady_lua_eval(r->ipc->server, code, name ? name : "=shadyctl",
			r->data, r->lua_error, sizeof(r->lua_error)))
		return error(r, r->lua_error);
	return true;
#else
	return error(r, "Lua runtime was not built");
#endif
}

static const struct {
	const char *name;
	bool (*run)(struct request *r);
} commands[] = {
	{ "version", cmd_version },
	{ "lua", cmd_lua },
	{ "windows", cmd_windows },
	{ "focused", cmd_focused },
	{ "workspaces", cmd_workspaces },
	{ "outputs", cmd_outputs },
	{ "plugins", cmd_plugins },
	{ "values", cmd_values },
	{ "value", cmd_value },
	{ "shortcuts", cmd_shortcuts },
	{ "shortcuts.toggle", cmd_shortcuts_toggle },
	{ "window.focus", cmd_window_focus },
	{ "window.close", cmd_window_close },
	{ "window.maximize", cmd_window_maximize },
	{ "window.fullscreen", cmd_window_fullscreen },
	{ "window.move", cmd_window_move },
	{ "workspace.switch", cmd_workspace_switch },
	{ "key", cmd_key },
	{ "key.down", cmd_key_down },
	{ "key.up", cmd_key_up },
	{ "type", cmd_type },
	{ "pointer", cmd_pointer },
	{ "pointer.move", cmd_pointer_move },
	{ "pointer.click", cmd_pointer_click },
	{ "pointer.down", cmd_pointer_down },
	{ "pointer.up", cmd_pointer_up },
	{ "pointer.scroll", cmd_pointer_scroll },
	{ "pointer.drag", cmd_pointer_drag },
	{ "window.at", cmd_window_at },
	{ "wait", cmd_wait },
	{ "screenshot", cmd_screenshot },
	{ "plugin.unload", cmd_plugin_unload },
	{ "plugin.reload", cmd_plugin_reload },
	{ "subscribe", cmd_subscribe },
	{ "quit", cmd_quit },
};

static void handle_line(struct ipc_client *client, char *line) {
	struct json_object args;
	const char *parse_error = NULL;
	struct json_buf data = {0};
	struct request r = { .ipc = client->ipc, .client = client, .args = &args, .data = &data };
	const struct json_value *id = NULL;
	bool ok = false;

	if (!json_parse_object(line, &args, &parse_error)) {
		r.error = parse_error;
	} else {
		id = json_get(&args, "id");
		r.id = id;
		const char *cmd = json_get_string(&args, "cmd");
		if (!cmd) {
			r.error = "missing \"cmd\"";
		} else {
			r.error = "unknown command";
			for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
				if (strcmp(commands[i].name, cmd) != 0) continue;
				r.error = NULL;
				ok = commands[i].run(&r);
				break;
			}
		}
	}

	if (r.pending) {
		if (ok) {
			/* Answered later; the lines after this one wait for it. */
			client->waiting = r.pending;
			json_buf_free(&data);
			return;
		}
		cancel_pending(r.pending);
	}

	struct json_buf id_text = {0};
	write_id(&id_text, id);
	struct json_buf response = {0};
	write_response(&response, &id_text, ok, &data, r.error);
	send_line(client, &response);
	if (ok && r.replay_values) shady_values_for_each(replay_value, client);
	json_buf_free(&response);
	json_buf_free(&id_text);
	json_buf_free(&data);
}

/* ---- sockets ---------------------------------------------------------- */

/* Run the complete lines read so far, up to one that is answered later. */
static void process_input(struct ipc_client *client) {
	size_t start = 0;
	for (size_t i = 0; i < client->in_len && !client->dead && !client->waiting; i++) {
		if (client->in[i] != '\n') continue;
		client->in[i] = '\0';
		char *line = client->in + start;
		start = i + 1;
		if (line[strspn(line, " \t\r")] == '\0') continue; /* blank line */
		handle_line(client, line);
	}
	if (client->dead) return;
	memmove(client->in, client->in + start, client->in_len - start);
	client->in_len -= start;
}

static int client_ready(int fd, uint32_t mask, void *data) {
	(void)fd;
	struct ipc_client *client = data;
	if (client->dead) return 0;
	if (mask & WL_EVENT_WRITABLE) flush(client);
	if (mask & WL_EVENT_READABLE) {
		for (;;) {
			if (client->dead || client->waiting) break;
			ssize_t n = recv(client->fd, client->in + client->in_len,
				sizeof(client->in) - client->in_len, 0);
			if (n < 0 && errno == EINTR) continue;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
			if (n <= 0) {
				kill_client(client);
				return 0;
			}
			client->in_len += (size_t)n;
			process_input(client);
			if (client->dead) return 0;
			if (!client->waiting && client->in_len == sizeof(client->in)) {
				struct json_buf line = {0};
				json_puts(&line, "{\"ok\":false,\"error\":\"request too long\"}");
				send_line(client, &line);
				json_buf_free(&line);
				kill_client(client);
				return 0;
			}
		}
	}
	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) kill_client(client);
	else if (!client->dead) flush(client); /* reading stops while one waits */
	return 0;
}

static int listen_ready(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct shady_ipc *ipc = data;
	for (;;) {
		int client_fd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
		if (client_fd < 0) {
			if (errno == EINTR) continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				wlr_log_errno(WLR_ERROR, "ipc: accept failed");
			return 0;
		}
		struct ipc_client *client = calloc(1, sizeof(*client));
		if (!client) {
			close(client_fd);
			continue;
		}
		client->ipc = ipc;
		client->fd = client_fd;
		struct wl_event_loop *loop = wl_display_get_event_loop(ipc->server->wl_display);
		client->source = wl_event_loop_add_fd(loop, client_fd, WL_EVENT_READABLE,
			client_ready, client);
		if (!client->source) {
			close(client_fd);
			free(client);
			continue;
		}
		wl_list_insert(ipc->clients.prev, &client->link);
	}
}

bool shady_ipc_init(struct shady_server *server, const char *wayland_socket) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (!runtime || !*runtime || !wayland_socket) {
		wlr_log(WLR_ERROR, "ipc: XDG_RUNTIME_DIR is not set; IPC disabled");
		return false;
	}
	struct shady_ipc *ipc = calloc(1, sizeof(*ipc));
	if (!ipc) return false;
	ipc->server = server;
	ipc->listen_fd = -1;
	wl_list_init(&ipc->clients);
	wl_list_init(&ipc->pending);

	int n = snprintf(ipc->path, sizeof(ipc->path), "%s/shady-%s.sock", runtime, wayland_socket);
	if (n < 0 || (size_t)n >= sizeof(ipc->path)) {
		wlr_log(WLR_ERROR, "ipc: socket path too long; IPC disabled");
		free(ipc);
		return false;
	}

	ipc->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	memcpy(addr.sun_path, ipc->path, (size_t)n + 1);
	/* The Wayland socket name is unique while we hold it, so a leftover
	 * file at this path belongs to a dead compositor. */
	unlink(ipc->path);
	mode_t old_mask = umask(0077);
	bool bound = ipc->listen_fd >= 0 &&
		bind(ipc->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
	umask(old_mask);
	if (!bound || listen(ipc->listen_fd, 16) != 0) {
		wlr_log_errno(WLR_ERROR, "ipc: cannot listen on %s", ipc->path);
		if (ipc->listen_fd >= 0) close(ipc->listen_fd);
		free(ipc);
		return false;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
	ipc->listen_source = wl_event_loop_add_fd(loop, ipc->listen_fd, WL_EVENT_READABLE,
		listen_ready, ipc);
	bool subscribed = ipc->listen_source != NULL;
	for (int type = 0; subscribed && type < SHADY_EVENT_COUNT; type++)
		subscribed = shady_event_subscribe_owned(server, (uint32_t)type, on_event, ipc, ipc) != 0;
	server->ipc = ipc;
	if (!subscribed) {
		shady_ipc_finish(server);
		return false;
	}
	setenv("SHADY_SOCKET", ipc->path, 1);
	wlr_log(WLR_INFO, "ipc: listening on %s", ipc->path);
	return true;
}

void shady_ipc_finish(struct shady_server *server) {
	struct shady_ipc *ipc = ipc_of(server);
	if (!ipc) return;
	shady_event_unsubscribe_owner(server, ipc);
	struct ipc_pending *pending, *pending_tmp;
	wl_list_for_each_safe(pending, pending_tmp, &ipc->pending, link)
		cancel_pending(pending);
	struct ipc_client *client;
	if (ipc->reap_source) wl_event_source_remove(ipc->reap_source);
	wl_list_for_each(client, &ipc->clients, link) client->dead = true;
	reap(ipc);
	if (ipc->listen_source) wl_event_source_remove(ipc->listen_source);
	if (ipc->listen_fd >= 0) close(ipc->listen_fd);
	unlink(ipc->path);
	if (getenv("SHADY_SOCKET") && !strcmp(getenv("SHADY_SOCKET"), ipc->path))
		unsetenv("SHADY_SOCKET");
	free(ipc);
	server->ipc = NULL;
}
