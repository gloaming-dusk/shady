/*
 * shadyctl: command-line client for the compositor IPC socket.
 *
 *   shadyctl windows
 *   shadyctl window.focus window=3
 *   shadyctl key keys=Super+h
 *   shadyctl subscribe events=window.focused,workspace.changed
 *   shadyctl --raw '{"cmd":"workspaces"}'
 *
 * Arguments are key=value pairs: true/false/null and numbers are sent as
 * JSON values, `events` is split on commas into an array, anything else is a
 * string. Prints the response's data (or the raw response with --json) and
 * exits 1 when the compositor reports an error. `subscribe` then prints one
 * event per line until the compositor goes away.
 *
 * The socket is $SHADY_SOCKET, else $XDG_RUNTIME_DIR/shady-$WAYLAND_DISPLAY.sock.
 * See docs/IPC_API.md.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../src/ipc/json.h"

static void usage(FILE *out) {
	fprintf(out,
		"usage: shadyctl [-s socket] [--json] <command> [key=value ...]\n"
		"       shadyctl [-s socket] --raw '<json request>'\n"
		"commands: version windows focused workspaces outputs plugins\n"
		"          window.focus window.close window.maximize window.fullscreen window.move\n"
		"          workspace.switch key plugin.reload plugin.unload subscribe quit\n");
}

static bool is_number(const char *s) {
	char *end = NULL;
	if (!*s) return false;
	strtod(s, &end);
	return end && *end == '\0';
}

static void add_value(struct json_buf *b, const char *key, const char *value) {
	if (!strcmp(key, "events")) {
		json_puts(b, "[");
		char *copy = strdup(value);
		if (!copy) return;
		char *save = NULL;
		bool first = true;
		for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
			if (!first) json_puts(b, ",");
			first = false;
			json_string(b, tok);
		}
		free(copy);
		json_puts(b, "]");
	} else if (!strcmp(value, "true") || !strcmp(value, "false") ||
			!strcmp(value, "null") || is_number(value)) {
		json_puts(b, value);
	} else {
		json_string(b, value);
	}
}

static const char *default_socket(char *buf, size_t size) {
	const char *env = getenv("SHADY_SOCKET");
	if (env && *env) return env;
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	if (!runtime || !*runtime) return NULL;
	snprintf(buf, size, "%s/shady-%s.sock", runtime, display && *display ? display : "wayland-0");
	return buf;
}

/* Read one line into `line` (cleared first). False on EOF or error. */
static bool read_line(int fd, struct json_buf *pending, struct json_buf *line) {
	json_buf_clear(line);
	for (;;) {
		char *nl = pending->len ? memchr(pending->data, '\n', pending->len) : NULL;
		if (nl) {
			size_t n = (size_t)(nl - pending->data);
			json_append(line, pending->data, n);
			json_consume(pending, n + 1);
			return !line->failed;
		}
		char chunk[4096];
		ssize_t got = recv(fd, chunk, sizeof(chunk), 0);
		if (got < 0 && errno == EINTR) continue;
		if (got <= 0) return false;
		json_append(pending, chunk, (size_t)got);
		if (pending->failed) return false;
	}
}

/* Print the "data" member of a response line, or the line itself. The
 * compositor writes `"data":` last, so the value runs to the final '}'. */
static void print_data(const char *line) {
	const char *data = strstr(line, ",\"data\":");
	if (!data) return;
	data += strlen(",\"data\":");
	size_t len = strlen(data);
	if (len && data[len - 1] == '}') len--;
	printf("%.*s\n", (int)len, data);
}

int main(int argc, char **argv) {
	const char *socket_path = NULL;
	const char *raw = NULL;
	bool json_output = false;
	int i = 1;
	for (; i < argc && argv[i][0] == '-'; i++) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc) socket_path = argv[++i];
		else if (!strcmp(argv[i], "--raw") && i + 1 < argc) raw = argv[++i];
		else if (!strcmp(argv[i], "--json")) json_output = true;
		else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(stdout);
			return 0;
		} else {
			usage(stderr);
			return 2;
		}
	}
	if (!raw && i >= argc) {
		usage(stderr);
		return 2;
	}

	struct json_buf request = {0};
	const char *cmd = raw ? NULL : argv[i];
	if (raw) {
		json_puts(&request, raw);
	} else {
		json_puts(&request, "{\"id\":1,\"cmd\":");
		json_string(&request, cmd);
		for (int a = i + 1; a < argc; a++) {
			const char *eq = strchr(argv[a], '=');
			if (!eq || eq == argv[a]) {
				fprintf(stderr, "shadyctl: expected key=value, got '%s'\n", argv[a]);
				return 2;
			}
			char key[64];
			size_t key_len = (size_t)(eq - argv[a]);
			if (key_len >= sizeof(key)) {
				fprintf(stderr, "shadyctl: key too long\n");
				return 2;
			}
			memcpy(key, argv[a], key_len);
			key[key_len] = '\0';
			json_puts(&request, ",");
			json_string(&request, key);
			json_puts(&request, ":");
			add_value(&request, key, eq + 1);
		}
		json_puts(&request, "}");
	}
	json_puts(&request, "\n");
	if (request.failed) return 1;

	char path_buf[sizeof(((struct sockaddr_un *)0)->sun_path)];
	if (!socket_path) socket_path = default_socket(path_buf, sizeof(path_buf));
	if (!socket_path || strlen(socket_path) >= sizeof(path_buf)) {
		fprintf(stderr, "shadyctl: cannot find the Shady socket (set SHADY_SOCKET)\n");
		return 1;
	}
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strcpy(addr.sun_path, socket_path);
	if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "shadyctl: cannot connect to %s: %s\n", socket_path, strerror(errno));
		return 1;
	}
	for (size_t sent = 0; sent < request.len;) {
		ssize_t n = send(fd, request.data + sent, request.len - sent, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) {
			fprintf(stderr, "shadyctl: write failed: %s\n", strerror(errno));
			return 1;
		}
		sent += (size_t)n;
	}

	struct json_buf pending = {0}, line = {0};
	if (!read_line(fd, &pending, &line)) {
		fprintf(stderr, "shadyctl: no response\n");
		return 1;
	}
	bool ok = strstr(line.data, "\"ok\":true") != NULL;
	if (json_output || raw || !ok) puts(line.data);
	else print_data(line.data);
	fflush(stdout);

	bool subscribing = ok && ((cmd && !strcmp(cmd, "subscribe")) ||
		(raw && strstr(raw, "\"subscribe\"")));
	while (subscribing && read_line(fd, &pending, &line)) {
		puts(line.data);
		fflush(stdout);
	}
	close(fd);
	json_buf_free(&request);
	json_buf_free(&pending);
	json_buf_free(&line);
	return ok ? 0 : 1;
}
