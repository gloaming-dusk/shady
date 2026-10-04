#ifndef SHADY_SHELL_CORE_H
#define SHADY_SHELL_CORE_H

/*
 * Shell core: the Wayland connection, outputs, seat input, layer surfaces
 * and the poll loop. It knows nothing about bars or menus; the shell UI in
 * shell.c and friends is one client of it. See docs/SHELL_DESIGN.md.
 *
 * Surfaces are redrawn lazily: shell_surface_redraw() only marks a surface
 * dirty, and the loop paints dirty surfaces once per iteration, never
 * faster than the compositor's frame callbacks. Any number of state changes
 * in one batch of events cost one repaint.
 */

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-protocol.h"

struct shell_core;
struct shell_surface;
struct shell_renderer;

struct shell_output {
    struct wl_list link;
    struct shell_core *core;
    struct wl_output *wl_output;
    uint32_t global_name;
    int32_t scale;
    char name[64];
    bool ready; /* initial output.done received */
};

struct shell_core_listener {
    void (*output_added)(void *data, struct shell_output *output);
    /* The output is still valid during the call and freed right after. */
    void (*output_removed)(void *data, struct shell_output *output);
    /* Globals the core does not handle itself (e.g. shady_shell_v1). */
    void (*global)(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version);
};

struct shell_core {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct shell_renderer *renderer;

    struct wl_list outputs; /* shell_output.link */
    struct wl_list surfaces; /* shell_surface.link */
    struct wl_list timers; /* shell_timer.link */
    struct wl_list watches; /* shell_watch.link */

    struct shell_surface *pointer_focus;
    struct shell_surface *keyboard_focus;
    double pointer_x, pointer_y;

    const struct shell_core_listener *listener;
    void *listener_data;
    bool running;
};

/* ---- core ------------------------------------------------------------- */

bool shell_core_init(struct shell_core *core,
    const struct shell_core_listener *listener, void *data);
/* Run until shell_core_stop() or the connection fails. */
int shell_core_run(struct shell_core *core);
void shell_core_stop(struct shell_core *core);
void shell_core_finish(struct shell_core *core);

/* ---- timers and fd watches -------------------------------------------- */

struct shell_timer;
typedef void (*shell_timer_fn)(void *data);
/* One-shot timer after `ms` milliseconds; freed after it fires. */
struct shell_timer *shell_timer_add(struct shell_core *core, uint32_t ms,
    shell_timer_fn fn, void *data);
void shell_timer_cancel(struct shell_timer *timer);

struct shell_watch;
/* `revents` are poll(2) bits. */
typedef void (*shell_watch_fn)(int fd, short revents, void *data);
struct shell_watch *shell_watch_add(struct shell_core *core, int fd, short events,
    shell_watch_fn fn, void *data);
void shell_watch_remove(struct shell_watch *watch);

/* ---- layer surfaces --------------------------------------------------- */

struct shell_surface_config {
    const char *name_space;
    enum zwlr_layer_shell_v1_layer layer;
    uint32_t anchor; /* ZWLR_LAYER_SURFACE_V1_ANCHOR_* */
    uint32_t width, height; /* logical px; 0 stretches along the anchors */
    int32_t exclusive_zone;
    int32_t margin_top, margin_right, margin_bottom, margin_left;
    enum zwlr_layer_surface_v1_keyboard_interactivity keyboard;
    struct shell_output *output; /* NULL: the compositor chooses */
};

struct shell_surface_handler {
    /* Paint the whole surface in logical coordinates; the context is
     * already scaled for HiDPI and cleared to transparent. */
    void (*draw)(void *data, struct shell_surface *surface, cairo_t *cr,
        double width, double height);
    /* The compositor closed the surface; destroy it from here or later. */
    void (*closed)(void *data, struct shell_surface *surface);
    void (*pointer_motion)(void *data, struct shell_surface *surface, double x, double y);
    void (*pointer_leave)(void *data, struct shell_surface *surface);
    void (*pointer_button)(void *data, struct shell_surface *surface,
        uint32_t button, bool pressed);
    /* `utf8` is the text the key produces, "" for none. */
    void (*key)(void *data, struct shell_surface *surface, xkb_keysym_t sym,
        const char *utf8);
};

struct shell_surface {
    struct wl_list link;
    struct shell_core *core;
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct shell_output *output;
    const struct shell_surface_handler *handler;
    void *data;

    uint32_t width, height; /* logical, from configure */
    int32_t scale; /* buffer scale in use */
    struct shell_output *entered[8];
    size_t entered_count;
    bool configured;
    bool dirty;
    struct wl_callback *frame;
    void *render_data; /* owned by the renderer */
    char name[32]; /* layer namespace, for logs */
};

struct shell_surface *shell_surface_create(struct shell_core *core,
    const struct shell_surface_config *config,
    const struct shell_surface_handler *handler, void *data);
/* Safe to call from the surface's own handlers. */
void shell_surface_destroy(struct shell_surface *surface);
void shell_surface_redraw(struct shell_surface *surface);
void shell_core_redraw_all(struct shell_core *core);

#endif
