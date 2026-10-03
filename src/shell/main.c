#define _POSIX_C_SOURCE 200809L

#include <cairo/cairo.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/input-event-codes.h>
#include <math.h>
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

#define BAR_HEIGHT 38
#define MAX_WORKSPACES 16
#define WORKSPACE_NAME_MAX 64
#define WINDOW_TEXT_MAX 256
#define MAX_APPS 512
#define APP_NAME_MAX 128
#define APP_EXEC_MAX 512
#define LAUNCHER_WIDTH 680
#define LAUNCHER_HEIGHT 580
#define LAUNCHER_RESULTS 8
#define SEARCH_MAX 128
#define MAX_WINDOWS 64
#define TASK_LABEL_MAX 256
#define CONTEXT_WIDTH 230
#define CONTEXT_ROW_HEIGHT 34
#define QUICK_WIDTH 250
#define QUICK_ROW_HEIGHT 38

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

struct shell_window {
    uint32_t id;
    char app_id[WINDOW_TEXT_MAX];
    char title[WINDOW_TEXT_MAX];
    char workspace[WORKSPACE_NAME_MAX];
    bool focused;
    bool maximized;
    bool fullscreen;
};

struct task_region {
    double x0;
    double x1;
    uint32_t window_id;
};

struct shell;
static struct shell_window *find_window(struct shell *shell, uint32_t id);

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
    struct wl_surface *context_surface;
    struct zwlr_layer_surface_v1 *context_layer_surface;
    struct wl_surface *quick_surface;
    struct zwlr_layer_surface_v1 *quick_layer_surface;

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
    struct shell_window windows[MAX_WINDOWS];
    struct task_region task_regions[MAX_WINDOWS];
    size_t window_count;
    size_t task_region_count;
    double clock_x0;
    double clock_x1;

    double pointer_x;
    double pointer_y;
    struct wl_surface *pointer_surface;

    struct launcher_app apps[MAX_APPS];
    size_t app_count;
    bool launcher_visible;
    bool launcher_configured;
    uint32_t launcher_width;
    uint32_t launcher_height;
    bool context_visible;
    bool context_configured;
    uint32_t context_width;
    uint32_t context_height;
    uint32_t context_window_id;
    int hovered_context_row;
    bool quick_visible;
    bool quick_configured;
    uint32_t quick_width;
    uint32_t quick_height;
    int hovered_quick_row;
    char search[SEARCH_MAX];
    size_t selected_result;
    int hovered_workspace;
    int hovered_task;
    int hovered_result;
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

/*
 * Visual language shared with the compositor's window frames: deep navy glass
 * with a faint top sheen, hairline edges, and a cyan accent that appears as a
 * glowing status pip on whatever currently has focus.
 */
struct ui_rgb { double r, g, b; };
/* Defaults match Neon Transit; rices override them with SHADY_SHELL_* colour
 * variables (see ui_load_theme). */
static struct ui_rgb UI_ACCENT = { 0.157, 0.902, 1.000 };
static struct ui_rgb UI_ACCENT_2 = { 0.100, 0.360, 0.900 };
static struct ui_rgb UI_ACCENT_DEEP = { 0.040, 0.300, 0.420 };
static struct ui_rgb UI_SURFACE = { 0.022, 0.030, 0.054 };
static struct ui_rgb UI_TEXT = { 0.910, 0.965, 1.000 };
static struct ui_rgb UI_TEXT_DIM = { 0.560, 0.650, 0.740 };
static struct ui_rgb UI_DANGER = { 1.000, 0.420, 0.450 };

static void ui_theme_color(const char *name, struct ui_rgb *out) {
    const char *value = getenv(name);
    unsigned r, g, b;
    if (!value || value[0] != '#' || strlen(value) != 7 ||
            sscanf(value + 1, "%02x%02x%02x", &r, &g, &b) != 3) {
        if (value && *value)
            fprintf(stderr, "shady-shell: ignoring %s=%s (want #RRGGBB)\n", name, value);
        return;
    }
    *out = (struct ui_rgb){ r / 255.0, g / 255.0, b / 255.0 };
}

static void ui_load_theme(void) {
    ui_theme_color("SHADY_SHELL_ACCENT", &UI_ACCENT);
    ui_theme_color("SHADY_SHELL_ACCENT_2", &UI_ACCENT_2);
    ui_theme_color("SHADY_SHELL_ACCENT_DEEP", &UI_ACCENT_DEEP);
    ui_theme_color("SHADY_SHELL_SURFACE", &UI_SURFACE);
    ui_theme_color("SHADY_SHELL_TEXT", &UI_TEXT);
    ui_theme_color("SHADY_SHELL_TEXT_DIM", &UI_TEXT_DIM);
    ui_theme_color("SHADY_SHELL_DANGER", &UI_DANGER);
}

static void add_letter_spacing(PangoLayout *layout) {
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(PANGO_SCALE / 4));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
}

static PangoLayout *make_layout_sized(cairo_t *cr, const char *text,
        bool bold, int size) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    char desc[64];
    snprintf(desc, sizeof(desc), bold ? "Sans SemiBold %d" : "Sans %d", size);
    PangoFontDescription *font = pango_font_description_from_string(desc);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_font_description_free(font);
    add_letter_spacing(layout);
    return layout;
}

/* Bar labels: measured and drawn through the same layout so hit regions
 * always match the rendered text. */
static PangoLayout *make_layout(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(
        bold ? "Sans SemiBold 9.5" : "Sans Medium 9.5");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_font_description_free(font);
    add_letter_spacing(layout);
    return layout;
}

