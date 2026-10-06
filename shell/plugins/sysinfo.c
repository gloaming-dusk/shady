/*
 * sysinfo: CPU, memory and load for shady-shell, read from /proc.
 *
 *   shell.plugin("sysinfo", { interval = 1000 })
 *   shell.value("sysinfo.cpu")      -- "0".."100", percent busy since the last sample
 *   shell.value("sysinfo.memory")   -- "0".."100", percent of RAM in use
 *   shell.value("sysinfo.load")     -- 1-minute load average, e.g. "0.42"
 *   shell.widget("sysinfo.graph", { width = 60, height = 18,
 *       series = "cpu" | "memory", color = "#RRGGBB" })
 *   shell.action("sysinfo.sample")  -- sample now instead of on the timer
 *
 * The graph draws the last 60 samples as a filled sparkline in `color`
 * (default: the theme accent).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shady/shell_plugin.h>

#define HISTORY 60

static const struct shady_shell_api_v1 *api;
static shady_shell_host host;
static shady_shell_timer timer;
static uint32_t interval_ms = 1000;
static unsigned long long last_busy, last_total;
static float cpu_history[HISTORY], memory_history[HISTORY];
static size_t samples; /* total taken; the newest is at (samples - 1) % HISTORY */

static bool read_cpu(double *percent) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return false;
    unsigned long long v[8] = {0};
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
        &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
    fclose(f);
    if (n < 4) return false;
    unsigned long long idle = v[3] + v[4]; /* idle + iowait */
    unsigned long long total = 0;
    for (int i = 0; i < 8; i++) total += v[i];
    unsigned long long busy = total - idle;
    bool have_previous = last_total != 0 && total > last_total;
    *percent = have_previous
        ? 100.0 * (double)(busy - last_busy) / (double)(total - last_total) : 0.0;
    last_busy = busy;
    last_total = total;
    return have_previous;
}

static bool read_memory(double *percent) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return false;
    char line[128];
    unsigned long long total = 0, available = 0, value;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %llu", &value) == 1) total = value;
        else if (sscanf(line, "MemAvailable: %llu", &value) == 1) available = value;
    }
    fclose(f);
    if (!total) return false;
    *percent = 100.0 * (double)(total - available) / (double)total;
    return true;
}

static void publish(const char *key, const char *format, double v) {
    char text[32];
    snprintf(text, sizeof(text), format, v);
    api->set_value(host, key, text);
}

static void sample(void) {
    double cpu = 0, memory = 0;
    bool have_cpu = read_cpu(&cpu);
    bool have_memory = read_memory(&memory);
    size_t slot = samples % HISTORY;
    cpu_history[slot] = (float)cpu;
    memory_history[slot] = (float)memory;
    samples++;
    if (have_cpu) publish("sysinfo.cpu", "%.0f", cpu);
    if (have_memory) publish("sysinfo.memory", "%.0f", memory);
    FILE *f = fopen("/proc/loadavg", "r");
    double load;
    if (f) {
        if (fscanf(f, "%lf", &load) == 1) publish("sysinfo.load", "%.2f", load);
        fclose(f);
    }
    /* The graph changes even when the rounded values do not. */
    api->redraw(host);
}

static void tick(void *data) {
    (void)data;
    sample();
    timer = api->timer_add(host, interval_ms, tick, NULL);
}

static void sample_action(const char *argument, void *data) {
    (void)argument;
    (void)data;
    sample();
}

static void draw_graph(void *data, cairo_t *cr, double width, double height,
        const shady_shell_props *props) {
    (void)data;
    const char *series = api->prop_string(props, "series");
    const float *history = series && !strcmp(series, "memory") ? memory_history : cpu_history;
    double rgba[4];
    if (!api->prop_color(props, "color", rgba) && !api->theme_color(host, "accent", rgba)) {
        rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1.0;
    }
    size_t count = samples < HISTORY ? samples : HISTORY;
    if (count < 2) return;
    double step = width / (double)(HISTORY - 1);
    double x0 = width - step * (double)(count - 1);
    cairo_new_path(cr);
    for (size_t i = 0; i < count; i++) {
        size_t slot = (samples - count + i) % HISTORY;
        double v = history[slot] / 100.0;
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        double x = x0 + step * (double)i;
        double y = height - 1 - v * (height - 2);
        if (i == 0) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_path_t *line = cairo_copy_path(cr);
    cairo_line_to(cr, width, height);
    cairo_line_to(cr, x0, height);
    cairo_close_path(cr);
    cairo_pattern_t *fill = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(fill, 0, rgba[0], rgba[1], rgba[2], 0.35 * rgba[3]);
    cairo_pattern_add_color_stop_rgba(fill, 1, rgba[0], rgba[1], rgba[2], 0.0);
    cairo_set_source(cr, fill);
    cairo_fill(cr);
    cairo_pattern_destroy(fill);
    cairo_new_path(cr);
    cairo_append_path(cr, line);
    cairo_path_destroy(line);
    cairo_set_line_width(cr, 1.2);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
    cairo_stroke(cr);
}

static bool init(shady_shell_host h, const shady_shell_props *options) {
    (void)h;
    double interval = api->prop_number(options, "interval", 1000);
    interval_ms = interval < 200 ? 200 : (uint32_t)interval;
    static const struct shady_shell_widget_type graph = {
        .name = "sysinfo.graph",
        .draw = draw_graph,
    };
    if (!api->add_widget(host, &graph) ||
            !api->add_action(host, "sysinfo.sample", sample_action, NULL))
        return false;
    sample();
    timer = api->timer_add(host, interval_ms, tick, NULL);
    return timer != NULL;
}

static void destroy(shady_shell_host h) {
    (void)h;
    /* The shell releases the timer, values, action and widget for us. */
    timer = NULL;
    samples = 0;
    last_busy = last_total = 0;
}

static const struct shady_shell_plugin plugin = {
    .name = "sysinfo",
    .init = init,
    .destroy = destroy,
};

const struct shady_shell_plugin *shady_shell_plugin_entry_v1(uint32_t host_abi,
        const struct shady_shell_api_v1 *host_api, shady_shell_host host_handle) {
    if (host_abi != SHADY_SHELL_PLUGIN_ABI_V1 || !host_api ||
            host_api->abi_version != SHADY_SHELL_PLUGIN_ABI_V1 ||
            !SHADY_SHELL_API_HAS(host_api, theme_color))
        return NULL;
    api = host_api;
    host = host_handle;
    return &plugin;
}
