#ifndef SHADY_SHELL_RENDER_H
#define SHADY_SHELL_RENDER_H

/*
 * Turns a surface's cairo drawing into a committed Wayland buffer.
 *
 * Two backends share this interface:
 *   - gl:  the cairo image is uploaded as a texture and composited with
 *          GLES2 through EGL. This is where shader effects attach.
 *   - shm: cairo draws straight into a shared-memory buffer. It is the
 *          fallback when EGL is unavailable and needs nothing but wl_shm.
 *
 * SHADY_SHELL_RENDERER=gl|shm picks one; the default is gl, falling back to
 * shm if EGL cannot be initialised.
 */

#include <cairo/cairo.h>
#include <stdbool.h>

struct shell_core;
struct shell_surface;

struct shell_renderer;

struct shell_renderer_impl {
    const char *name;
    void (*destroy)(struct shell_renderer *renderer);
    /* A cairo image surface of the given buffer size to paint into. */
    cairo_surface_t *(*begin)(struct shell_renderer *renderer,
        struct shell_surface *surface, int width, int height);
    /* Attach, damage and commit what was painted since begin(). */
    bool (*end)(struct shell_renderer *renderer, struct shell_surface *surface);
    /* Release per-surface resources before the wl_surface goes away. */
    void (*surface_finish)(struct shell_renderer *renderer, struct shell_surface *surface);
};

struct shell_renderer {
    const struct shell_renderer_impl *impl;
    struct shell_core *core;
};

struct shell_renderer *shell_renderer_create(struct shell_core *core);
struct shell_renderer *shell_renderer_shm_create(struct shell_core *core);
/* NULL when GL support is not built in or EGL fails to initialise. */
struct shell_renderer *shell_renderer_gl_create(struct shell_core *core);

#endif
