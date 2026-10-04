#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core.h"
#include "render.h"

/* Two buffers per surface: one can sit with the compositor while cairo
 * paints the other. A third request while both are busy allocates a
 * temporary buffer that is freed on release. */
#define SHM_BUFFERS 2

struct shm_buffer {
    struct wl_buffer *wl_buffer;
    cairo_surface_t *image;
    void *data;
    size_t size;
    int width, height;
    bool busy;
    bool orphan; /* not owned by a surface any more: free on release */
};

struct shm_surface {
    struct shm_buffer *buffers[SHM_BUFFERS];
    struct shm_buffer *current; /* between begin() and end() */
};

static void buffer_free(struct shm_buffer *buffer) {
    if (!buffer) return;
    if (buffer->image) cairo_surface_destroy(buffer->image);
    if (buffer->wl_buffer) wl_buffer_destroy(buffer->wl_buffer);
    if (buffer->data) munmap(buffer->data, buffer->size);
    free(buffer);
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    (void)wl_buffer;
    struct shm_buffer *buffer = data;
    buffer->busy = false;
    if (buffer->orphan) buffer_free(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

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

static struct shm_buffer *buffer_create(struct shell_core *core, int width, int height) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    size_t size = (size_t)stride * (size_t)height;
    int fd = create_shm_file(size);
    if (fd < 0) return NULL;
    struct shm_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        close(fd);
        return NULL;
    }
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->data == MAP_FAILED) {
        buffer->data = NULL;
        close(fd);
        buffer_free(buffer);
        return NULL;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(core->shm, fd, (int32_t)size);
    close(fd);
    buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride,
        WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    buffer->image = cairo_image_surface_create_for_data(buffer->data, CAIRO_FORMAT_ARGB32,
        width, height, stride);
    if (!buffer->wl_buffer || cairo_surface_status(buffer->image) != CAIRO_STATUS_SUCCESS) {
        buffer_free(buffer);
        return NULL;
    }
    wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
    return buffer;
}

/* Drop a buffer the surface no longer wants; if the compositor still holds
 * it, free it when released. */
static void buffer_drop(struct shm_buffer *buffer) {
    if (!buffer) return;
    if (buffer->busy) buffer->orphan = true;
    else buffer_free(buffer);
}

static cairo_surface_t *shm_begin(struct shell_renderer *renderer,
        struct shell_surface *surface, int width, int height) {
    struct shm_surface *state = surface->render_data;
    if (!state) {
        state = calloc(1, sizeof(*state));
        if (!state) return NULL;
        surface->render_data = state;
    }
    struct shm_buffer *chosen = NULL;
    for (int i = 0; i < SHM_BUFFERS && !chosen; i++) {
        struct shm_buffer *buffer = state->buffers[i];
        if (buffer && !buffer->busy && (buffer->width != width || buffer->height != height)) {
            buffer_drop(buffer);
            state->buffers[i] = NULL;
            buffer = NULL;
        }
        if (!buffer) {
            buffer = buffer_create(renderer->core, width, height);
            state->buffers[i] = buffer;
        }
        if (buffer && !buffer->busy) chosen = buffer;
    }
    if (!chosen) {
        chosen = buffer_create(renderer->core, width, height);
        if (!chosen) return NULL;
        chosen->orphan = true;
    }
    state->current = chosen;
    return chosen->image;
}

static bool shm_end(struct shell_renderer *renderer, struct shell_surface *surface) {
    (void)renderer;
    struct shm_surface *state = surface->render_data;
    struct shm_buffer *buffer = state ? state->current : NULL;
    if (!buffer) return false;
    state->current = NULL;
    buffer->busy = true;
    wl_surface_attach(surface->wl_surface, buffer->wl_buffer, 0, 0);
    wl_surface_damage_buffer(surface->wl_surface, 0, 0, buffer->width, buffer->height);
    wl_surface_commit(surface->wl_surface);
    return true;
}

static void shm_surface_finish(struct shell_renderer *renderer, struct shell_surface *surface) {
    (void)renderer;
    struct shm_surface *state = surface->render_data;
    if (!state) return;
    for (int i = 0; i < SHM_BUFFERS; i++) buffer_drop(state->buffers[i]);
    free(state);
    surface->render_data = NULL;
}

static void shm_destroy(struct shell_renderer *renderer) {
    free(renderer);
}

static const struct shell_renderer_impl shm_impl = {
    .name = "shm",
    .destroy = shm_destroy,
    .begin = shm_begin,
    .end = shm_end,
    .surface_finish = shm_surface_finish,
};

struct shell_renderer *shell_renderer_shm_create(struct shell_core *core) {
    struct shell_renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer) return NULL;
    renderer->impl = &shm_impl;
    renderer->core = core;
    return renderer;
}
