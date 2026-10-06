/*
 * greetd client for shady-shell: lets a Lua config log people in through
 * greetd's IPC socket ($GREETD_SOCK). See docs/GREETER.md.
 *
 * Values
 *   greetd.users          "name\tdisplay name" per line: regular accounts
 *   greetd.sessions       "name\tid" per line: wayland-sessions entries
 *   greetd.state          idle | busy | prompt | done | error
 *   greetd.prompt         the PAM prompt while in state prompt
 *   greetd.secret         "1" when the prompt's answer should be hidden
 *   greetd.message        the last info or error text, "" for none
 *   greetd.message_kind   info | error
 *   greetd.last_user, greetd.last_session   from the previous login
 *
 * Actions
 *   greetd.session(id)    the session to start once authenticated
 *   greetd.login(user)    start authenticating `user`
 *   greetd.respond(text)  answer the prompt; sent as soon as greetd asks,
 *                         so login + respond in one go needs one keypress
 *   greetd.cancel()       abandon the attempt
 *
 * Once the session is accepted the compositor is asked to quit; greetd
 * starts the session when the greeter exits.
 *
 * SHADY_GREETER_DEMO=1 fakes greetd (any non-empty password works) for
 * trying the greeter in a nested session.
 */

#define _DEFAULT_SOURCE

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <shady/shell_plugin.h>

#include "ipc/json.h"

#define MAX_USERS 64
#define MAX_SESSIONS 64
#define MAX_MESSAGE (64 * 1024)

static const struct shady_shell_api_v1 *api;
static shady_shell_host host;

struct session {
    char id[128]; /* desktop file name without .desktop */
    char name[128];
    char exec[512];
    char desktop[128]; /* DesktopNames, first entry */
};

enum request {
    REQUEST_NONE,
    REQUEST_CREATE,
    REQUEST_RESPOND,
    REQUEST_START,
    REQUEST_CANCEL,
};

static struct {
    int fd;
    bool demo;
    shady_shell_watch watch;
    unsigned char *in;
    size_t in_len;
    enum request inflight;
    bool active; /* greetd holds a session for us */
    char user[64];
    char want_user[64]; /* login requested while a session was active */
    char pending[1024]; /* answer typed before the prompt arrived */
    bool has_pending;
    struct session sessions[MAX_SESSIONS];
    size_t session_count;
    int session; /* chosen index, -1 for none */
    char state_path[512];
} g = { .fd = -1, .session = -1 };

static void wipe(void *data, size_t size) {
    volatile unsigned char *p = data;
    while (size--) *p++ = 0;
}

static void logf_(const char *fmt, const char *arg) {
    char line[512];
    snprintf(line, sizeof(line), fmt, arg ? arg : "");
    api->log(host, line);
}

static void set_state(const char *state) {
    api->set_value(host, "greetd.state", state);
}

static void set_message(const char *kind, const char *text) {
    api->set_value(host, "greetd.message_kind", kind);
    api->set_value(host, "greetd.message", text ? text : "");
}

/* ---- accounts and sessions -------------------------------------------- */

static bool login_shell(const char *shell) {
    if (!shell || !*shell) return true;
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    return strcmp(base, "nologin") != 0 && strcmp(base, "false") != 0;
}

static void publish_users(void) {
    struct json_buf out = {0};
    size_t count = 0;
    setpwent();
    struct passwd *pw;
    while ((pw = getpwent()) && count < MAX_USERS) {
        if (pw->pw_uid < 1000 || pw->pw_uid >= 60000 || !login_shell(pw->pw_shell)) continue;
        char display[128];
        snprintf(display, sizeof(display), "%s", pw->pw_gecos ? pw->pw_gecos : "");
        char *comma = strchr(display, ',');
        if (comma) *comma = '\0';
        json_printf(&out, "%s%s\t%s", count ? "\n" : "", pw->pw_name,
            display[0] ? display : pw->pw_name);
        count++;
    }
    endpwent();
    json_append(&out, "", 1);
    api->set_value(host, "greetd.users", out.failed ? "" : out.data);
    json_buf_free(&out);
}

