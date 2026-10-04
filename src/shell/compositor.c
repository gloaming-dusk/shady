#define _GNU_SOURCE

#include "compositor.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "../ipc/json.h"
#include "script.h"
#include "shell.h"

#define RECONNECT_MS 2000
#define LINE_MAX_BYTES (64 * 1024)

struct value {
    struct wl_list link;
    char *key;
    char *value;
};

static struct {
    struct shell *shell;
    int fd;
    struct shell_watch *watch;
    struct shell_timer *retry;
    struct json_buf in;
    struct wl_list values;
    bool ready;
    bool connected_once;
} link_state = { .fd = -1 };

static void connect_later(void);

static struct value *find(const char *key) {
    struct value *v;
    wl_list_for_each(v, &link_state.values, link)
        if (!strcmp(v->key, key)) return v;
    return NULL;
}

static void set_value(const char *key, const char *value) {
    struct value *v = find(key);
    if (!value) {
        if (!v) return;
        wl_list_remove(&v->link);
        free(v->key);
        free(v->value);
        free(v);
    } else if (v) {
        if (!strcmp(v->value, value)) return;
        char *copy = strdup(value);
        if (!copy) return;
        free(v->value);
        v->value = copy;
    } else {
        v = calloc(1, sizeof(*v));
        if (!v || !(v->key = strdup(key)) || !(v->value = strdup(value))) {
            if (v) free(v->key);
            free(v);
            return;
        }
        wl_list_insert(link_state.values.prev, &v->link);
    }
    shell_lua_model_changed(link_state.shell);
}

static void handle_line(char *line) {
    struct json_object object;
    const char *error = NULL;
    if (!json_parse_object(line, &object, &error)) return;
    const char *event = json_get_string(&object, "event");
    if (event && !strcmp(event, "value.changed")) {
        const char *key = json_get_string(&object, "key");
        const struct json_value *value = json_get(&object, "value");
        if (key) set_value(key, value && value->type == JSON_STRING ? value->string : NULL);
        return;
    }
    /* The only response is to our subscribe; report a refusal once. */
    const struct json_value *ok = json_get(&object, "ok");
    if (ok && ok->type == JSON_BOOL && !ok->boolean)
        fprintf(stderr, "shady-shell: compositor IPC refused the subscription: %s\n",
            json_get_string(&object, "error") ? json_get_string(&object, "error") : "?");
}

static void disconnect(void) {
    if (link_state.watch) shell_watch_remove(link_state.watch);
    link_state.watch = NULL;
    if (link_state.fd >= 0) close(link_state.fd);
    link_state.fd = -1;
    json_buf_clear(&link_state.in);
}

static void readable(int fd, short revents, void *data) {
    (void)data;
    for (;;) {
        char chunk[4096];
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n <= 0) {
            fprintf(stderr, "shady-shell: compositor IPC closed; reconnecting\n");
            disconnect();
            connect_later();
            return;
        }
        json_append(&link_state.in, chunk, (size_t)n);
        if (link_state.in.failed || link_state.in.len > LINE_MAX_BYTES) {
            disconnect();
            connect_later();
            return;
        }
        char *nl;
        while (link_state.in.len && (nl = memchr(link_state.in.data, '\n', link_state.in.len))) {
            *nl = '\0';
            handle_line(link_state.in.data);
            json_consume(&link_state.in, (size_t)(nl - link_state.in.data) + 1);
        }
    }
    if (revents & (POLLHUP | POLLERR)) {
        disconnect();
        connect_later();
    }
}

static const char *socket_path(char *buf, size_t size) {
    const char *path = getenv("SHADY_SHELL_IPC_SOCKET");
    if (path && *path) return path;
    path = getenv("SHADY_SOCKET");
    if (path && *path) return path;
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *display = getenv("WAYLAND_DISPLAY");
    if (!runtime || !*runtime) return NULL;
    snprintf(buf, size, "%s/shady-%s.sock", runtime, display && *display ? display : "wayland-0");
    return buf;
}

static bool try_connect(void) {
    char buf[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *path = socket_path(buf, sizeof(buf));
    if (!path || strlen(path) >= sizeof(buf)) return false;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        if (fd >= 0) close(fd);
        return false;
    }
    static const char request[] = "{\"cmd\":\"subscribe\",\"events\":[\"value.changed\"]}\n";
    if (send(fd, request, sizeof(request) - 1, MSG_NOSIGNAL) != (ssize_t)(sizeof(request) - 1)) {
        close(fd);
        return false;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    link_state.fd = fd;
    link_state.watch = shell_watch_add(&link_state.shell->core, fd, POLLIN, readable, NULL);
    /* A reconnect gets a fresh replay of every value. */
    struct value *v, *tmp;
    wl_list_for_each_safe(v, tmp, &link_state.values, link) {
        wl_list_remove(&v->link);
        free(v->key);
        free(v->value);
        free(v);
    }
    if (!link_state.connected_once)
        fprintf(stderr, "shady-shell: following compositor values on %s\n", path);
    link_state.connected_once = true;
    return true;
}

static void retry(void *data) {
    (void)data;
    link_state.retry = NULL;
    if (!try_connect()) connect_later();
}

static void connect_later(void) {
    if (!link_state.retry)
        link_state.retry = shell_timer_add(&link_state.shell->core, RECONNECT_MS, retry, NULL);
}

void shell_compositor_init(struct shell *shell) {
    link_state.shell = shell;
    wl_list_init(&link_state.values);
    link_state.ready = true;
    if (!try_connect()) connect_later();
}

void shell_compositor_finish(void) {
    if (!link_state.ready) return;
    disconnect();
    shell_timer_cancel(link_state.retry);
    link_state.retry = NULL;
    while (!wl_list_empty(&link_state.values)) {
        struct value *v = wl_container_of(link_state.values.next, v, link);
        wl_list_remove(&v->link);
        free(v->key);
        free(v->value);
        free(v);
    }
    json_buf_free(&link_state.in);
    link_state.ready = false;
}

const char *shell_compositor_value(const char *key) {
    if (!link_state.ready || !key) return NULL;
    struct value *v = find(key);
    return v ? v->value : NULL;
}
