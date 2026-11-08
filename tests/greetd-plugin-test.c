/* The greetd plugin against a scripted greetd: whether an answer is hidden
 * must not outlive the prompt that asked for it to be shown. */
#define _DEFAULT_SOURCE

#include <assert.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <shady/shell_plugin.h>

const struct shady_shell_plugin *shady_shell_plugin_entry_v1(uint32_t host_abi,
    const struct shady_shell_api_v1 *host_api, shady_shell_host host_handle);

static struct { char key[64]; char value[1024]; } values[32];
static size_t value_count;
static struct { char name[64]; shady_shell_action_fn fn; void *data; } actions[8];
static size_t action_count;
static shady_shell_watch_fn watch_fn;
static void *watch_data;
static int watch_fd = -1;
static int server = -1;

static void api_log(shady_shell_host h, const char *message) {
    (void)h;
    (void)message;
}

static const char *api_value(shady_shell_host h, const char *key) {
    (void)h;
    for (size_t i = 0; i < value_count; i++)
        if (!strcmp(values[i].key, key)) return values[i].value;
    return NULL;
}

static bool api_set_value(shady_shell_host h, const char *key, const char *value) {
    (void)h;
    size_t i = 0;
    while (i < value_count && strcmp(values[i].key, key)) i++;
    assert(i < sizeof(values) / sizeof(values[0]));
    if (i == value_count) value_count++;
    snprintf(values[i].key, sizeof(values[i].key), "%s", key);
    snprintf(values[i].value, sizeof(values[i].value), "%s", value);
    return true;
}

static bool api_add_action(shady_shell_host h, const char *name,
        shady_shell_action_fn fn, void *data) {
    (void)h;
    snprintf(actions[action_count].name, sizeof(actions[0].name), "%s", name);
    actions[action_count].fn = fn;
    actions[action_count++].data = data;
    return true;
}

static shady_shell_watch api_watch_add(shady_shell_host h, int fd, short events,
        shady_shell_watch_fn fn, void *data) {
    (void)h;
    (void)events;
    watch_fd = fd;
    watch_fn = fn;
    watch_data = data;
    return (shady_shell_watch)&watch_fd;
}

static void api_watch_remove(shady_shell_host h, shady_shell_watch watch) {
    (void)h;
    (void)watch;
}

static bool api_theme_color(shady_shell_host h, const char *name, double rgba[4]) {
    (void)h;
    (void)name;
    (void)rgba;
    return false;
}

static const struct shady_shell_api_v1 api = {
    .abi_version = SHADY_SHELL_PLUGIN_ABI_V1,
    .struct_size = sizeof(struct shady_shell_api_v1),
    .log = api_log,
    .set_value = api_set_value,
    .value = api_value,
    .add_action = api_add_action,
    .watch_add = api_watch_add,
    .watch_remove = api_watch_remove,
    .theme_color = api_theme_color,
};

static void action(const char *name, const char *argument) {
    for (size_t i = 0; i < action_count; i++) {
        if (strcmp(actions[i].name, name)) continue;
        actions[i].fn(argument, actions[i].data);
        return;
    }
    assert(!"no such action");
}

static void expect_request(const char *type) {
    uint32_t len = 0;
    assert(recv(server, &len, sizeof(len), MSG_WAITALL) == (ssize_t)sizeof(len));
    char *body = calloc(1, (size_t)len + 1);
    assert(body && recv(server, body, len, MSG_WAITALL) == (ssize_t)len);
    if (!strstr(body, type)) {
        fprintf(stderr, "expected %s, got %s\n", type, body);
        abort();
    }
    free(body);
}

/* Send greetd's answer and let the plugin read it. */
static void reply(const char *json) {
    uint32_t len = (uint32_t)strlen(json);
    assert(send(server, &len, sizeof(len), 0) == (ssize_t)sizeof(len));
    assert(send(server, json, len, 0) == (ssize_t)len);
    watch_fn(watch_fd, POLLIN, watch_data);
}

static void expect_value(const char *key, const char *want) {
    const char *got = api_value(NULL, key);
    if (!got || strcmp(got, want)) {
        fprintf(stderr, "%s: wanted '%s', got '%s'\n", key, want, got ? got : "(unset)");
        abort();
    }
}

static const char visible[] =
    "{\"type\":\"auth_message\",\"auth_message_type\":\"visible\",\"auth_message\":\"Code:\"}";
static const char success[] = "{\"type\":\"success\"}";

int main(void) {
    char dir[] = "/tmp/shady-greetd-test-XXXXXX";
    assert(mkdtemp(dir));
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/sock", dir);
    char state[sizeof(dir) + 16];
    snprintf(state, sizeof(state), "%s/last", dir);

    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(listener >= 0 && bind(listener, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(listener, 1) == 0);
    setenv("GREETD_SOCK", addr.sun_path, 1);
    setenv("SHADY_GREETER_SESSIONS", dir, 1);
    setenv("SHADY_GREETER_STATE", state, 1);
    unsetenv("SHADY_GREETER_DEMO");

    const struct shady_shell_plugin *plugin =
        shady_shell_plugin_entry_v1(SHADY_SHELL_PLUGIN_ABI_V1, &api, NULL);
    assert(plugin && plugin->init(NULL, NULL));
    server = accept(listener, NULL, NULL);
    assert(server >= 0);
    expect_value("greetd.secret", "1");

    /* Cancelled at a visible prompt. */
    action("greetd.login", "alice");
    expect_request("create_session");
    reply(visible);
    expect_value("greetd.state", "prompt");
    expect_value("greetd.secret", "0");
    action("greetd.cancel", "");
    expect_value("greetd.secret", "1");
    expect_value("greetd.prompt", "");
    expect_request("cancel_session");
    reply(success);
    expect_value("greetd.state", "idle");

    /* Refused after answering a visible prompt. */
    action("greetd.login", "alice");
    expect_request("create_session");
    reply(visible);
    expect_value("greetd.secret", "0");
    action("greetd.respond", "123456");
    expect_request("post_auth_message_response");
    reply("{\"type\":\"error\",\"error_type\":\"auth_error\",\"description\":\"no\"}");
    expect_value("greetd.secret", "1");
    expect_request("cancel_session");
    reply(success);
    expect_value("greetd.state", "error");

    /* A new attempt starts hidden, even straight from a visible prompt. */
    action("greetd.login", "alice");
    expect_request("create_session");
    reply(visible);
    expect_value("greetd.secret", "0");
    action("greetd.login", "bob");
    expect_value("greetd.secret", "1");
    expect_request("cancel_session");
    reply(success);
    expect_request("create_session");

    plugin->destroy(NULL);
    close(server);
    close(listener);
    unlink(addr.sun_path);
    rmdir(dir);
    puts("greetd-plugin: PASS");
    return 0;
}
