#define _POSIX_C_SOURCE 200809L

#include "core.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "render.h"

struct shell_timer {
    struct wl_list link;
    uint64_t due_ms;
    shell_timer_fn fn;
    void *data;
};

struct shell_watch {
    struct wl_list link;
    int fd;
    short events;
    shell_watch_fn fn;
    void *data;
    bool removed;
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ---- outputs ---------------------------------------------------------- */

static void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y,
        int32_t pw, int32_t ph, int32_t subpixel, const char *make, const char *model,
        int32_t transform) {
    (void)data; (void)wl_output; (void)x; (void)y; (void)pw; (void)ph;
    (void)subpixel; (void)make; (void)model; (void)transform;
}

static void output_mode(void *data, struct wl_output *wl_output, uint32_t flags,
        int32_t width, int32_t height, int32_t refresh) {
    (void)data; (void)wl_output; (void)flags; (void)width; (void)height; (void)refresh;
}

static void surface_update_scale(struct shell_surface *surface);

static void output_done(void *data, struct wl_output *wl_output) {
    (void)wl_output;
    struct shell_output *output = data;
    struct shell_core *core = output->core;
    bool first = !output->ready;
    output->ready = true;
    if (first && core->listener->output_added)
        core->listener->output_added(core->listener_data, output);
    struct shell_surface *surface;
    wl_list_for_each(surface, &core->surfaces, link) surface_update_scale(surface);
}

static void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
    (void)wl_output;
    struct shell_output *output = data;
    output->scale = factor > 0 ? factor : 1;
}

static void output_name(void *data, struct wl_output *wl_output, const char *name) {
    (void)wl_output;
    struct shell_output *output = data;
    snprintf(output->name, sizeof(output->name), "%s", name ? name : "");
}

static void output_description(void *data, struct wl_output *wl_output,
        const char *description) {
    (void)data; (void)wl_output; (void)description;
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void output_destroy(struct shell_output *output) {
    struct shell_core *core = output->core;
    struct shell_surface *surface;
    wl_list_for_each(surface, &core->surfaces, link) {
        for (size_t i = 0; i < surface->entered_count; i++) {
            if (surface->entered[i] != output) continue;
            surface->entered[i] = surface->entered[--surface->entered_count];
            break;
        }
        if (surface->output == output) surface->output = NULL;
    }
    if (output->ready && core->listener->output_removed)
        core->listener->output_removed(core->listener_data, output);
    wl_list_remove(&output->link);
    if (wl_output_get_version(output->wl_output) >= WL_OUTPUT_RELEASE_SINCE_VERSION)
        wl_output_release(output->wl_output);
    else
        wl_output_destroy(output->wl_output);
    free(output);
    wl_list_for_each(surface, &core->surfaces, link) surface_update_scale(surface);
}

/* ---- surfaces --------------------------------------------------------- */

/* The buffer scale follows the outputs the surface is on: the highest
 * scale among them, else its assigned output, else the highest output. */
static void surface_update_scale(struct shell_surface *surface) {
    int32_t scale = 0;
    for (size_t i = 0; i < surface->entered_count; i++)
        if (surface->entered[i]->scale > scale) scale = surface->entered[i]->scale;
    if (!scale && surface->output) scale = surface->output->scale;
    if (!scale) {
        struct shell_output *output;
        wl_list_for_each(output, &surface->core->outputs, link)
            if (output->scale > scale) scale = output->scale;
    }
    if (scale < 1) scale = 1;
    if (scale != surface->scale) {
        if (surface->scale)
            fprintf(stderr, "shady-shell: %s scale %d\n", surface->name, scale);
        surface->scale = scale;
        shell_surface_redraw(surface);
    }
}

static struct shell_output *output_from_wl(struct shell_core *core, struct wl_output *wl_output) {
    struct shell_output *output;
    wl_list_for_each(output, &core->outputs, link)
        if (output->wl_output == wl_output) return output;
    return NULL;
}

static void surface_enter(void *data, struct wl_surface *wl_surface, struct wl_output *wl_output) {
    (void)wl_surface;
    struct shell_surface *surface = data;
    struct shell_output *output = output_from_wl(surface->core, wl_output);
    if (!output || surface->entered_count == sizeof(surface->entered) / sizeof(surface->entered[0]))
        return;
    for (size_t i = 0; i < surface->entered_count; i++)
        if (surface->entered[i] == output) return;
    surface->entered[surface->entered_count++] = output;
    surface_update_scale(surface);
}

static void surface_leave(void *data, struct wl_surface *wl_surface, struct wl_output *wl_output) {
    (void)wl_surface;
    struct shell_surface *surface = data;
    struct shell_output *output = output_from_wl(surface->core, wl_output);
    for (size_t i = 0; i < surface->entered_count; i++) {
        if (surface->entered[i] != output) continue;
        surface->entered[i] = surface->entered[--surface->entered_count];
        surface_update_scale(surface);
        return;
    }
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
        uint32_t serial, uint32_t width, uint32_t height) {
    struct shell_surface *surface = data;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
    if ((width && width != surface->width) || (height && height != surface->height))
        fprintf(stderr, "shady-shell: %s size %ux%u\n", surface->name,
            width ? width : surface->width, height ? height : surface->height);
    if (width) surface->width = width;
    if (height) surface->height = height;
    surface->configured = true;
    shell_surface_redraw(surface);
}

static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
    (void)layer_surface;
    struct shell_surface *surface = data;
    if (surface->handler->closed) surface->handler->closed(surface->data, surface);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
    .configure = layer_configure,
    .closed = layer_closed,
};