static int text_width(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = make_layout(cr, text, bold);
    int width = 0;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

static void draw_layout_alpha(cairo_t *cr, PangoLayout *layout,
        double x, double y, struct ui_rgb color, double alpha) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

static void draw_layout(cairo_t *cr, PangoLayout *layout,
        double x, double y, double r, double g, double b) {
    draw_layout_alpha(cr, layout, x, y, (struct ui_rgb){ r, g, b }, 1.0);
}

/* Vertically centre a layout inside [y, y + height). */
static double layout_center_y(PangoLayout *layout, double y, double height) {
    int h = 0;
    pango_layout_get_pixel_size(layout, NULL, &h);
    return y + (height - h) * 0.5;
}

static void draw_text_color(cairo_t *cr, const char *text,
        double x, double y, bool bold, int size,
        double r, double g, double b) {
    PangoLayout *layout = make_layout_sized(cr, text, bold, size);
    draw_layout(cr, layout, x, y, r, g, b);
    g_object_unref(layout);
}

static void rounded_rect(cairo_t *cr, double x, double y,
        double width, double height, double radius) {
    double r = radius;
    if (r > width * 0.5) r = width * 0.5;
    if (r > height * 0.5) r = height * 0.5;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + width - r, y + height - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + height - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI + G_PI_2);
    cairo_close_path(cr);
}

static struct ui_rgb ui_mix(struct ui_rgb a, struct ui_rgb b, double t) {
    return (struct ui_rgb){
        a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
    };
}

/* Rounded fill with a soft vertical sheen: `lift` brightens the top edge. */
static void fill_sheen(cairo_t *cr, double x, double y, double w, double h,
        double radius, struct ui_rgb base, double alpha, double lift) {
    struct ui_rgb top = ui_mix(base, (struct ui_rgb){ 1, 1, 1 }, lift);
    cairo_pattern_t *pattern = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, top.r, top.g, top.b, alpha);
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, base.r, base.g, base.b, alpha);
    rounded_rect(cr, x, y, w, h, radius);
    cairo_set_source(cr, pattern);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
}

/* 1px outline aligned to the pixel grid. */
static void stroke_hairline(cairo_t *cr, double x, double y, double w, double h,
        double radius, struct ui_rgb color, double alpha) {
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, radius);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
    cairo_stroke(cr);
}

/* Focus pip, matching the compositor title bar: a lit dot with a halo for
 * the focused item, a dim ring otherwise. */
