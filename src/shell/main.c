#define _POSIX_C_SOURCE 200809L

#include <cairo/cairo.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "shady-shell-v1-client-protocol.h"

#define BAR_HEIGHT 32
#define MAX_WORKSPACES 16
#define WORKSPACE_NAME_MAX 64
#define WINDOW_TEXT_MAX 256
#define MAX_APPS 512
#define APP_NAME_MAX 128
#define APP_EXEC_MAX 512
#define LAUNCHER_WIDTH 640
#define LAUNCHER_HEIGHT 420
#define LAUNCHER_RESULTS 8
#define SEARCH_MAX 128

struct shell_buffer {
    struct wl_buffer *wl_buffer;
    void *data;
    size_t size;
};

struct workspace_region {
    double x0;
    double x1;
};

struct launcher_app {
    char name[APP_NAME_MAX];
    char exec[APP_EXEC_MAX];
};

struct shell {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct shady_shell_v1 *shady_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wl_surface *launcher_surface;
    struct zwlr_layer_surface_v1 *launcher_layer_surface;

    uint32_t width;
    uint32_t height;
    bool configured;
    bool running;
    bool snapshot_done;

    char workspaces[MAX_WORKSPACES][WORKSPACE_NAME_MAX];
    struct workspace_region workspace_regions[MAX_WORKSPACES];
    size_t workspace_count;
    char active_workspace[WORKSPACE_NAME_MAX];
    char focused_app_id[WINDOW_TEXT_MAX];
    char focused_title[WINDOW_TEXT_MAX];

    double pointer_x;
    double pointer_y;
    struct wl_surface *pointer_surface;

    struct launcher_app apps[MAX_APPS];
    size_t app_count;
    bool launcher_visible;
    bool launcher_configured;
    uint32_t launcher_width;
    uint32_t launcher_height;
    char search[SEARCH_MAX];
    size_t selected_result;
};

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static bool ascii_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !*needle) return true;
    if (!haystack) return false;
    size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] &&
                (char)tolower((unsigned char)p[i]) ==
                (char)tolower((unsigned char)needle[i])) i++;
        if (i == n) return true;
    }
    return false;
}

static void sanitize_exec(char *dst, size_t dst_size, const char *src) {
    size_t out = 0;
    for (size_t i = 0; src && src[i] && out + 1 < dst_size; i++) {
        if (src[i] != '%') {
            dst[out++] = src[i];
            continue;
        }
        if (src[i + 1] == '%') {
            dst[out++] = '%';
            i++;
            continue;
        }
        if (src[i + 1]) i++;
    }
    while (out > 0 && (dst[out - 1] == ' ' || dst[out - 1] == '\t')) out--;
    dst[out] = '\0';
}

static bool desktop_truthy(const char *value) {
    return value && (!strcasecmp(value, "true") || !strcmp(value, "1"));
}

static void launcher_add_desktop_file(struct shell *shell, const char *path) {
    if (shell->app_count >= MAX_APPS) return;
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024], name[APP_NAME_MAX] = {0}, exec[APP_EXEC_MAX] = {0};
    bool in_entry = false, hidden = false, nodisplay = false, application = true;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (line[0] == '[') {
            in_entry = strcmp(line, "[Desktop Entry]") == 0;
            continue;
        }
        if (!in_entry || line[0] == '#' || !line[0]) continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = '\0';
        if (!strcmp(line, "Name")) copy_text(name, sizeof(name), eq);
        else if (!strcmp(line, "Exec")) copy_text(exec, sizeof(exec), eq);
        else if (!strcmp(line, "Hidden")) hidden = desktop_truthy(eq);
        else if (!strcmp(line, "NoDisplay")) nodisplay = desktop_truthy(eq);
        else if (!strcmp(line, "Type")) application = strcmp(eq, "Application") == 0;
    }
    fclose(f);
    if (!application || hidden || nodisplay || !name[0] || !exec[0]) return;

    struct launcher_app *app = &shell->apps[shell->app_count];
    copy_text(app->name, sizeof(app->name), name);
    sanitize_exec(app->exec, sizeof(app->exec), exec);
    if (!app->exec[0]) return;
    shell->app_count++;
}

