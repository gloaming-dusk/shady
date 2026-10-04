#define _GNU_SOURCE
#include "ipc.h"

#include <errno.h>
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

/* A request line longer than this is refused and the client dropped. */
#define IPC_LINE_MAX (64 * 1024)
/* A subscriber that stops reading is dropped once this much output queues. */
#define IPC_OUT_MAX (4 * 1024 * 1024)

struct shady_ipc {
	struct shady_server *server;
	int listen_fd;
	struct wl_event_source *listen_source;
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	struct wl_list clients;
	struct shady_toplevel *focused;
	struct wl_event_source *reap_source; /* pending idle reap */
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
};

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
		WL_EVENT_READABLE | (client->out.len ? WL_EVENT_WRITABLE : 0));
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
	return -1;
}

/* ---- commands --------------------------------------------------------- */

struct request {
	struct shady_ipc *ipc;
	struct ipc_client *client;
	const struct json_object *args;
	struct json_buf *data; /* the response's "data" value */
	const char *error;
};

static bool error(struct request *r, const char *message) {
	r->error = message;
	return false;
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

/* "Super+Shift+z": the same shortcut syntax as shady.bind. The key goes
 * through module hooks, plugins and bindings, exactly like a real press. */
static bool cmd_key(struct request *r) {
	const char *spec = request_string(r, "keys");
	if (!spec) return false;
	char buf[128];
	if (strlen(spec) >= sizeof(buf)) return error(r, "shortcut too long");
	strcpy(buf, spec);
	uint32_t mods = 0;
	const char *key = NULL;
	char *save = NULL;
	for (char *tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
		uint32_t m = modifier_from_name(tok);
		if (m) mods |= m;
		else if (key) return error(r, "shortcut has more than one key");
		else key = tok;
	}
	if (!key) return error(r, "shortcut has no key");
	xkb_keysym_t sym = xkb_keysym_from_name(key, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym == XKB_KEY_NoSymbol) return error(r, "unknown key");
	bool pressed = shady_input_automation_key(r->ipc->server, sym, mods, true);
	bool released = shady_input_automation_key(r->ipc->server, sym, mods, false);
	json_puts(r->data, pressed || released ? "{\"handled\":true}" : "{\"handled\":false}");
	return true;
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
		mask = (1u << SHADY_EVENT_COUNT) - 1;
	} else if (events->type != JSON_ARRAY) {
		return error(r, "events must be an array of event names");
	} else {
		for (size_t i = 0; i < events->count; i++) {
			int type = event_type_from_name(events->items[i]);
			if (type < 0) return error(r, "unknown event name");
			mask |= 1u << type;
		}
	}
	r->client->subscriptions |= mask;
	json_puts(r->data, "[");
	bool first = true;
	for (int type = 0; type < SHADY_EVENT_COUNT; type++) {
		if (!(r->client->subscriptions & (1u << type))) continue;
		if (!first) json_puts(r->data, ",");
		first = false;
		json_string(r->data, shady_event_name(type));
	}
	json_puts(r->data, "]");
	return true;
}

static bool cmd_quit(struct request *r) {
	wl_display_terminate(r->ipc->server->wl_display);
	return true;
}

static const struct {
	const char *name;
	bool (*run)(struct request *r);
} commands[] = {
	{ "version", cmd_version },
	{ "windows", cmd_windows },
	{ "focused", cmd_focused },
	{ "workspaces", cmd_workspaces },
	{ "outputs", cmd_outputs },
	{ "plugins", cmd_plugins },
	{ "window.focus", cmd_window_focus },
	{ "window.close", cmd_window_close },
	{ "window.maximize", cmd_window_maximize },
	{ "window.fullscreen", cmd_window_fullscreen },
	{ "window.move", cmd_window_move },
	{ "workspace.switch", cmd_workspace_switch },
	{ "key", cmd_key },
	{ "plugin.unload", cmd_plugin_unload },
	{ "plugin.reload", cmd_plugin_reload },
	{ "subscribe", cmd_subscribe },
	{ "quit", cmd_quit },
};

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

	struct json_buf response = {0};
	json_puts(&response, "{");
	write_id(&response, id);
	if (ok) {
		json_puts(&response, "\"ok\":true");
		if (data.len) {
			json_puts(&response, ",\"data\":");
			json_append(&response, data.data, data.len);
		}
	} else {
		json_puts(&response, "\"ok\":false,\"error\":");
		json_string(&response, r.error ? r.error : "failed");
	}
	json_puts(&response, "}");
	send_line(client, &response);
	json_buf_free(&response);
	json_buf_free(&data);
}

/* ---- sockets ---------------------------------------------------------- */

static int client_ready(int fd, uint32_t mask, void *data) {
	(void)fd;
	struct ipc_client *client = data;
	if (client->dead) return 0;
	if (mask & WL_EVENT_WRITABLE) flush(client);
	if (mask & WL_EVENT_READABLE) {
		for (;;) {
			if (client->dead) return 0;
			ssize_t n = recv(client->fd, client->in + client->in_len,
				sizeof(client->in) - client->in_len, 0);
			if (n < 0 && errno == EINTR) continue;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
			if (n <= 0) {
				kill_client(client);
				return 0;
			}
			client->in_len += (size_t)n;
			size_t start = 0;
			for (size_t i = 0; i < client->in_len && !client->dead; i++) {
				if (client->in[i] != '\n') continue;
				client->in[i] = '\0';
				char *line = client->in + start;
				start = i + 1;
				if (line[strspn(line, " \t\r")] == '\0') continue; /* blank line */
				handle_line(client, line);
			}
			if (client->dead) return 0;
			memmove(client->in, client->in + start, client->in_len - start);
			client->in_len -= start;
			if (client->in_len == sizeof(client->in)) {
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