struct shell_surface *shell_surface_create(struct shell_core *core,
        const struct shell_surface_config *config,
        const struct shell_surface_handler *handler, void *data) {
    if (!core->compositor || !core->layer_shell) return NULL;
    struct shell_surface *surface = calloc(1, sizeof(*surface));
    if (!surface) return NULL;
    surface->core = core;
    surface->handler = handler;
    surface->data = data;
    surface->output = config->output;
    surface->width = config->width;
    surface->height = config->height;
    snprintf(surface->name, sizeof(surface->name), "%s", config->name_space ? config->name_space : "surface");
    surface->wl_surface = wl_compositor_create_surface(core->compositor);
    if (!surface->wl_surface) {
        free(surface);
        return NULL;
    }
    wl_surface_add_listener(surface->wl_surface, &surface_listener, surface);
    surface->layer_surface = zwlr_layer_shell_v1_get_layer_surface(core->layer_shell,
        surface->wl_surface, config->output ? config->output->wl_output : NULL,
        config->layer, config->name_space);
    if (!surface->layer_surface) {
        wl_surface_destroy(surface->wl_surface);
        free(surface);
        return NULL;
    }
    zwlr_layer_surface_v1_add_listener(surface->layer_surface, &layer_listener, surface);
    zwlr_layer_surface_v1_set_size(surface->layer_surface, config->width, config->height);
    surface->requested_width = config->width;
    surface->requested_height = config->height;
    zwlr_layer_surface_v1_set_anchor(surface->layer_surface, config->anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(surface->layer_surface, config->exclusive_zone);
    zwlr_layer_surface_v1_set_margin(surface->layer_surface, config->margin_top,
        config->margin_right, config->margin_bottom, config->margin_left);
    zwlr_layer_surface_v1_set_keyboard_interactivity(surface->layer_surface, config->keyboard);
    wl_list_insert(core->surfaces.prev, &surface->link);
    surface_update_scale(surface);
    surface->dirty = false; /* nothing to paint before the first configure */
    wl_surface_commit(surface->wl_surface);
    return surface;
}

void shell_surface_destroy(struct shell_surface *surface) {
    if (!surface) return;
    struct shell_core *core = surface->core;
    if (core->pointer_focus == surface) core->pointer_focus = NULL;
    if (core->keyboard_focus == surface) core->keyboard_focus = NULL;
    if (core->renderer) core->renderer->impl->surface_finish(core->renderer, surface);
    if (surface->frame) wl_callback_destroy(surface->frame);
    zwlr_layer_surface_v1_destroy(surface->layer_surface);
    wl_surface_destroy(surface->wl_surface);
    wl_list_remove(&surface->link);
    free(surface);
}

void shell_surface_redraw(struct shell_surface *surface) {
    if (surface) surface->dirty = true;
}

void shell_surface_set_size(struct shell_surface *surface, uint32_t width, uint32_t height) {
    if (!surface || (surface->requested_width == width && surface->requested_height == height))
        return;
    surface->requested_width = width;
    surface->requested_height = height;
    zwlr_layer_surface_v1_set_size(surface->layer_surface, width, height);
    wl_surface_commit(surface->wl_surface);
}

void shell_core_redraw_all(struct shell_core *core) {
    struct shell_surface *surface;
    wl_list_for_each(surface, &core->surfaces, link) surface->dirty = true;
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    (void)time;
    struct shell_surface *surface = data;
    wl_callback_destroy(callback);
    surface->frame = NULL;
}

static const struct wl_callback_listener frame_listener = {
    .done = frame_done,
};

static void paint(struct shell_surface *surface) {
    struct shell_core *core = surface->core;
    int width = (int)surface->width * surface->scale;
    int height = (int)surface->height * surface->scale;
    if (width <= 0 || height <= 0) return;
    cairo_surface_t *image = core->renderer->impl->begin(core->renderer, surface, width, height);
    if (!image) return;
    cairo_t *cr = cairo_create(image);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_scale(cr, surface->scale, surface->scale);
    surface->handler->draw(surface->data, surface, cr, surface->width, surface->height);
    cairo_destroy(cr);
    cairo_surface_flush(image);

    wl_surface_set_buffer_scale(surface->wl_surface, surface->scale);
    surface->frame = wl_surface_frame(surface->wl_surface);
    wl_callback_add_listener(surface->frame, &frame_listener, surface);
    if (!core->renderer->impl->end(core->renderer, surface)) {
        /* Nothing was committed, so no frame callback will arrive. */
        wl_callback_destroy(surface->frame);
        surface->frame = NULL;
    }
}

/* Paint every dirty surface that is configured and not waiting on a frame. */
static void paint_dirty(struct shell_core *core) {
    struct shell_surface *surface;
    wl_list_for_each(surface, &core->surfaces, link) {
        if (!surface->dirty || !surface->configured || surface->frame) continue;
        surface->dirty = false;
        paint(surface);
    }
}

/* ---- input ------------------------------------------------------------ */

static struct shell_surface *surface_from_wl(struct shell_core *core, struct wl_surface *wl_surface) {
    struct shell_surface *surface;
    wl_list_for_each(surface, &core->surfaces, link)
        if (surface->wl_surface == wl_surface) return surface;
    return NULL;
}

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *wl_surface, wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)serial;
    struct shell_core *core = data;
    core->pointer_focus = surface_from_wl(core, wl_surface);
    core->pointer_x = wl_fixed_to_double(sx);
    core->pointer_y = wl_fixed_to_double(sy);
    struct shell_surface *s = core->pointer_focus;
    if (s && s->handler->pointer_motion)
        s->handler->pointer_motion(s->data, s, core->pointer_x, core->pointer_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *wl_surface) {
    (void)pointer; (void)serial; (void)wl_surface;
    struct shell_core *core = data;
    struct shell_surface *s = core->pointer_focus;
    core->pointer_focus = NULL;
    if (s && s->handler->pointer_leave) s->handler->pointer_leave(s->data, s);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
        wl_fixed_t sx, wl_fixed_t sy) {
    (void)pointer; (void)time;
    struct shell_core *core = data;
    core->pointer_x = wl_fixed_to_double(sx);
    core->pointer_y = wl_fixed_to_double(sy);
    struct shell_surface *s = core->pointer_focus;
    if (s && s->handler->pointer_motion)
        s->handler->pointer_motion(s->data, s, core->pointer_x, core->pointer_y);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
        uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer; (void)serial; (void)time;
    struct shell_core *core = data;
    struct shell_surface *s = core->pointer_focus;
    if (s && s->handler->pointer_button)
        s->handler->pointer_button(s->data, s, button,
            state == WL_POINTER_BUTTON_STATE_PRESSED);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
        uint32_t axis, wl_fixed_t value) {
    (void)data; (void)pointer; (void)time; (void)axis; (void)value;
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data; (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {
    (void)data; (void)pointer; (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
        uint32_t axis) {
    (void)data; (void)pointer; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
        int32_t discrete) {
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

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
        int32_t fd, uint32_t size) {
    (void)keyboard;
    struct shell_core *core = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) return;
    if (core->xkb_state) xkb_state_unref(core->xkb_state);
    if (core->xkb_keymap) xkb_keymap_unref(core->xkb_keymap);
    core->xkb_keymap = xkb_keymap_new_from_string(core->xkb_context, map,
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    core->xkb_state = core->xkb_keymap ? xkb_state_new(core->xkb_keymap) : NULL;
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        struct wl_surface *wl_surface, struct wl_array *keys) {
    (void)keyboard; (void)serial; (void)keys;
    struct shell_core *core = data;
    core->keyboard_focus = surface_from_wl(core, wl_surface);
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        struct wl_surface *wl_surface) {
    (void)keyboard; (void)serial; (void)wl_surface;
    struct shell_core *core = data;
    core->keyboard_focus = NULL;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        uint32_t time, uint32_t key, uint32_t state) {
    (void)keyboard; (void)serial; (void)time;
    struct shell_core *core = data;
    struct shell_surface *s = core->keyboard_focus;
    if (!s || !s->handler->key || !core->xkb_state || state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;
    xkb_keycode_t code = key + 8;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(core->xkb_state, code);
    char text[32] = {0};
    int n = xkb_state_key_get_utf8(core->xkb_state, code, text, sizeof(text));
    if (n <= 0 || (unsigned char)text[0] < 0x20 || text[0] == 0x7f) text[0] = '\0';
    s->handler->key(s->data, s, sym, text);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    (void)keyboard; (void)serial;
    struct shell_core *core = data;
    if (core->xkb_state)
        xkb_state_update_mask(core->xkb_state, depressed, latched, locked, 0, 0, group);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
        int32_t delay) {
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

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct shell_core *core = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !core->pointer) {
        core->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(core->pointer, &pointer_listener, core);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && core->pointer) {
        wl_pointer_release(core->pointer);
        core->pointer = NULL;
        core->pointer_focus = NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !core->keyboard) {
        core->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(core->keyboard, &keyboard_listener, core);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && core->keyboard) {
        wl_keyboard_release(core->keyboard);
        core->keyboard = NULL;
        core->keyboard_focus = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* ---- registry --------------------------------------------------------- */

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    struct shell_core *core = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        core->compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
            min_u32(version, 4));
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        core->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, wl_seat_interface.name) && !core->seat) {
        core->seat = wl_registry_bind(registry, name, &wl_seat_interface, min_u32(version, 7));
        wl_seat_add_listener(core->seat, &seat_listener, core);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        core->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface,
            min_u32(version, 4));
    } else if (!strcmp(interface, wl_output_interface.name)) {
        struct shell_output *output = calloc(1, sizeof(*output));
        if (!output) return;
        output->core = core;
        output->global_name = name;
        output->scale = 1;
        output->wl_output = wl_registry_bind(registry, name, &wl_output_interface,
            min_u32(version, 4));
        wl_output_add_listener(output->wl_output, &output_listener, output);
        wl_list_insert(core->outputs.prev, &output->link);
    } else if (core->listener->global) {
        core->listener->global(core->listener_data, registry, name, interface, version);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    struct shell_core *core = data;
    struct shell_output *output, *tmp;
    wl_list_for_each_safe(output, tmp, &core->outputs, link) {
        if (output->global_name == name) {
            output_destroy(output);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* ---- lifecycle and loop ----------------------------------------------- */

bool shell_core_init(struct shell_core *core,
        const struct shell_core_listener *listener, void *data) {
    memset(core, 0, sizeof(*core));
    wl_list_init(&core->outputs);
    wl_list_init(&core->surfaces);
    wl_list_init(&core->timers);
    wl_list_init(&core->watches);
    core->listener = listener;
    core->listener_data = data;
    core->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!core->xkb_context) return false;

    core->display = wl_display_connect(NULL);
    if (!core->display) {
        fprintf(stderr, "shady-shell: failed to connect to Wayland display\n");
        return false;
    }
    core->registry = wl_display_get_registry(core->display);
    wl_registry_add_listener(core->registry, &registry_listener, core);
    /* Globals, then the outputs' and seat's initial events. */
    if (wl_display_roundtrip(core->display) < 0 || wl_display_roundtrip(core->display) < 0)
        return false;
    if (!core->compositor || !core->shm || !core->layer_shell) {
        fprintf(stderr, "shady-shell: compositor lacks wl_compositor, wl_shm or layer-shell\n");
        return false;
    }
    core->renderer = shell_renderer_create(core);
    if (!core->renderer) return false;
    fprintf(stderr, "shady-shell: renderer %s\n", core->renderer->impl->name);
    core->running = true;
    return true;
}

void shell_core_stop(struct shell_core *core) {
    core->running = false;
}

struct shell_timer *shell_timer_add(struct shell_core *core, uint32_t ms,
        shell_timer_fn fn, void *data) {
    struct shell_timer *timer = calloc(1, sizeof(*timer));
    if (!timer) return NULL;
    timer->due_ms = now_ms() + ms;
    timer->fn = fn;
    timer->data = data;
    wl_list_insert(&core->timers, &timer->link);
    return timer;
}

void shell_timer_cancel(struct shell_timer *timer) {
    if (!timer) return;
    wl_list_remove(&timer->link);
    free(timer);
}

struct shell_watch *shell_watch_add(struct shell_core *core, int fd, short events,
        shell_watch_fn fn, void *data) {
    struct shell_watch *watch = calloc(1, sizeof(*watch));
    if (!watch) return NULL;
    watch->fd = fd;
    watch->events = events;
    watch->fn = fn;
    watch->data = data;
    wl_list_insert(core->watches.prev, &watch->link);
    return watch;
}

/* Watches are freed by the loop, so removing one from its own callback (or
 * another's) is safe. */
void shell_watch_remove(struct shell_watch *watch) {
    if (watch) watch->removed = true;
}

static void reap_watches(struct shell_core *core) {
    struct shell_watch *watch, *tmp;
    wl_list_for_each_safe(watch, tmp, &core->watches, link) {
        if (!watch->removed) continue;
        wl_list_remove(&watch->link);
        free(watch);
    }
}

static int next_timeout(struct shell_core *core) {
    if (wl_list_empty(&core->timers)) return -1;
    uint64_t now = now_ms();
    uint64_t soonest = UINT64_MAX;
    struct shell_timer *timer;
    wl_list_for_each(timer, &core->timers, link)
        if (timer->due_ms < soonest) soonest = timer->due_ms;
    if (soonest <= now) return 0;
    uint64_t wait = soonest - now;
    return wait > 60000 ? 60000 : (int)wait;
}

/* Fire due timers one at a time; a callback may add or cancel others. */
static void run_timers(struct shell_core *core) {
    for (;;) {
        uint64_t now = now_ms();
        struct shell_timer *due = NULL, *timer;
        wl_list_for_each(timer, &core->timers, link) {
            if (timer->due_ms <= now) {
                due = timer;
                break;
            }
        }
        if (!due) return;
        shell_timer_fn fn = due->fn;
        void *data = due->data;
        shell_timer_cancel(due);
        fn(data);
    }
}

#define MAX_POLL 32

int shell_core_run(struct shell_core *core) {
    int display_fd = wl_display_get_fd(core->display);
    while (core->running) {
        if (wl_display_dispatch_pending(core->display) < 0) return -1;
        paint_dirty(core);
        if (wl_display_flush(core->display) < 0 && errno != EAGAIN) return -1;

        struct pollfd fds[MAX_POLL];
        struct shell_watch *slot[MAX_POLL];
        size_t count = 0;
        fds[count++] = (struct pollfd){ .fd = display_fd, .events = POLLIN };
        struct shell_watch *watch;
        wl_list_for_each(watch, &core->watches, link) {
            if (watch->removed || count == MAX_POLL) continue;
            slot[count] = watch;
            fds[count++] = (struct pollfd){ .fd = watch->fd, .events = watch->events };
        }

        int rc = poll(fds, count, next_timeout(core));
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
        if ((fds[0].revents & POLLIN) && wl_display_dispatch(core->display) < 0) return -1;
        for (size_t i = 1; i < count; i++)
            if (fds[i].revents && !slot[i]->removed)
                slot[i]->fn(slot[i]->fd, fds[i].revents, slot[i]->data);
        reap_watches(core);
        run_timers(core);
    }
    return 0;
}

void shell_core_finish(struct shell_core *core) {
    struct shell_surface *surface, *stmp;
    wl_list_for_each_safe(surface, stmp, &core->surfaces, link) shell_surface_destroy(surface);
    struct shell_timer *timer, *ttmp;
    wl_list_for_each_safe(timer, ttmp, &core->timers, link) shell_timer_cancel(timer);
    struct shell_watch *watch;
    wl_list_for_each(watch, &core->watches, link) watch->removed = true;
    reap_watches(core);
    if (core->renderer) core->renderer->impl->destroy(core->renderer);
    core->renderer = NULL;
    struct shell_output *output, *otmp;
    wl_list_for_each_safe(output, otmp, &core->outputs, link) {
        output->ready = false; /* no output_removed callbacks during teardown */
        output_destroy(output);
    }
    if (core->keyboard) wl_keyboard_release(core->keyboard);
    if (core->pointer) wl_pointer_release(core->pointer);
    if (core->seat) wl_seat_release(core->seat);
    if (core->layer_shell) zwlr_layer_shell_v1_destroy(core->layer_shell);
    if (core->shm) wl_shm_destroy(core->shm);
    if (core->compositor) wl_compositor_destroy(core->compositor);
    if (core->registry) wl_registry_destroy(core->registry);
    if (core->display) wl_display_disconnect(core->display);
    if (core->xkb_state) xkb_state_unref(core->xkb_state);
    if (core->xkb_keymap) xkb_keymap_unref(core->xkb_keymap);
    if (core->xkb_context) xkb_context_unref(core->xkb_context);
    memset(core, 0, sizeof(*core));
}
