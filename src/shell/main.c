#define _POSIX_C_SOURCE 200809L

#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pango/pangocairo.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "shady-shell-v1-client-protocol.h"

#define BAR_HEIGHT 32
#define MAX_WORKSPACES 16
#define WORKSPACE_NAME_MAX 64
#define WINDOW_TEXT_MAX 256

struct shell_buffer {
    struct wl_buffer *wl_buffer;
    void *data;
    size_t size;
};

struct workspace_region {
    double x0;
    double x1;
};

struct shell {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct shady_shell_v1 *shady_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;

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
};

static void copy_text(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
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
    .done = protocol_done,
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface,
        wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)serial;
    (void)surface;
    struct shell *shell = data;
    shell->pointer_x = wl_fixed_to_double(surface_x);
    shell->pointer_y = wl_fixed_to_double(surface_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
        uint32_t serial, struct wl_surface *surface) {
    (void)data;
    (void)pointer;
    (void)serial;
    (void)surface;
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
    if (!shell->shady_shell || button != BTN_LEFT ||
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
}

int main(void) {
    struct shell shell = {0};
    if (!shell_init(&shell)) {
        shell_finish(&shell);
        return 1;
    }

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
