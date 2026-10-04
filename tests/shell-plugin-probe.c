/* Shell plugin used by tests/headless-shell-plugin.sh. It touches every
 * entry of the API and reports through values:
 *   probe.greeting  the "greeting" option
 *   probe.pipe      "tick", read by an fd watch from a pipe a timer wrote to
 *   probe.unwatched "yes" once the watch removed itself from its callback
 *   probe.cancel    "fired" only if a cancelled timer ran (must not appear)
 *   probe.echo      the argument of the last probe.echo action
 * and draws widget "probe.box" filled with its "color" property. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#include <shady/shell_plugin.h>

static const struct shady_shell_api_v1 *api;
static shady_shell_host host;
static int fds[2] = { -1, -1 };
static shady_shell_watch watch;

static void readable(int fd, short revents, void *data) {
    (void)revents;
    (void)data;
    char buffer[16] = {0};
    ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) return;
    buffer[strcspn(buffer, "\n")] = '\0';
    api->set_value(host, "probe.pipe", buffer);
    api->watch_remove(host, watch);
    watch = NULL;
    api->set_value(host, "probe.unwatched", "yes");
}

static void write_tick(void *data) {
    (void)data;
    if (write(fds[1], "tick\n", 5) != 5) api->log(host, "pipe write failed");
}

static void must_not_fire(void *data) {
    (void)data;
    api->set_value(host, "probe.cancel", "fired");
}

static void echo(const char *argument, void *data) {
    (void)data;
    api->set_value(host, "probe.echo", argument);
}

static void draw_box(void *data, cairo_t *cr, double width, double height,
        const shady_shell_props *props) {
    (void)data;
    double rgba[4] = { 1, 0, 0, 1 };
    api->prop_color(props, "color", rgba);
    cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
    /* Larger than the box: the shell must clip it. */
    cairo_rectangle(cr, -50, -50, width + 100, height + 100);
    cairo_fill(cr);
}

static bool init(shady_shell_host h, const shady_shell_props *options) {
    (void)h;
    api->log(host, "init");
    const char *greeting = api->prop_string(options, "greeting");
    api->set_value(host, "probe.greeting", greeting ? greeting : "none");
    if (pipe(fds) < 0) return false;
    watch = api->watch_add(host, fds[0], POLLIN, readable, NULL);
    shady_shell_timer cancelled = api->timer_add(host, 50, must_not_fire, NULL);
    api->timer_cancel(host, cancelled);
    static const struct shady_shell_widget_type box = { .name = "probe.box", .draw = draw_box };
    return watch && api->timer_add(host, 100, write_tick, NULL) &&
        api->add_action(host, "probe.echo", echo, NULL) && api->add_widget(host, &box) &&
        !api->add_widget(host, &box); /* a second registration is refused */
}

static void destroy(shady_shell_host h) {
    (void)h;
    if (fds[0] >= 0) close(fds[0]);
    if (fds[1] >= 0) close(fds[1]);
    fds[0] = fds[1] = -1;
    api->log(host, "destroyed");
}

static const struct shady_shell_plugin plugin = {
    .name = "probe",
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