static void desktop_entry(const char *dir, const char *file) {
    size_t len = strlen(file);
    if (len <= 8 || strcmp(file + len - 8, ".desktop") != 0) return;
    if (g.session_count >= MAX_SESSIONS) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    FILE *f = fopen(path, "r");
    if (!f) return;
    struct session s = {0};
    snprintf(s.id, sizeof(s.id), "%.*s", (int)(len - 8), file);
    bool in_entry = false, hidden = false;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '[') {
            in_entry = !strcmp(line, "[Desktop Entry]");
            continue;
        }
        if (!in_entry) continue;
        if (!strncmp(line, "Name=", 5)) snprintf(s.name, sizeof(s.name), "%.127s", line + 5);
        else if (!strncmp(line, "Exec=", 5)) snprintf(s.exec, sizeof(s.exec), "%.511s", line + 5);
        else if (!strncmp(line, "DesktopNames=", 13)) {
            snprintf(s.desktop, sizeof(s.desktop), "%.127s", line + 13);
            s.desktop[strcspn(s.desktop, ";")] = '\0';
        } else if (!strcmp(line, "Hidden=true") || !strcmp(line, "NoDisplay=true")) {
            hidden = true;
        }
    }
    fclose(f);
    if (hidden || !s.name[0] || !s.exec[0]) return;
    for (size_t i = 0; i < g.session_count; i++)
        if (!strcmp(g.sessions[i].id, s.id)) return; /* earlier directories win */
    if (!s.desktop[0]) snprintf(s.desktop, sizeof(s.desktop), "%s", s.id);
    g.sessions[g.session_count++] = s;
}

static int session_order(const void *a, const void *b) {
    return strcmp(((const struct session *)a)->name, ((const struct session *)b)->name);
}

static void publish_sessions(void) {
    const char *dirs = getenv("SHADY_GREETER_SESSIONS");
    if (!dirs || !*dirs)
        dirs = "/run/current-system/sw/share/wayland-sessions:"
            "/usr/local/share/wayland-sessions:/usr/share/wayland-sessions";
    char list[2048];
    snprintf(list, sizeof(list), "%s", dirs);
    for (char *save = NULL, *dir = strtok_r(list, ":", &save); dir;
            dir = strtok_r(NULL, ":", &save)) {
        DIR *d = opendir(dir);
        if (!d) continue;
        struct dirent *e;
        while ((e = readdir(d))) desktop_entry(dir, e->d_name);
        closedir(d);
    }
    qsort(g.sessions, g.session_count, sizeof(g.sessions[0]), session_order);
    struct json_buf out = {0};
    for (size_t i = 0; i < g.session_count; i++)
        json_printf(&out, "%s%s\t%s", i ? "\n" : "", g.sessions[i].name, g.sessions[i].id);
    json_append(&out, "", 1);
    api->set_value(host, "greetd.sessions", out.failed ? "" : out.data);
    json_buf_free(&out);
}

/* ---- remembered choice ------------------------------------------------- */

static void find_state_path(void) {
    const char *path = getenv("SHADY_GREETER_STATE");
    if (path && *path) {
        snprintf(g.state_path, sizeof(g.state_path), "%s", path);
        return;
    }
    const char *state = getenv("XDG_STATE_HOME");
    const char *home = getenv("HOME");
    if (state && *state)
        snprintf(g.state_path, sizeof(g.state_path), "%s/shady-greeter/last", state);
    else if (home && *home)
        snprintf(g.state_path, sizeof(g.state_path), "%s/.local/state/shady-greeter/last", home);
}

static void load_last(void) {
    FILE *f = g.state_path[0] ? fopen(g.state_path, "r") : NULL;
    if (!f) return;
    char user[64] = "", session[128] = "";
    if (fgets(user, sizeof(user), f)) user[strcspn(user, "\n")] = '\0';
    if (fgets(session, sizeof(session), f)) session[strcspn(session, "\n")] = '\0';
    fclose(f);
    api->set_value(host, "greetd.last_user", user);
    api->set_value(host, "greetd.last_session", session);
}