static void draw_pip(cairo_t *cr, double x, double y, double radius, bool lit) {
    if (lit) {
        cairo_pattern_t *halo = cairo_pattern_create_radial(
            x, y, 0, x, y, radius * 3.2);
        cairo_pattern_add_color_stop_rgba(halo, 0.0,
            UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.45);
        cairo_pattern_add_color_stop_rgba(halo, 1.0,
            UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
        cairo_set_source(cr, halo);
        cairo_arc(cr, x, y, radius * 3.2, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(halo);
        cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
        cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.28);
        cairo_arc(cr, x, y, radius - 0.5, 0, 2 * G_PI);
        cairo_stroke(cr);
    }
}

/* Popup/panel body: glass card, hairline rim and a brighter top edge. */
static void draw_panel(cairo_t *cr, double w, double h, double radius) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    fill_sheen(cr, 0, 0, w, h, radius, UI_SURFACE, 0.975, 0.045);
    stroke_hairline(cr, 0, 0, w, h, radius, UI_TEXT, 0.08);
    /* Accent kiss along the top edge, fading out towards the corners. */
    cairo_pattern_t *edge = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgba(edge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
    cairo_pattern_add_color_stop_rgba(edge, 0.5, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.40);
    cairo_pattern_add_color_stop_rgba(edge, 1.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
    cairo_set_source(cr, edge);
    cairo_rectangle(cr, radius, 0, w - 2 * radius, 1);
    cairo_fill(cr);
    cairo_pattern_destroy(edge);
}

/* Highlighted menu/list row: accent-tinted glass plus a focus pip. */
static void draw_row_highlight(cairo_t *cr, double x, double y, double w,
        double h, double radius, bool strong, bool danger) {
    struct ui_rgb tint = danger ? ui_mix(UI_SURFACE, UI_DANGER, 0.28) : UI_ACCENT_DEEP;
    fill_sheen(cr, x, y, w, h, radius, tint, strong ? 0.85 : 0.55, 0.06);
    stroke_hairline(cr, x, y, w, h, radius,
        danger ? UI_DANGER : UI_ACCENT, strong ? 0.32 : 0.16);
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
    const double w = shell->width;
    const double h = shell->height;

    /* Glass strip: lighter at the top, settling into deep navy. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *strip = cairo_pattern_create_linear(0, 0, 0, h);
    struct ui_rgb strip_top = ui_mix(UI_SURFACE, (struct ui_rgb){ 1, 1, 1 }, 0.025);
    struct ui_rgb strip_bottom = ui_mix(UI_SURFACE, (struct ui_rgb){ 0, 0, 0 }, 0.35);
    cairo_pattern_add_color_stop_rgba(strip, 0.0, strip_top.r, strip_top.g, strip_top.b, 0.94);
    cairo_pattern_add_color_stop_rgba(strip, 1.0, strip_bottom.r, strip_bottom.g, strip_bottom.b, 0.94);
    cairo_set_source(cr, strip);
    cairo_paint(cr);
    cairo_pattern_destroy(strip);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
    cairo_rectangle(cr, 0, 0, w, 1);
    cairo_fill(cr);
    /* Bottom edge glows with the accent in the middle and fades out. */
    cairo_pattern_t *edge = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgba(edge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.06);
    cairo_pattern_add_color_stop_rgba(edge, 0.5, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.42);
    cairo_pattern_add_color_stop_rgba(edge, 1.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.06);
    cairo_set_source(cr, edge);
    cairo_rectangle(cr, 0, h - 1, w, 1);
    cairo_fill(cr);
    cairo_pattern_destroy(edge);

    /* Brand badge: accent gradient tile with a dark monogram. */
    cairo_pattern_t *badge = cairo_pattern_create_linear(8, 6, 34, 32);
    cairo_pattern_add_color_stop_rgba(badge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
    cairo_pattern_add_color_stop_rgba(badge, 1.0, UI_ACCENT_2.r, UI_ACCENT_2.g, UI_ACCENT_2.b, 1.0);
    rounded_rect(cr, 8, 6, 26, 26, 8);
    cairo_set_source(cr, badge);
    cairo_fill(cr);
    cairo_pattern_destroy(badge);
    stroke_hairline(cr, 8, 6, 26, 26, 8, (struct ui_rgb){ 1, 1, 1 }, 0.25);
    PangoLayout *mono = make_layout_sized(cr, "S", true, 11);
    int mono_w = 0;
    pango_layout_get_pixel_size(mono, &mono_w, NULL);
    draw_layout(cr, mono, 21 - mono_w * 0.5, layout_center_y(mono, 6, 26),
        0.02, 0.06, 0.10);
    g_object_unref(mono);
    PangoLayout *brand = make_layout(cr, "Shady", true);
    draw_layout_alpha(cr, brand, 42, layout_center_y(brand, 0, h), UI_TEXT, 0.92);
    g_object_unref(brand);

    double x = 94.0;
    /* Measure workspace pills first so they can share one recessed track. */
    double ws_x = x;
    for (size_t i = 0; i < shell->workspace_count; i++) {
        bool active = strcmp(shell->workspaces[i], shell->active_workspace) == 0;
        double box_width = text_width(cr, shell->workspaces[i], active) + 22.0;
        shell->workspace_regions[i].x0 = ws_x;
        shell->workspace_regions[i].x1 = ws_x + box_width;
        ws_x += box_width + 5.0;
    }
    if (shell->workspace_count > 0) {
        double track_w = shell->workspace_regions[shell->workspace_count - 1].x1 - x + 6.0;
        rounded_rect(cr, x - 3, 5, track_w, h - 10, 10);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
        cairo_fill(cr);
        stroke_hairline(cr, x - 3, 5, track_w, h - 10, 10,
            (struct ui_rgb){ 1, 1, 1 }, 0.06);
    }
    for (size_t i = 0; i < shell->workspace_count; i++) {
        const char *name = shell->workspaces[i];
        bool active = strcmp(name, shell->active_workspace) == 0;
        bool hovered = shell->hovered_workspace == (int)i;
        double px = shell->workspace_regions[i].x0;
        double box_width = shell->workspace_regions[i].x1 - px;

        if (active) {
            fill_sheen(cr, px, 8, box_width, h - 16, 7, UI_ACCENT_DEEP, 0.92, 0.08);
            stroke_hairline(cr, px, 8, box_width, h - 16, 7, UI_ACCENT, 0.35);
        } else if (hovered) {
            rounded_rect(cr, px, 8, box_width, h - 16, 7);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
            cairo_fill(cr);
        }
        PangoLayout *layout = make_layout(cr, name, active);
        double ty = layout_center_y(layout, 0, h);
        if (active) {
            cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
            cairo_arc(cr, px + 10, h / 2.0, 2.2, 0, 2 * G_PI);
            cairo_fill(cr);
            draw_layout_alpha(cr, layout, px + 17, ty, UI_TEXT, 0.98);
        } else {
            draw_layout_alpha(cr, layout, px + 11, ty, UI_TEXT, hovered ? 0.85 : 0.55);
        }
        g_object_unref(layout);
    }
    x = ws_x;

    shell->task_region_count = 0;
    double task_limit = shell->width - 150.0;
    for (size_t i = 0; i < shell->window_count && x + 72.0 < task_limit; i++) {
        struct shell_window *window = &shell->windows[i];
        if (window->workspace[0] && shell->active_workspace[0] &&
                strcmp(window->workspace, shell->active_workspace) != 0)
            continue;
        const char *label = window->title[0] ? window->title : window->app_id;
        if (!label[0]) label = "Window";
        int measured = text_width(cr, label, window->focused);
        double box_width = measured + 36.0;
        if (box_width < 92.0) box_width = 92.0;
        if (box_width > 200.0) box_width = 200.0;
        if (x + box_width > task_limit) box_width = task_limit - x;
        if (box_width < 72.0) break;

        size_t region = shell->task_region_count++;
        shell->task_regions[region].x0 = x;
        shell->task_regions[region].x1 = x + box_width;
        shell->task_regions[region].window_id = window->id;
        bool hovered = shell->hovered_task == (int)region;

        if (window->focused) {
            fill_sheen(cr, x, 6, box_width, h - 12, 9, UI_ACCENT_DEEP, 0.90, 0.08);
            stroke_hairline(cr, x, 6, box_width, h - 12, 9, UI_ACCENT, 0.38);
        } else {
            rounded_rect(cr, x, 6, box_width, h - 12, 9);
            cairo_set_source_rgba(cr, 1, 1, 1, hovered ? 0.06 : 0.025);
            cairo_fill(cr);
            stroke_hairline(cr, x, 6, box_width, h - 12, 9,
                (struct ui_rgb){ 1, 1, 1 }, hovered ? 0.12 : 0.06);
        }
        draw_pip(cr, x + 13, h / 2.0, 3.0, window->focused);

        PangoLayout *layout = make_layout(cr, label, window->focused);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, (int)(box_width - 32.0) * PANGO_SCALE);
        draw_layout_alpha(cr, layout, x + 23, layout_center_y(layout, 0, h),
            UI_TEXT, window->focused ? 0.97 : (hovered ? 0.82 : 0.60));
        g_object_unref(layout);
        x += box_width + 6.0;
    }

    char clock_text[64] = {0};
    char date_text[64] = {0};
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(clock_text, sizeof(clock_text), "%H:%M", &local);
    strftime(date_text, sizeof(date_text), "%a %d %b", &local);

    PangoLayout *clock = make_layout(cr, clock_text, true);
    PangoLayout *date = make_layout(cr, date_text, false);
    int clock_width = 0, date_width = 0;
    pango_layout_get_pixel_size(clock, &clock_width, NULL);
    pango_layout_get_pixel_size(date, &date_width, NULL);
    double clock_x = w - clock_width - 22.0;
    double date_x = clock_x - date_width - 10.0;
    shell->clock_x0 = date_x - 12;
    shell->clock_x1 = clock_x + clock_width + 10;
    double pill_w = shell->clock_x1 - shell->clock_x0;
    if (shell->quick_visible) {
        fill_sheen(cr, shell->clock_x0, 6, pill_w, h - 12, 9, UI_ACCENT_DEEP, 0.92, 0.08);
        stroke_hairline(cr, shell->clock_x0, 6, pill_w, h - 12, 9, UI_ACCENT, 0.38);
    } else {
        rounded_rect(cr, shell->clock_x0, 6, pill_w, h - 12, 9);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.03);
        cairo_fill(cr);
        stroke_hairline(cr, shell->clock_x0, 6, pill_w, h - 12, 9,
            (struct ui_rgb){ 1, 1, 1 }, 0.07);
    }
    draw_layout_alpha(cr, date, date_x, layout_center_y(date, 0, h), UI_TEXT_DIM, 0.95);
    draw_layout_alpha(cr, clock, clock_x, layout_center_y(clock, 0, h), UI_TEXT, 0.98);
    g_object_unref(date);
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
    const double lw = shell->launcher_width;

    draw_panel(cr, lw, shell->launcher_height, 18);

    cairo_pattern_t *badge = cairo_pattern_create_linear(22, 18, 56, 52);
    cairo_pattern_add_color_stop_rgba(badge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
    cairo_pattern_add_color_stop_rgba(badge, 1.0, UI_ACCENT_2.r, UI_ACCENT_2.g, UI_ACCENT_2.b, 1.0);
    rounded_rect(cr, 22, 18, 34, 34, 10);
    cairo_set_source(cr, badge);
    cairo_fill(cr);
    cairo_pattern_destroy(badge);
    stroke_hairline(cr, 22, 18, 34, 34, 10, (struct ui_rgb){ 1, 1, 1 }, 0.25);
    draw_text_color(cr, "S", 34, 24, true, 12, 0.02, 0.06, 0.10);
    draw_text_color(cr, "Applications", 68, 18, true, 13,
        UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
    draw_text_color(cr, "Launch something", 68, 38, false, 9,
        UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);
    /* Keycap hint. */
    PangoLayout *esc = make_layout_sized(cr, "Esc", true, 8);
    int esc_w = 0;
    pango_layout_get_pixel_size(esc, &esc_w, NULL);
    double esc_x = lw - 22 - esc_w - 12;
    rounded_rect(cr, esc_x, 24, esc_w + 12, 20, 5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
    cairo_fill(cr);
    stroke_hairline(cr, esc_x, 24, esc_w + 12, 20, 5, UI_TEXT, 0.14);
    draw_layout_alpha(cr, esc, esc_x + 6, layout_center_y(esc, 24, 20), UI_TEXT_DIM, 1.0);
    g_object_unref(esc);

    /* Search field: recessed well with an accent focus ring. */
    rounded_rect(cr, 22, 68, lw - 44, 48, 12);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
    cairo_fill(cr);
    stroke_hairline(cr, 22, 68, lw - 44, 48, 12, UI_ACCENT, 0.42);
    stroke_hairline(cr, 20, 66, lw - 40, 52, 14, UI_ACCENT, 0.10);
    draw_text_color(cr, "⌕", 38, 79, false, 13,
        UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
    if (shell->search[0]) {
        PangoLayout *query = make_layout_sized(cr, shell->search, true, 10);
        int query_w = 0;
        pango_layout_get_pixel_size(query, &query_w, NULL);
        draw_layout_alpha(cr, query, 62, layout_center_y(query, 68, 48), UI_TEXT, 1.0);
        g_object_unref(query);
        /* Caret after the query. */
        cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.9);
        cairo_rectangle(cr, 64 + query_w, 82, 1.5, 20);
        cairo_fill(cr);
    } else {
        PangoLayout *hint = make_layout_sized(cr, "Search applications…", false, 10);
        draw_layout_alpha(cr, hint, 62, layout_center_y(hint, 68, 48), UI_TEXT_DIM, 0.75);
        g_object_unref(hint);
    }

    size_t matches[LAUNCHER_RESULTS];
    size_t count = launcher_matching_indices(shell, matches);
    if (count == 0) shell->selected_result = 0;
    else if (shell->selected_result >= count) shell->selected_result = count - 1;

    double y = 132.0;
    for (size_t i = 0; i < count; i++) {
        const struct launcher_app *app = &shell->apps[matches[i]];
        bool selected = i == shell->selected_result;
        bool hovered = shell->hovered_result == (int)i;
        if (selected || hovered)
            draw_row_highlight(cr, 22, y - 7, lw - 44, 46, 11, selected, false);

        /* Monogram tile stands in for an app icon. */
        char initial[8] = {0};
        gunichar first = g_utf8_get_char_validated(app->name, -1);
        if (first != (gunichar)-1 && first != (gunichar)-2 && first != 0)
            g_unichar_to_utf8(g_unichar_toupper(first), initial);
        else
            initial[0] = '?';
        fill_sheen(cr, 34, y - 1, 34, 34, 9,
            selected ? ui_mix(UI_ACCENT_DEEP, UI_ACCENT, 0.18) : ui_mix(UI_SURFACE, UI_TEXT, 0.07),
            1.0, 0.10);
        stroke_hairline(cr, 34, y - 1, 34, 34, 9,
            selected ? UI_ACCENT : UI_TEXT, selected ? 0.45 : 0.10);
        PangoLayout *glyph = make_layout_sized(cr, initial, true, 11);
        int glyph_w = 0;
        pango_layout_get_pixel_size(glyph, &glyph_w, NULL);
        draw_layout_alpha(cr, glyph, 51 - glyph_w * 0.5,
            layout_center_y(glyph, y - 1, 34), UI_TEXT, selected ? 1.0 : 0.75);
        g_object_unref(glyph);

        PangoLayout *name = make_layout_sized(cr, app->name, selected, 10);
        pango_layout_set_ellipsize(name, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(name, (int)(lw - 140) * PANGO_SCALE);
        draw_layout_alpha(cr, name, 80, y, UI_TEXT, selected ? 1.0 : 0.84);
        g_object_unref(name);
        PangoLayout *exec = make_layout_sized(cr, app->exec, false, 8);
        pango_layout_set_ellipsize(exec, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(exec, (int)(lw - 140) * PANGO_SCALE);
        draw_layout_alpha(cr, exec, 80, y + 18, UI_TEXT_DIM, selected ? 0.9 : 0.65);
        g_object_unref(exec);
        if (selected) {
            PangoLayout *enter = make_layout_sized(cr, "↵", true, 10);
            draw_layout_alpha(cr, enter, lw - 52, layout_center_y(enter, y - 7, 46),
                UI_ACCENT, 0.85);
            g_object_unref(enter);
        }
        y += 50.0;
    }

    if (count == 0) {
        draw_text_color(cr, "No applications found", 38, 145, true, 10,
            UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
        draw_text_color(cr, "Try another name or executable", 38, 165, false, 9,
            UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);
    }

    /* Footer: hairline divider and keyboard hints. */
    double fy = shell->launcher_height - 44.0;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
    cairo_rectangle(cr, 22, fy, lw - 44, 1);
    cairo_fill(cr);
    draw_text_color(cr, "↑ ↓  navigate     ↵  launch     Esc  close", 24,
        fy + 14, false, 8, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);

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
    shell->hovered_result = -1;
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
    shell->hovered_result = -1;
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

static struct shell_window *find_window(struct shell *shell, uint32_t id) {
    for (size_t i = 0; i < shell->window_count; i++)
        if (shell->windows[i].id == id) return &shell->windows[i];
    return NULL;
}

static size_t context_row_count(struct shell *shell) {
    return 4 + shell->workspace_count;
}

static void context_hide(struct shell *shell) {
    if (shell->context_layer_surface) {
        zwlr_layer_surface_v1_destroy(shell->context_layer_surface);
        shell->context_layer_surface = NULL;
    }
    if (shell->context_surface) {
        wl_surface_destroy(shell->context_surface);
        shell->context_surface = NULL;
    }
    shell->context_visible = false;
    shell->context_configured = false;
    shell->context_window_id = 0;
    shell->hovered_context_row = -1;
}

static const char *context_row_label(struct shell *shell,
        struct shell_window *window, size_t row, char *buffer, size_t size) {
    if (row == 0) return "Focus";
    if (row == 1) return window && window->maximized ? "Restore" : "Maximize";
    if (row == 2) return window && window->fullscreen ? "Exit fullscreen" : "Fullscreen";
    if (row < 3 + shell->workspace_count) {
        size_t ws = row - 3;
        snprintf(buffer, size, "Move to %s", shell->workspaces[ws]);
        return buffer;
    }
    return "Close";
}

static void draw_context(struct shell *shell) {
    if (!shell->context_visible || !shell->context_configured ||
            !shell->context_surface || shell->context_width == 0 ||
            shell->context_height == 0) return;
    struct shell_window *window = find_window(shell, shell->context_window_id);
    if (!window) { context_hide(shell); return; }

    struct shell_buffer *buffer = create_buffer(shell,
        shell->context_width, shell->context_height);
    if (!buffer) return;
    cairo_surface_t *image = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        (int)shell->context_width, (int)shell->context_height,
        (int)shell->context_width * 4);
    cairo_t *cr = cairo_create(image);
    const double cw = shell->context_width;
    draw_panel(cr, cw, shell->context_height, 11);

    size_t rows = context_row_count(shell);
    for (size_t row = 0; row < rows; row++) {
        double y = row * CONTEXT_ROW_HEIGHT;
        bool hovered = shell->hovered_context_row == (int)row;
        bool destructive = row + 1 == rows;
        bool workspace_row = row >= 3 && row < 3 + shell->workspace_count;
        /* Group dividers: window actions | move targets | close. */
        if (row == 3 || destructive) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_rectangle(cr, 12, y, cw - 24, 1);
            cairo_fill(cr);
        }
        if (hovered)
            draw_row_highlight(cr, 6, y + 3, cw - 12, CONTEXT_ROW_HEIGHT - 6, 7,
                true, destructive);
        char label_buf[WORKSPACE_NAME_MAX + 32];
        const char *label = context_row_label(shell, window, row,
            label_buf, sizeof(label_buf));
        bool current = workspace_row && window &&
            strcmp(window->workspace, shell->workspaces[row - 3]) == 0;
        if (workspace_row)
            draw_pip(cr, 18, y + CONTEXT_ROW_HEIGHT / 2.0, 2.5, current);
        PangoLayout *layout = make_layout_sized(cr, label, hovered, 9);
        draw_layout_alpha(cr, layout, workspace_row ? 28 : 14,
            layout_center_y(layout, y, CONTEXT_ROW_HEIGHT),
            destructive ? UI_DANGER : UI_TEXT,
            hovered ? 1.0 : (destructive ? 0.85 : 0.78));
        g_object_unref(layout);
    }

    cairo_destroy(cr);
    cairo_surface_flush(image);
    cairo_surface_destroy(image);
    wl_surface_attach(shell->context_surface, buffer->wl_buffer, 0, 0);
    wl_surface_damage_buffer(shell->context_surface, 0, 0,
        (int)shell->context_width, (int)shell->context_height);
    wl_surface_commit(shell->context_surface);
}

static void context_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct shell *shell = data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    shell->context_width = width ? width : CONTEXT_WIDTH;
    shell->context_height = height ? height :
        (uint32_t)(context_row_count(shell) * CONTEXT_ROW_HEIGHT);
    shell->context_configured = true;
    draw_context(shell);
}

static void context_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    context_hide(data);
}

static const struct zwlr_layer_surface_v1_listener context_layer_listener = {
    .configure = context_configure,
    .closed = context_closed,
};

static void context_show(struct shell *shell, uint32_t window_id, int x) {
    if (!shell->layer_shell || !shell->compositor) return;
    context_hide(shell);
    shell->context_surface = wl_compositor_create_surface(shell->compositor);
    if (!shell->context_surface) return;
    shell->context_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        shell->layer_shell, shell->context_surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "shady-window-menu");
    if (!shell->context_layer_surface) {
        wl_surface_destroy(shell->context_surface);
        shell->context_surface = NULL;
        return;
    }
    shell->context_window_id = window_id;
    shell->context_visible = true;
    shell->context_configured = false;
    shell->hovered_context_row = -1;
    uint32_t height = (uint32_t)(context_row_count(shell) * CONTEXT_ROW_HEIGHT);
    zwlr_layer_surface_v1_add_listener(shell->context_layer_surface,
        &context_layer_listener, shell);
    zwlr_layer_surface_v1_set_size(shell->context_layer_surface,
        CONTEXT_WIDTH, height);
    zwlr_layer_surface_v1_set_anchor(shell->context_layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_margin(shell->context_layer_surface,
        BAR_HEIGHT + 4, 0, 0, x < 0 ? 0 : x);
    zwlr_layer_surface_v1_set_exclusive_zone(shell->context_layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(shell->context_layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(shell->context_surface);
}

static size_t quick_row_count(struct shell *shell) {
    return 3 + shell->workspace_count;
}

static void quick_hide(struct shell *shell) {
    if (shell->quick_layer_surface) {
        zwlr_layer_surface_v1_destroy(shell->quick_layer_surface);
        shell->quick_layer_surface = NULL;
    }
    if (shell->quick_surface) {
        wl_surface_destroy(shell->quick_surface);
        shell->quick_surface = NULL;
    }
    shell->quick_visible = false;
    shell->quick_configured = false;
    shell->hovered_quick_row = -1;
    if (shell->snapshot_done) draw_bar(shell);
}

static const char *quick_row_label(struct shell *shell, size_t row,
        char *buffer, size_t size) {
    if (row == 0) return "Launcher";
    if (row == 1) return "Next window";
    if (row < 2 + shell->workspace_count) {
        /* The active workspace is marked with a pip, not text. */
        snprintf(buffer, size, "Workspace  %s", shell->workspaces[row - 2]);
        return buffer;
    }
    return "Quit Shady";
}

static void draw_quick(struct shell *shell) {
    if (!shell->quick_visible || !shell->quick_configured ||
            !shell->quick_surface || shell->quick_width == 0 ||
            shell->quick_height == 0) return;

    struct shell_buffer *buffer = create_buffer(shell,
        shell->quick_width, shell->quick_height);
    if (!buffer) return;
    cairo_surface_t *image = cairo_image_surface_create_for_data(
        buffer->data, CAIRO_FORMAT_ARGB32,
        (int)shell->quick_width, (int)shell->quick_height,
        (int)shell->quick_width * 4);
    cairo_t *cr = cairo_create(image);
    const double qw = shell->quick_width;
    draw_panel(cr, qw, shell->quick_height, 13);
    PangoLayout *title = make_layout_sized(cr, "Quick settings", true, 10);
    draw_layout_alpha(cr, title, 16, layout_center_y(title, 0, 34), UI_TEXT, 0.96);
    g_object_unref(title);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    cairo_rectangle(cr, 12, 33, qw - 24, 1);
    cairo_fill(cr);

    size_t rows = quick_row_count(shell);
    for (size_t row = 0; row < rows; row++) {
        double y = 34 + row * QUICK_ROW_HEIGHT;
        bool hovered = shell->hovered_quick_row == (int)row;
        bool destructive = row + 1 == rows;
        bool workspace_row = row >= 2 && row < 2 + shell->workspace_count;
        if (destructive) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_rectangle(cr, 12, y + 1, qw - 24, 1);
            cairo_fill(cr);
        }
        if (hovered)
            draw_row_highlight(cr, 7, y + 3, qw - 14, QUICK_ROW_HEIGHT - 6, 8,
                true, destructive);
        char label_buf[WORKSPACE_NAME_MAX + 32];
        const char *label = quick_row_label(shell, row,
            label_buf, sizeof(label_buf));
        bool active = workspace_row &&
            strcmp(shell->workspaces[row - 2], shell->active_workspace) == 0;
        if (workspace_row)
            draw_pip(cr, 21, y + QUICK_ROW_HEIGHT / 2.0, 2.8, active);
        PangoLayout *layout = make_layout_sized(cr, label, hovered || active, 9);
        draw_layout_alpha(cr, layout, workspace_row ? 32 : 16,
            layout_center_y(layout, y, QUICK_ROW_HEIGHT),
            destructive ? UI_DANGER : UI_TEXT,
            hovered || active ? 1.0 : (destructive ? 0.85 : 0.78));
        g_object_unref(layout);
    }

    cairo_destroy(cr);
    cairo_surface_flush(image);
    cairo_surface_destroy(image);
    wl_surface_attach(shell->quick_surface, buffer->wl_buffer, 0, 0);
    wl_surface_damage_buffer(shell->quick_surface, 0, 0,
        (int)shell->quick_width, (int)shell->quick_height);
    wl_surface_commit(shell->quick_surface);
}

static void quick_configure(void *data,
        struct zwlr_layer_surface_v1 *layer_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct shell *shell = data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    shell->quick_width = width ? width : QUICK_WIDTH;
    shell->quick_height = height ? height :
        (uint32_t)(34 + quick_row_count(shell) * QUICK_ROW_HEIGHT + 6);
    shell->quick_configured = true;
    draw_quick(shell);
}

static void quick_closed(void *data,
        struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    quick_hide(data);
}

static const struct zwlr_layer_surface_v1_listener quick_layer_listener = {
    .configure = quick_configure,
    .closed = quick_closed,
};

static void quick_show(struct shell *shell) {
    if (!shell->layer_shell || !shell->compositor || shell->quick_visible) return;
    context_hide(shell);
    shell->quick_surface = wl_compositor_create_surface(shell->compositor);
    if (!shell->quick_surface) return;
    shell->quick_layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        shell->layer_shell, shell->quick_surface, NULL,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "shady-quick-settings");
    if (!shell->quick_layer_surface) {
        wl_surface_destroy(shell->quick_surface);
        shell->quick_surface = NULL;
        return;
    }
    shell->quick_visible = true;
    shell->quick_configured = false;
    shell->hovered_quick_row = -1;
    uint32_t height = (uint32_t)(34 + quick_row_count(shell) * QUICK_ROW_HEIGHT + 6);
    zwlr_layer_surface_v1_add_listener(shell->quick_layer_surface,
        &quick_layer_listener, shell);
    zwlr_layer_surface_v1_set_size(shell->quick_layer_surface, QUICK_WIDTH, height);
    zwlr_layer_surface_v1_set_anchor(shell->quick_layer_surface,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_margin(shell->quick_layer_surface,
        BAR_HEIGHT + 4, 8, 0, 0);
    zwlr_layer_surface_v1_set_exclusive_zone(shell->quick_layer_surface, 0);
    zwlr_layer_surface_v1_set_keyboard_interactivity(shell->quick_layer_surface,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    wl_surface_commit(shell->quick_surface);
    if (shell->snapshot_done) draw_bar(shell);
}

static void quick_toggle(struct shell *shell) {
    if (shell->quick_visible) quick_hide(shell);
    else quick_show(shell);
}

static struct shell_window *ensure_window(struct shell *shell, uint32_t id) {
    struct shell_window *window = find_window(shell, id);
    if (window) return window;
    if (shell->window_count >= MAX_WINDOWS) return NULL;
    window = &shell->windows[shell->window_count++];
    memset(window, 0, sizeof(*window));
    window->id = id;
    return window;
}

static void remove_window(struct shell *shell, uint32_t id) {
    if (shell->context_window_id == id) context_hide(shell);
    for (size_t i = 0; i < shell->window_count; i++) {
        if (shell->windows[i].id != id) continue;
        if (i + 1 < shell->window_count)
            memmove(&shell->windows[i], &shell->windows[i + 1],
                (shell->window_count - i - 1) * sizeof(shell->windows[0]));
        shell->window_count--;
        return;
    }
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
    if (changed && shell->quick_visible) {
        quick_hide(shell);
        quick_show(shell);
    }
    if (changed && shell->snapshot_done) draw_bar(shell);
}

static void protocol_active_workspace(void *data,
        struct shady_shell_v1 *protocol, const char *name) {
    (void)protocol;
    struct shell *shell = data;
    add_workspace(shell, name);
    copy_text(shell->active_workspace, sizeof(shell->active_workspace), name);
    if (shell->quick_visible) draw_quick(shell);
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

static void protocol_window(void *data, struct shady_shell_v1 *protocol,
        uint32_t id, const char *app_id, const char *title,
        const char *workspace, uint32_t focused) {
    (void)protocol;
    struct shell *shell = data;
    struct shell_window *window = ensure_window(shell, id);
    if (!window) return;
    copy_text(window->app_id, sizeof(window->app_id), app_id);
    copy_text(window->title, sizeof(window->title), title);
    copy_text(window->workspace, sizeof(window->workspace), workspace);
    window->focused = focused != 0;
    /* Focus is exclusive, but the compositor only re-sends the newly focused
     * window, so clear the previous holder here. */
    if (window->focused) {
        for (size_t i = 0; i < shell->window_count; i++)
            if (&shell->windows[i] != window) shell->windows[i].focused = false;
    }
    if (shell->snapshot_done) draw_bar(shell);
}

static void protocol_window_removed(void *data,
        struct shady_shell_v1 *protocol, uint32_t id) {
    (void)protocol;
    struct shell *shell = data;
    remove_window(shell, id);
    if (shell->context_window_id == id) shell->context_window_id = 0;
    if (shell->snapshot_done) draw_bar(shell);
}

static void protocol_window_state(void *data, struct shady_shell_v1 *protocol,
        uint32_t id, uint32_t maximized, uint32_t fullscreen) {
    (void)protocol;
    struct shell *shell = data;
    struct shell_window *window = ensure_window(shell, id);
    if (!window) return;
    window->maximized = maximized != 0;
    window->fullscreen = fullscreen != 0;
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
    .window = protocol_window,
    .window_removed = protocol_window_removed,
    .window_state = protocol_window_state,
    .toggle_launcher = protocol_toggle_launcher,
    .done = protocol_done,
};

static void update_pointer_hover(struct shell *shell) {
    if (shell->pointer_surface == shell->surface) {
        int hovered_workspace = -1;
        int hovered_task = -1;
        if (shell->pointer_y >= 0 && shell->pointer_y < shell->height) {
            for (size_t i = 0; i < shell->workspace_count; i++) {
                if (shell->pointer_x >= shell->workspace_regions[i].x0 &&
                        shell->pointer_x < shell->workspace_regions[i].x1) {
                    hovered_workspace = (int)i;
                    break;
                }
            }
            if (hovered_workspace < 0) {
                for (size_t i = 0; i < shell->task_region_count; i++) {
                    if (shell->pointer_x >= shell->task_regions[i].x0 &&
                            shell->pointer_x < shell->task_regions[i].x1) {
                        hovered_task = (int)i;
                        break;
                    }
                }
            }
        }
        if (hovered_workspace != shell->hovered_workspace ||
                hovered_task != shell->hovered_task) {
            shell->hovered_workspace = hovered_workspace;
            shell->hovered_task = hovered_task;
            draw_bar(shell);
        }
        return;
    }

    if (shell->pointer_surface == shell->quick_surface &&
            shell->quick_visible) {
        int hovered = -1;
        size_t rows = quick_row_count(shell);
        if (shell->pointer_x >= 0 && shell->pointer_x < shell->quick_width &&
                shell->pointer_y >= 34 && shell->pointer_y < shell->quick_height) {
            int row = (int)((shell->pointer_y - 34) / QUICK_ROW_HEIGHT);
            if (row >= 0 && (size_t)row < rows) hovered = row;
        }
        if (hovered != shell->hovered_quick_row) {
            shell->hovered_quick_row = hovered;
            draw_quick(shell);
        }
        return;
    }

    if (shell->pointer_surface == shell->context_surface &&
            shell->context_visible) {
        int hovered = -1;
        size_t rows = context_row_count(shell);
        if (shell->pointer_x >= 0 && shell->pointer_x < shell->context_width &&
                shell->pointer_y >= 0 && shell->pointer_y < shell->context_height) {
            int row = (int)(shell->pointer_y / CONTEXT_ROW_HEIGHT);
            if (row >= 0 && (size_t)row < rows) hovered = row;
        }
        if (hovered != shell->hovered_context_row) {
            shell->hovered_context_row = hovered;
            draw_context(shell);
        }
        return;
    }

    if (shell->pointer_surface == shell->launcher_surface &&
            shell->launcher_visible) {
        int hovered = -1;
        size_t matches[LAUNCHER_RESULTS];
        size_t count = launcher_matching_indices(shell, matches);
        if (shell->pointer_y >= 125.0) {
            int row = (int)((shell->pointer_y - 125.0) / 50.0);
            double row_y = 125.0 + row * 50.0;
            if (row >= 0 && (size_t)row < count &&
                    shell->pointer_y < row_y + 46.0 &&
                    shell->pointer_x >= 22.0 &&
                    shell->pointer_x < shell->launcher_width - 22.0)
                hovered = row;
        }
        if (hovered != shell->hovered_result) {
            shell->hovered_result = hovered;
            draw_launcher(shell);
        }
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface,
        wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)serial;
    struct shell *shell = data;
    shell->pointer_surface = surface;
    shell->pointer_x = wl_fixed_to_double(surface_x);
    shell->pointer_y = wl_fixed_to_double(surface_y);
    update_pointer_hover(shell);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    struct shell *shell = data;
    bool redraw_bar = surface == shell->surface &&
        (shell->hovered_workspace >= 0 || shell->hovered_task >= 0);
    bool redraw_launcher = surface == shell->launcher_surface && shell->hovered_result >= 0;
    bool redraw_context = surface == shell->context_surface && shell->hovered_context_row >= 0;
    bool redraw_quick = surface == shell->quick_surface && shell->hovered_quick_row >= 0;
    shell->pointer_surface = NULL;
    shell->hovered_workspace = -1;
    shell->hovered_task = -1;
    shell->hovered_context_row = -1;
    shell->hovered_quick_row = -1;
    shell->hovered_result = -1;
    if (redraw_bar) draw_bar(shell);
    if (redraw_launcher) draw_launcher(shell);
    if (redraw_context) draw_context(shell);
    if (redraw_quick) draw_quick(shell);
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
        uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)time;
    struct shell *shell = data;
    shell->pointer_x = wl_fixed_to_double(surface_x);
    shell->pointer_y = wl_fixed_to_double(surface_y);
    update_pointer_hover(shell);
}

static void pointer_button(void *data, struct wl_pointer *pointer,
        uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    (void)serial;
    (void)time;
    struct shell *shell = data;
    if (state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;

    if (button == BTN_LEFT && shell->pointer_surface == shell->launcher_surface &&
            shell->launcher_visible && shell->hovered_result >= 0) {
        shell->selected_result = (size_t)shell->hovered_result;
        launcher_activate_selected(shell);
        return;
    }

    if (button == BTN_LEFT && shell->pointer_surface == shell->quick_surface &&
            shell->quick_visible && shell->hovered_quick_row >= 0) {
        size_t row = (size_t)shell->hovered_quick_row;
        size_t rows = quick_row_count(shell);
        if (row == 0) {
            quick_hide(shell);
            launcher_toggle(shell);
        } else if (row == 1) {
            shady_shell_v1_cycle_window(shell->shady_shell);
            wl_display_flush(shell->display);
            quick_hide(shell);
        } else if (row < 2 + shell->workspace_count) {
            shady_shell_v1_activate_workspace(shell->shady_shell,
                shell->workspaces[row - 2]);
            wl_display_flush(shell->display);
            quick_hide(shell);
        } else if (row + 1 == rows) {
            shady_shell_v1_terminate(shell->shady_shell);
            wl_display_flush(shell->display);
        }
        return;
    }

    if (button == BTN_LEFT && shell->pointer_surface == shell->context_surface &&
            shell->context_visible && shell->hovered_context_row >= 0) {
        size_t row = (size_t)shell->hovered_context_row;
        size_t rows = context_row_count(shell);
        uint32_t id = shell->context_window_id;
        if (row == 0)
            shady_shell_v1_activate_window(shell->shady_shell, id);
        else if (row == 1)
            shady_shell_v1_toggle_maximize(shell->shady_shell, id);
        else if (row == 2)
            shady_shell_v1_toggle_fullscreen(shell->shady_shell, id);
        else if (row < 3 + shell->workspace_count)
            shady_shell_v1_move_window_to_workspace(shell->shady_shell, id,
                shell->workspaces[row - 3]);
        else if (row + 1 == rows)
            shady_shell_v1_close_window(shell->shady_shell, id);
        wl_display_flush(shell->display);
        context_hide(shell);
        return;
    }

    if (!shell->shady_shell || shell->pointer_surface != shell->surface ||
            shell->pointer_y < 0 || shell->pointer_y >= shell->height)
        return;

    if (button == BTN_LEFT && shell->pointer_x >= shell->clock_x0 &&
            shell->pointer_x < shell->clock_x1) {
        context_hide(shell);
        quick_toggle(shell);
        return;
    }

    if (button == BTN_LEFT) {
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

    if (shell->hovered_task >= 0 &&
            (size_t)shell->hovered_task < shell->task_region_count) {
        uint32_t id = shell->task_regions[shell->hovered_task].window_id;
        if (button == BTN_LEFT) {
            context_hide(shell);
            shady_shell_v1_activate_window(shell->shady_shell, id);
            wl_display_flush(shell->display);
        } else if (button == BTN_RIGHT) {
            int x = (int)shell->task_regions[shell->hovered_task].x0;
            context_show(shell, id, x);
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
        uint32_t bind_version = version < 4 ? version : 4;
        shell->shady_shell = wl_registry_bind(registry, name,
            &shady_shell_v1_interface, bind_version);
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
    shell->hovered_workspace = -1;
    shell->hovered_task = -1;
    shell->hovered_context_row = -1;
    shell->hovered_quick_row = -1;
    shell->hovered_result = -1;
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
    quick_hide(shell);
    context_hide(shell);
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
    ui_load_theme();
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