static void launcher_scan_dir(struct shell *shell, const char *base) {
    if (!base || !*base) return;
    char path[4096];
    snprintf(path, sizeof(path), "%s/applications", base);
    DIR *dir = opendir(path);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && shell->app_count < MAX_APPS) {
        size_t len = strlen(entry->d_name);
        if (len < 9 || strcmp(entry->d_name + len - 8, ".desktop") != 0) continue;
        char file[4096];
        int written = snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(file)) continue;
        launcher_add_desktop_file(shell, file);
    }
    closedir(dir);
}

static void launcher_load_apps(struct shell *shell) {
    const char *home = getenv("HOME");
    const char *xdg_home = getenv("XDG_DATA_HOME");
    char local[4096];
    if (xdg_home && *xdg_home) launcher_scan_dir(shell, xdg_home);
    else if (home && *home) {
        snprintf(local, sizeof(local), "%s/.local/share", home);
        launcher_scan_dir(shell, local);
    }

    const char *dirs = getenv("XDG_DATA_DIRS");
    if (!dirs || !*dirs) dirs = "/usr/local/share:/usr/share";
    char *copy = strdup(dirs);
    if (copy) {
        char *save = NULL;
        for (char *p = strtok_r(copy, ":", &save); p; p = strtok_r(NULL, ":", &save))
            launcher_scan_dir(shell, p);
        free(copy);
    }
    fprintf(stderr, "shady-shell: indexed %zu applications\n", shell->app_count);
}

static size_t launcher_matching_indices(struct shell *shell,
        size_t out[LAUNCHER_RESULTS]) {
    size_t count = 0;
    for (size_t i = 0; i < shell->app_count && count < LAUNCHER_RESULTS; i++) {
        if (ascii_contains_ci(shell->apps[i].name, shell->search) ||
                ascii_contains_ci(shell->apps[i].exec, shell->search)) {
            out[count++] = i;
        }
    }
    return count;
}

static void launcher_spawn(const struct launcher_app *app) {
    if (!app || !app->exec[0]) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-lc", app->exec, (char *)NULL);
        _exit(127);
    }
}

static int create_shm_file(size_t size) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) {
        fprintf(stderr, "shady-shell: XDG_RUNTIME_DIR is not set\n");
        return -1;
    }

    char path[4096];
    int n = snprintf(path, sizeof(path), "%s/shady-shell-XXXXXX", runtime);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;

    int fd = mkstemp(path);
    if (fd < 0) {
        perror("shady-shell: mkstemp");
        return -1;
    }
    unlink(path);

    if (ftruncate(fd, (off_t)size) < 0) {
        perror("shady-shell: ftruncate");
        close(fd);
        return -1;
    }
    return fd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    struct shell_buffer *buffer = data;
    wl_buffer_destroy(wl_buffer);
    munmap(buffer->data, buffer->size);
    free(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static struct shell_buffer *create_buffer(struct shell *shell,
        uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return NULL;

    const int stride = (int)width * 4;
    const size_t size = (size_t)stride * height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("shady-shell: mmap");
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shell->shm, fd, (int)size);
    close(fd);
    if (!pool) {
        munmap(data, size);
        return NULL;
    }

    struct shell_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        wl_shm_pool_destroy(pool);
        munmap(data, size);
        return NULL;
    }

    buffer->data = data;
    buffer->size = size;
    buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
        (int)width, (int)height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    if (!buffer->wl_buffer) {
        munmap(data, size);
        free(buffer);
        return NULL;
    }

    wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
    return buffer;
}

static PangoLayout *make_layout(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(
        bold ? "Sans Bold 10" : "Sans 10");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_font_description_free(font);
    return layout;
}