static void mkdir_parents(char *path) {
    for (char *p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        mkdir(path, 0700);
        *p = '/';
    }
}

static void save_last(void) {
    if (!g.state_path[0]) return;
    char path[sizeof(g.state_path)];
    snprintf(path, sizeof(path), "%s", g.state_path);
    mkdir_parents(path);
    FILE *f = fopen(g.state_path, "w");
    if (!f) return;
    fprintf(f, "%s\n%s\n", g.user, g.session >= 0 ? g.sessions[g.session].id : "");
    fclose(f);
}

/* ---- leaving the greeter ---------------------------------------------- */

static void quit_compositor(void) {
    char buf[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *path = getenv("SHADY_SOCKET");
    if (!path || !*path) {
        const char *runtime = getenv("XDG_RUNTIME_DIR");
        const char *display = getenv("WAYLAND_DISPLAY");
        if (!runtime || !*runtime) return;
        snprintf(buf, sizeof(buf), "%s/shady-%s.sock", runtime,
            display && *display ? display : "wayland-0");
        path = buf;
    }
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    static const char request[] = "{\"cmd\":\"quit\"}\n";
    if (fd < 0 || connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
            send(fd, request, sizeof(request) - 1, MSG_NOSIGNAL) < 0)
        logf_("cannot ask the compositor to quit: %s", strerror(errno));
    if (fd >= 0) close(fd);
}

static void finish(void) {
    set_state("done");
    save_last();
    if (g.demo) {
        set_message("info", "Demo: the session would start now");
        return;
    }
    quit_compositor();
}

/* ---- greetd IPC ------------------------------------------------------- */

static bool send_request(enum request kind, struct json_buf *json) {
    bool ok = !json->failed && g.fd >= 0;
    if (ok) {
        uint32_t len = (uint32_t)json->len;
        ok = send(g.fd, &len, sizeof(len), MSG_NOSIGNAL) == (ssize_t)sizeof(len) &&
            send(g.fd, json->data, json->len, MSG_NOSIGNAL) == (ssize_t)json->len;
    }
    wipe(json->data, json->cap);
    json_buf_free(json);
    if (!ok) {
        set_message("error", "Lost the connection to greetd");
        set_state("error");
        return false;
    }
    g.inflight = kind;
    set_state("busy");
    return true;
}

static void request_create(const char *user) {
    snprintf(g.user, sizeof(g.user), "%s", user);
    struct json_buf json = {0};
    json_puts(&json, "{\"type\":\"create_session\",\"username\":");
    json_string(&json, user);
    json_puts(&json, "}");
    if (send_request(REQUEST_CREATE, &json)) g.active = true;
}

static void request_respond(const char *answer) {
    struct json_buf json = {0};
    json_puts(&json, "{\"type\":\"post_auth_message_response\"");
    if (answer) {
        json_puts(&json, ",\"response\":");
        json_string(&json, answer);
    }
    json_puts(&json, "}");
    send_request(REQUEST_RESPOND, &json);
}

static void request_cancel(void) {
    struct json_buf json = {0};
    json_puts(&json, "{\"type\":\"cancel_session\"}");
    send_request(REQUEST_CANCEL, &json);
}

static void request_start(void) {
    if (g.session < 0) {
        set_message("error", "No session to start");
        request_cancel();
        return;
    }
    const struct session *s = &g.sessions[g.session];
    struct json_buf json = {0};
    json_puts(&json, "{\"type\":\"start_session\",\"cmd\":[\"/bin/sh\",\"-c\",");
    json_string(&json, s->exec);
    json_puts(&json, "],\"env\":[\"XDG_SESSION_TYPE=wayland\",");
    char env[160];
    snprintf(env, sizeof(env), "XDG_SESSION_DESKTOP=%s", s->desktop);
    json_string(&json, env);
    json_puts(&json, "]}");
    send_request(REQUEST_START, &json);
}

static void answer_or_prompt(const char *type, const char *text) {
    if (!strcmp(type, "info") || !strcmp(type, "error")) {
        set_message(type, text);
        request_respond(NULL);
        return;
    }
    if (g.has_pending) {
        request_respond(g.pending);
        wipe(g.pending, sizeof(g.pending));
        g.has_pending = false;
        return;
    }
    api->set_value(host, "greetd.prompt", text);
    api->set_value(host, "greetd.secret", strcmp(type, "visible") ? "1" : "0");
    set_state("prompt");
}

static void handle_response(const struct json_object *msg) {
    enum request was = g.inflight;
    g.inflight = REQUEST_NONE;
    const char *type = json_get_string(msg, "type");
    if (!type) type = "";
    const char *detail = json_get_string(msg, "auth_message_type");
    if (!detail) detail = json_get_string(msg, "error_type");
    char line[160];
    snprintf(line, sizeof(line), "<- %s%s%s", type, detail ? " " : "", detail ? detail : "");
    api->log(host, line);

    if (was == REQUEST_CANCEL) {
        g.active = false;
        if (g.want_user[0]) {
            char user[sizeof(g.want_user)];
            snprintf(user, sizeof(user), "%s", g.want_user);
            g.want_user[0] = '\0';
            request_create(user);
        } else {
            const char *kind = api->value(host, "greetd.message_kind");
            set_state(kind && !strcmp(kind, "error") ? "error" : "idle");
        }
        return;
    }
    if (!strcmp(type, "success")) {
        if (was == REQUEST_START) finish();
        else request_start();
    } else if (!strcmp(type, "auth_message")) {
        const char *kind = json_get_string(msg, "auth_message_type");
        const char *text = json_get_string(msg, "auth_message");
        answer_or_prompt(kind ? kind : "secret", text ? text : "");
    } else {
        /* An error ends the attempt; greetd wants the session cancelled. */
        const char *text = json_get_string(msg, "description");
        const char *kind = json_get_string(msg, "error_type");
        if (kind && !strcmp(kind, "auth_error")) text = "Wrong password";
        set_message("error", text && *text ? text : "Login failed");
        wipe(g.pending, sizeof(g.pending));
        g.has_pending = false;
        request_cancel();
    }
}

static void readable(int fd, short revents, void *data) {
    (void)data;
    if (revents & POLLIN) {
        unsigned char chunk[4096];
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n > 0 && g.in_len + (size_t)n <= MAX_MESSAGE + 4) {
            unsigned char *in = realloc(g.in, g.in_len + (size_t)n + 1);
            if (in) {
                g.in = in;
                memcpy(g.in + g.in_len, chunk, (size_t)n);
                g.in_len += (size_t)n;
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            revents |= POLLHUP;
        }
    }
    while (g.in_len >= 4) {
        uint32_t len;
        memcpy(&len, g.in, sizeof(len));
        if (len > MAX_MESSAGE) {
            revents |= POLLHUP;
            break;
        }
        if (g.in_len < 4 + (size_t)len) break;
        char *text = malloc((size_t)len + 1);
        if (text) {
            memcpy(text, g.in + 4, len);
            text[len] = '\0';
            struct json_object msg;
            const char *error = NULL;
            if (json_parse_object(text, &msg, &error)) handle_response(&msg);
            else logf_("bad greetd message: %s", error);
            free(text);
        }
        memmove(g.in, g.in + 4 + len, g.in_len - 4 - len);
        g.in_len -= 4 + (size_t)len;
    }
    if (revents & (POLLHUP | POLLERR)) {
        api->watch_remove(host, g.watch);
        g.watch = NULL;
        close(fd);
        g.fd = -1;
        set_message("error", "greetd closed the connection");
        set_state("error");
    }
}

/* ---- the demo backend ------------------------------------------------- */

static void demo_login(void) {
    if (!g.has_pending) {
        api->set_value(host, "greetd.prompt", "Password:");
        api->set_value(host, "greetd.secret", "1");
        set_state("prompt");
        return;
    }
    bool ok = g.pending[0] != '\0';
    wipe(g.pending, sizeof(g.pending));
    g.has_pending = false;
    if (ok) finish();
    else {
        set_message("error", "Wrong password");
        set_state("error");
    }
}

/* ---- actions ---------------------------------------------------------- */

static void action_session(const char *id, void *data) {
    (void)data;
    for (size_t i = 0; i < g.session_count; i++) {
        if (!strcmp(g.sessions[i].id, id)) {
            g.session = (int)i;
            return;
        }
    }
    logf_("unknown session '%s'", id);
}

static void action_login(const char *user, void *data) {
    (void)data;
    if (!*user) return;
    set_message("info", "");
    api->set_value(host, "greetd.prompt", "");
    if (g.demo) {
        snprintf(g.user, sizeof(g.user), "%s", user);
        demo_login();
        return;
    }
    if (g.fd < 0) {
        set_message("error", "greetd is not running (GREETD_SOCK is unset)");
        set_state("error");
        return;
    }
    if (g.inflight != REQUEST_NONE) return;
    if (g.active) {
        snprintf(g.want_user, sizeof(g.want_user), "%s", user);
        request_cancel();
    } else {
        request_create(user);
    }
}

static void action_respond(const char *answer, void *data) {
    (void)data;
    const char *state = api->value(host, "greetd.state");
    bool prompting = state && !strcmp(state, "prompt");
    if (!prompting || g.inflight != REQUEST_NONE) {
        /* The prompt has not arrived yet: keep the answer for it. */
        snprintf(g.pending, sizeof(g.pending), "%s", answer);
        g.has_pending = true;
        if (g.demo && prompting) demo_login();
        return;
    }
    if (g.demo) {
        snprintf(g.pending, sizeof(g.pending), "%s", answer);
        g.has_pending = true;
        demo_login();
        return;
    }
    request_respond(answer);
}

static void action_cancel(const char *argument, void *data) {
    (void)argument;
    (void)data;
    wipe(g.pending, sizeof(g.pending));
    g.has_pending = false;
    set_message("info", "");
    if (!g.demo && g.active && g.inflight == REQUEST_NONE) request_cancel();
    else set_state("idle");
}

/* ---- plugin ----------------------------------------------------------- */

static bool init(shady_shell_host h, const shady_shell_props *options) {
    (void)h;
    (void)options;
    publish_users();
    publish_sessions();
    find_state_path();
    load_last();
    set_message("info", "");
    api->set_value(host, "greetd.prompt", "");
    api->set_value(host, "greetd.secret", "1");

    const char *demo = getenv("SHADY_GREETER_DEMO");
    g.demo = demo && !strcmp(demo, "1");
    const char *path = getenv("GREETD_SOCK");
    if (!g.demo && path && *path) {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        struct sockaddr_un addr = { .sun_family = AF_UNIX };
        snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            g.fd = fd;
            g.watch = api->watch_add(host, fd, POLLIN, readable, NULL);
        } else {
            logf_("cannot connect to greetd: %s", strerror(errno));
            if (fd >= 0) close(fd);
        }
    }
    set_state("idle");

    api->add_action(host, "greetd.session", action_session, NULL);
    api->add_action(host, "greetd.login", action_login, NULL);
    api->add_action(host, "greetd.respond", action_respond, NULL);
    api->add_action(host, "greetd.cancel", action_cancel, NULL);
    return true;
}

static void destroy(shady_shell_host h) {
    (void)h;
    wipe(g.pending, sizeof(g.pending));
    if (g.fd >= 0) close(g.fd);
    free(g.in);
}

static const struct shady_shell_plugin plugin = {
    .name = "greetd",
    .init = init,
    .destroy = destroy,
};

const struct shady_shell_plugin *shady_shell_plugin_entry_v1(uint32_t host_abi,
        const struct shady_shell_api_v1 *host_api, shady_shell_host host_handle) {
    if (host_abi != SHADY_SHELL_PLUGIN_ABI_V1 || !SHADY_SHELL_API_HAS(host_api, theme_color))
        return NULL;
    api = host_api;
    host = host_handle;
    return &plugin;
}