static int text_width(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = make_layout(cr, text, bold);
    int width = 0;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

static void draw_layout(cairo_t *cr, PangoLayout *layout,
        double x, double y, double r, double g, double b) {
    cairo_set_source_rgba(cr, r, g, b, 1.0);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

static void draw_text(cairo_t *cr, const char *text,
        double x, double y, bool bold) {
    PangoLayout *layout = make_layout(cr, text, bold);
    draw_layout(cr, layout, x, y, 0.91, 0.94, 0.98);
    g_object_unref(layout);
}

static void draw_bar(struct shell *shell) {
    if (!shell->configured || shell->width == 0 || shell->height == 0) return;

    struct shell_buffer *buffer =
        create_buffer(shell, shell->width, shell->height);
    if (!buffer) return;

    cairo_surface_t *image = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        (int)shell->width, (int)shell->height, (int)shell->width * 4);
    cairo_t *cr = cairo_create(image);

    cairo_set_source_rgba(cr, 0.025, 0.035, 0.065, 0.96);
    cairo_paint(cr);

    cairo_set_source_rgba(cr, 0.09, 0.24, 0.35, 0.9);
    cairo_rectangle(cr, 0, shell->height - 1, shell->width, 1);
    cairo_fill(cr);

    draw_text(cr, "Shady", 10, 8, true);

    double x = 72.0;
    for (size_t i = 0; i < shell->workspace_count; i++) {
        const char *name = shell->workspaces[i];
        bool active = strcmp(name, shell->active_workspace) == 0;
        int label_width = text_width(cr, name, active);
        double box_width = label_width + 18.0;

        shell->workspace_regions[i].x0 = x;
        shell->workspace_regions[i].x1 = x + box_width;

        if (active) {
            cairo_set_source_rgba(cr, 0.08, 0.26, 0.39, 0.95);
            cairo_rectangle(cr, x, 4, box_width, shell->height - 8);
            cairo_fill(cr);
        }

        draw_text(cr, name, x + 9, 8, active);
        x += box_width + 4.0;
    }

    const char *focused = shell->focused_title[0]
        ? shell->focused_title : shell->focused_app_id;
    if (focused[0]) {
        PangoLayout *layout = make_layout(cr, focused, false);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        int available = (int)shell->width - (int)x - 130;
        if (available > 80) {
            pango_layout_set_width(layout, available * PANGO_SCALE);
            draw_layout(cr, layout, x + 16, 8, 0.72, 0.79, 0.88);
        }
        g_object_unref(layout);
    }

    char clock_text[64] = {0};
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(clock_text, sizeof(clock_text), "%H:%M", &local);

    PangoLayout *clock = make_layout(cr, clock_text, false);
    int clock_width = 0;
    int clock_height = 0;
    pango_layout_get_pixel_size(clock, &clock_width, &clock_height);
    draw_layout(cr, clock, shell->width - clock_width - 10,
        (shell->height - clock_height) / 2.0, 0.80, 0.86, 0.93);
    g_object_unref(clock);

    cairo_destroy(cr);
    cairo_surface_flush(image);
    cairo_surface_destroy(image);

    wl_surface_attach(shell->surface, buffer->wl_buffer, 0, 0);
    wl_surface_damage_buffer(shell->surface, 0, 0,
        (int)shell->width, (int)shell->height);
    wl_surface_commit(shell->surface);
}

static void draw_launcher(struct shell *shell) {
    if (!shell->launcher_visible || !shell->launcher_configured ||
            !shell->launcher_surface || shell->launcher_width == 0 ||
            shell->launcher_height == 0) return;

    struct shell_buffer *buffer = create_buffer(shell,
        shell->launcher_width, shell->launcher_height);
    if (!buffer) return;

    cairo_surface_t *image = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        (int)shell->launcher_width, (int)shell->launcher_height,
        (int)shell->launcher_width * 4);
    cairo_t *cr = cairo_create(image);

    cairo_set_source_rgba(cr, 0.018, 0.026, 0.050, 0.98);
    cairo_paint(cr);
    cairo_set_source_rgba(cr, 0.08, 0.25, 0.38, 0.95);
    cairo_rectangle(cr, 0, 0, shell->launcher_width, 2);
    cairo_fill(cr);

    char search_line[SEARCH_MAX + 8];
    snprintf(search_line, sizeof(search_line), "> %s", shell->search);
    draw_text(cr, search_line, 24, 20, true);

    size_t matches[LAUNCHER_RESULTS];
    size_t count = launcher_matching_indices(shell, matches);
    if (count == 0) shell->selected_result = 0;
    else if (shell->selected_result >= count) shell->selected_result = count - 1;

    double y = 66.0;
    for (size_t i = 0; i < count; i++) {
        const struct launcher_app *app = &shell->apps[matches[i]];
        if (i == shell->selected_result) {
            cairo_set_source_rgba(cr, 0.07, 0.22, 0.34, 0.96);
            cairo_rectangle(cr, 16, y - 6, shell->launcher_width - 32, 34);
            cairo_fill(cr);
        }
        draw_text(cr, app->name, 28, y, i == shell->selected_result);
        y += 40.0;
    }

    if (count == 0) {
        draw_text(cr, "No applications found", 28, 70, false);
    }

    cairo_destroy(cr);
    cairo_surface_flush(image);
    cairo_surface_destroy(image);

    wl_surface_attach(shell->launcher_surface, buffer->wl_buffer, 0, 0);
    wl_surface_damage_buffer(shell->launcher_surface, 0, 0,
        (int)shell->launcher_width, (int)shell->launcher_height);
    wl_surface_commit(shell->launcher_surface);
}

static void launcher_hide(struct shell *shell) {
    if (shell->launcher_layer_surface) {
        zwlr_layer_surface_v1_destroy(shell->launcher_layer_surface);
        shell->launcher_layer_surface = NULL;
    }
    if (shell->launcher_surface) {
        wl_surface_destroy(shell->launcher_surface);
        shell->launcher_surface = NULL;
    }
    shell->launcher_visible = false;
    shell->launcher_configured = false;
    shell->search[0] = '\0';
    shell->selected_result = 0;
}

static void launcher_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct shell *shell = data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    shell->launcher_width = width ? width : LAUNCHER_WIDTH;
    shell->launcher_height = height ? height : LAUNCHER_HEIGHT;
    shell->launcher_configured = true;
    fprintf(stderr, "shady-shell: launcher configured %ux%u\n",
        shell->launcher_width, shell->launcher_height);
    draw_launcher(shell);
}

static void launcher_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    launcher_hide(data);
}

static const struct zwlr_layer_surface_v1_listener launcher_layer_listener = {
    .configure = launcher_configure,
    .closed = launcher_closed,
};

static void launcher_show(struct shell *shell) {
    if (shell->launcher_visible || !shell->layer_shell || !shell->compositor)
        return;
    shell->launcher_surface = wl_compositor_create_surface(shell->compositor);
    if (!shell->launcher_surface) return;
    shell->launcher_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        shell->layer_shell, shell->launcher_surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "shady-launcher");
    if (!shell->launcher_layer_surface) {
        wl_surface_destroy(shell->launcher_surface);
        shell->launcher_surface = NULL;
        return;
    }
    zwlr_layer_surface_v1_add_listener(shell->launcher_layer_surface,
        &launcher_layer_listener, shell);
    zwlr_layer_surface_v1_set_size(shell->launcher_layer_surface,
        LAUNCHER_WIDTH, LAUNCHER_HEIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(shell->launcher_layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(shell->launcher_layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);
    shell->launcher_visible = true;
    shell->launcher_configured = false;
    shell->search[0] = '\0';
    shell->selected_result = 0;
    wl_surface_commit(shell->launcher_surface);
}

static void launcher_toggle(struct shell *shell) {
    if (shell->launcher_visible) launcher_hide(shell);
    else launcher_show(shell);
}

static void launcher_activate_selected(struct shell *shell) {
    size_t matches[LAUNCHER_RESULTS];
    size_t count = launcher_matching_indices(shell, matches);
    if (count == 0 || shell->selected_result >= count) return;
    launcher_spawn(&shell->apps[matches[shell->selected_result]]);
    launcher_hide(shell);
}

static bool add_workspace(struct shell *shell, const char *name) {
    if (!name || !*name) return false;
    for (size_t i = 0; i < shell->workspace_count; i++) {
        if (strcmp(shell->workspaces[i], name) == 0) return false;
    }
    if (shell->workspace_count >= MAX_WORKSPACES) return false;
    copy_text(shell->workspaces[shell->workspace_count],
        sizeof(shell->workspaces[shell->workspace_count]), name);
    shell->workspace_count++;
    return true;
}

static void protocol_workspace(void *data,
        struct shady_shell_v1 *protocol, const char *name) {
    (void)protocol;
    struct shell *shell = data;
    bool changed = add_workspace(shell, name);
    if (changed && shell->snapshot_done) draw_bar(shell);
}

static void protocol_active_workspace(void *data,
        struct shady_shell_v1 *protocol, const char *name) {
    (void)protocol;
    struct shell *shell = data;
    add_workspace(shell, name);
    copy_text(shell->active_workspace, sizeof(shell->active_workspace), name);
    if (shell->snapshot_done) draw_bar(shell);
}

static void protocol_focused_window(void *data,
        struct shady_shell_v1 *protocol, const char *app_id, const char *title) {
    (void)protocol;
    struct shell *shell = data;
    copy_text(shell->focused_app_id, sizeof(shell->focused_app_id), app_id);
    copy_text(shell->focused_title, sizeof(shell->focused_title), title);
    if (shell->snapshot_done) draw_bar(shell);
}

static void protocol_toggle_launcher(void *data, struct shady_shell_v1 *protocol) {
    (void)protocol;
    launcher_toggle(data);
}

static void protocol_done(void *data, struct shady_shell_v1 *protocol) {
    (void)protocol;
    struct shell *shell = data;
    shell->snapshot_done = true;
    fprintf(stderr, "shady-shell: state workspaces=%zu active=%s focus=%s\n",
        shell->workspace_count,
        shell->active_workspace[0] ? shell->active_workspace : "<none>",
        shell->focused_title[0] ? shell->focused_title :
            (shell->focused_app_id[0] ? shell->focused_app_id : "<none>"));
    draw_bar(shell);
}

static const struct shady_shell_v1_listener shady_shell_listener = {
    .workspace = protocol_workspace,
    .active_workspace = protocol_active_workspace,
    .focused_window = protocol_focused_window,
    .toggle_launcher = protocol_toggle_launcher,
    .done = protocol_done,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface,
        wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)serial;
    struct shell *shell = data;
    shell->pointer_surface = surface;
    shell->pointer_x = wl_fixed_to_double(surface_x);
    shell->pointer_y = wl_fixed_to_double(surface_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    struct shell *shell = data;
    shell->pointer_surface = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
        uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)time;
    struct shell *shell = data;
    shell->pointer_x = wl_fixed_to_double(surface_x);
    shell->pointer_y = wl_fixed_to_double(surface_y);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
        uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    (void)serial;
    (void)time;
    struct shell *shell = data;
    if (!shell->shady_shell || shell->pointer_surface != shell->surface ||
            button != BTN_LEFT ||
            state != WL_POINTER_BUTTON_STATE_PRESSED ||
            shell->pointer_y < 0 || shell->pointer_y >= shell->height) {
        return;
    }

    for (size_t i = 0; i < shell->workspace_count; i++) {
        if (shell->pointer_x >= shell->workspace_regions[i].x0 &&
                shell->pointer_x < shell->workspace_regions[i].x1) {
            shady_shell_v1_activate_workspace(shell->shady_shell,
                shell->workspaces[i]);
            wl_display_flush(shell->display);
            return;
        }
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
        uint32_t time, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer,
        uint32_t axis_source) {
    (void)data; (void)pointer; (void)axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
        uint32_t time, uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer,
        uint32_t axis, int32_t discrete) {
    (void)data; (void)pointer; (void)axis; (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int32_t fd, uint32_t size) {
    (void)keyboard;
    struct shell *shell = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;

    if (shell->xkb_state) xkb_state_unref(shell->xkb_state);
    if (shell->xkb_keymap) xkb_keymap_unref(shell->xkb_keymap);
    shell->xkb_keymap = xkb_keymap_new_from_string(shell->xkb_context, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    shell->xkb_state = shell->xkb_keymap ? xkb_state_new(shell->xkb_keymap) : NULL;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    (void)data; (void)keyboard; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface) {
    (void)data; (void)keyboard; (void)serial; (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard; (void)serial; (void)time;
    struct shell *shell = data;
    if (!shell->launcher_visible || !shell->xkb_state ||
            state != WL_KEYBOARD_KEY_STATE_PRESSED) return;

    xkb_keycode_t code = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(shell->xkb_state, code);
    if (sym == XKB_KEY_Escape) {
        launcher_hide(shell);
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        launcher_activate_selected(shell);
        return;
    }
    if (sym == XKB_KEY_Up) {
        if (shell->selected_result > 0) shell->selected_result--;
        draw_launcher(shell);
        return;
    }
    if (sym == XKB_KEY_Down) {
        size_t matches[LAUNCHER_RESULTS];
        size_t count = launcher_matching_indices(shell, matches);
        if (count && shell->selected_result + 1 < count) shell->selected_result++;
        draw_launcher(shell);
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        size_t len = strlen(shell->search);
        if (len) {
            do { len--; } while (len && ((unsigned char)shell->search[len] & 0xC0) == 0x80);
            shell->search[len] = '\0';
            shell->selected_result = 0;
            draw_launcher(shell);
        }
        return;
    }

    char text[32] = {0};
    int n = xkb_state_key_get_utf8(shell->xkb_state, code, text, sizeof(text));
    if (n <= 0 || (unsigned char)text[0] < 0x20) return;
    size_t len = strlen(shell->search);
    if (len + (size_t)n >= sizeof(shell->search)) return;
    memcpy(shell->search + len, text, (size_t)n);
    shell->search[len + (size_t)n] = '\0';
    shell->selected_result = 0;
    draw_launcher(shell);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t depressed, uint32_t latched,
        uint32_t locked, uint32_t group) {
    (void)keyboard; (void)serial;
    struct shell *shell = data;
    if (shell->xkb_state)
        xkb_state_update_mask(shell->xkb_state, depressed, latched, locked,
            0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
        int32_t rate, int32_t delay) {
    (void)data; (void)keyboard; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities) {
    struct shell *shell = data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !shell->pointer) {
        shell->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(shell->pointer, &pointer_listener, shell);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && shell->pointer) {
        wl_pointer_release(shell->pointer);
        shell->pointer = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !shell->keyboard) {
        shell->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(shell->keyboard, &keyboard_listener, shell);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && shell->keyboard) {
        wl_keyboard_release(shell->keyboard);
        shell->keyboard = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void layer_surface_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct shell *shell = data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    shell->width = width;
    shell->height = height > 0 ? height : BAR_HEIGHT;
    shell->configured = true;
    fprintf(stderr, "shady-shell: configured %ux%u (exclusive=%d)\n",
        shell->width, shell->height, BAR_HEIGHT);
    draw_bar(shell);
}

static void layer_surface_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    struct shell *shell = data;
    shell->running = false;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void registry_global(void *data, struct wl_registry *registry,
        uint32_t name, const char *interface, uint32_t version) {
    struct shell *shell = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t bind_version = version < 4 ? version : 4;
        shell->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, bind_version);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shell->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t bind_version = version < 7 ? version : 7;
        shell->seat = wl_registry_bind(registry, name,
            &wl_seat_interface, bind_version);
        wl_seat_add_listener(shell->seat, &seat_listener, shell);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        uint32_t bind_version = version < 4 ? version : 4;
        shell->layer_shell = wl_registry_bind(registry, name,
            &zwlr_layer_shell_v1_interface, bind_version);
    } else if (strcmp(interface, shady_shell_v1_interface.name) == 0) {
        shell->shady_shell = wl_registry_bind(registry, name,
            &shady_shell_v1_interface, 1);
        shady_shell_v1_add_listener(shell->shady_shell,
            &shady_shell_listener, shell);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
        uint32_t name) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

static bool shell_init(struct shell *shell) {
    shell->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!shell->xkb_context) return false;
    launcher_load_apps(shell);

    shell->display = wl_display_connect(NULL);
    if (!shell->display) {
        fprintf(stderr, "shady-shell: failed to connect to Wayland display\n");
        return false;
    }

    shell->registry = wl_display_get_registry(shell->display);
    wl_registry_add_listener(shell->registry, &registry_listener, shell);
    if (wl_display_roundtrip(shell->display) < 0) return false;

    if (!shell->compositor || !shell->shm || !shell->layer_shell ||
            !shell->shady_shell) {
        fprintf(stderr,
            "shady-shell: compositor is missing required Shady/layer-shell globals\n");
        return false;
    }

    shell->surface = wl_compositor_create_surface(shell->compositor);
    if (!shell->surface) return false;

    shell->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        shell->layer_shell, shell->surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP, "shady-shell");
    if (!shell->layer_surface) return false;

    zwlr_layer_surface_v1_add_listener(shell->layer_surface,
        &layer_surface_listener, shell);
    zwlr_layer_surface_v1_set_size(shell->layer_surface, 0, BAR_HEIGHT);
    zwlr_layer_surface_v1_set_anchor(shell->layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(shell->layer_surface, BAR_HEIGHT);
    zwlr_layer_surface_v1_set_keyboard_interactivity(shell->layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    wl_surface_commit(shell->surface);
    shell->running = true;
    return true;
}

static void shell_finish(struct shell *shell) {
    launcher_hide(shell);
    if (shell->keyboard) wl_keyboard_release(shell->keyboard);
    if (shell->pointer) wl_pointer_release(shell->pointer);
    if (shell->seat) wl_seat_release(shell->seat);
    if (shell->layer_surface)
        zwlr_layer_surface_v1_destroy(shell->layer_surface);
    if (shell->surface) wl_surface_destroy(shell->surface);
    if (shell->shady_shell) shady_shell_v1_destroy(shell->shady_shell);
    if (shell->layer_shell) zwlr_layer_shell_v1_destroy(shell->layer_shell);
    if (shell->shm) wl_shm_destroy(shell->shm);
    if (shell->compositor) wl_compositor_destroy(shell->compositor);
    if (shell->registry) wl_registry_destroy(shell->registry);
    if (shell->display) wl_display_disconnect(shell->display);
    if (shell->xkb_state) xkb_state_unref(shell->xkb_state);
    if (shell->xkb_keymap) xkb_keymap_unref(shell->xkb_keymap);
    if (shell->xkb_context) xkb_context_unref(shell->xkb_context);
}

int main(void) {
    signal(SIGCHLD, SIG_IGN);
    struct shell shell = {0};
    if (!shell_init(&shell)) {
        shell_finish(&shell);
        return 1;
    }
    const char *open_launcher = getenv("SHADY_SHELL_OPEN_LAUNCHER");
    if (open_launcher && *open_launcher && strcmp(open_launcher, "0") != 0)
        launcher_show(&shell);

    int display_fd = wl_display_get_fd(shell.display);
    while (shell.running) {
        if (wl_display_dispatch_pending(shell.display) < 0) break;
        if (wl_display_flush(shell.display) < 0 && errno != EAGAIN) break;

        struct pollfd pfd = {
            .fd = display_fd,
            .events = POLLIN,
        };
        time_t now = time(NULL);
        int timeout_ms = (int)((60 - (now % 60)) * 1000);
        if (timeout_ms <= 0 || timeout_ms > 60000) timeout_ms = 60000;
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (rc == 0) {
            draw_bar(&shell);
            continue;
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (pfd.revents & POLLIN) {
            if (wl_display_dispatch(shell.display) < 0) break;
        }
    }

    shell_finish(&shell);
    return 0;
}
