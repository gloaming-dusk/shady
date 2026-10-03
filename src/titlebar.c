#include "titlebar.h"

#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <stdint.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

#include "shady.h"

#define SHADY_DRM_FORMAT_ARGB8888 ((uint32_t)'A' | ((uint32_t)'R' << 8) | \
    ((uint32_t)'2' << 16) | ((uint32_t)'4' << 24))

struct shady_titlebar_buffer {
    struct wlr_buffer base;
    uint32_t *pixels;
    size_t stride;
};

static void titlebar_buffer_destroy(struct wlr_buffer *buffer) {
    struct shady_titlebar_buffer *title =
        wl_container_of(buffer, title, base);
    wlr_buffer_finish(&title->base);
    free(title->pixels);
    free(title);
}

static bool titlebar_buffer_begin_data_ptr_access(struct wlr_buffer *buffer,
        uint32_t flags, void **data, uint32_t *format, size_t *stride) {
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
    struct shady_titlebar_buffer *title =
        wl_container_of(buffer, title, base);
    *data = title->pixels;
    *format = SHADY_DRM_FORMAT_ARGB8888;
    *stride = title->stride;
    return true;
}

static void titlebar_buffer_end_data_ptr_access(struct wlr_buffer *buffer) {
    (void)buffer;
}

static const struct wlr_buffer_impl titlebar_buffer_impl = {
    .destroy = titlebar_buffer_destroy,
    .begin_data_ptr_access = titlebar_buffer_begin_data_ptr_access,
    .end_data_ptr_access = titlebar_buffer_end_data_ptr_access,
};

static bool titlebar_focused(const struct shady_toplevel *toplevel) {
    return toplevel && !wl_list_empty(&toplevel->server->toplevels) &&
        toplevel->server->toplevels.next == &toplevel->link;
}

static struct shady_titlebar_buffer *render_titlebar(
        struct shady_toplevel *toplevel, int width, int height) {
    if (width <= 0 || height <= 0) return NULL;

    struct shady_titlebar_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return NULL;

    buffer->stride = (size_t)width * 4;
    buffer->pixels = calloc((size_t)height, buffer->stride);
    if (!buffer->pixels) {
        free(buffer);
        return NULL;
    }

    wlr_buffer_init(&buffer->base, &titlebar_buffer_impl, width, height);

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)buffer->pixels, CAIRO_FORMAT_ARGB32,
        width, height, (int)buffer->stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    const float *bg = titlebar_focused(toplevel)
        ? toplevel->server->config.window_titlebar_focus_color
        : toplevel->server->config.window_titlebar_color;
    const float *fg = toplevel->server->config.window_titlebar_text_color;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, bg[0], bg[1], bg[2], 0.96);
    cairo_paint(cr);

    const char *title = toplevel->xdg_toplevel->title;
    if (!title || !*title) title = toplevel->xdg_toplevel->app_id;
    if (!title || !*title) title = "Shady";

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font =
        pango_font_description_from_string("Sans SemiBold 10");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, title, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_width(layout, (width > 28 ? width - 28 : width) * PANGO_SCALE);

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);
    (void)text_w;
    double y = (height - text_h) * 0.5;
    if (y < 0) y = 0;

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 1.0);
    cairo_move_to(cr, 10.0, y);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
    return buffer;
}

bool shady_titlebar_init(struct shady_toplevel *toplevel) {
    if (!toplevel || !toplevel->scene_tree) return false;
    toplevel->titlebar_tree = wlr_scene_tree_create(toplevel->scene_tree);
    if (!toplevel->titlebar_tree) return false;
    toplevel->titlebar_scene_buffer =
        wlr_scene_buffer_create(toplevel->titlebar_tree, NULL);
    if (!toplevel->titlebar_scene_buffer) {
        wlr_scene_node_destroy(&toplevel->titlebar_tree->node);
        toplevel->titlebar_tree = NULL;
        return false;
    }
    wlr_scene_node_set_enabled(&toplevel->titlebar_tree->node, false);
    return true;
}

void shady_titlebar_refresh(struct shady_toplevel *toplevel) {
    if (!toplevel || !toplevel->titlebar_tree ||
            !toplevel->titlebar_scene_buffer ||
            !toplevel->xdg_toplevel || !toplevel->xdg_toplevel->base) {
        return;
    }

    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
    int width = surface ? surface->current.width : 0;
    int height = (int)(toplevel->server->config.window_titlebar_height + 0.5f);
    bool enabled = toplevel->mapped &&
        toplevel->server->config.window_titlebar &&
        !toplevel->fullscreen && width > 0 && height > 0;

    wlr_scene_node_set_enabled(&toplevel->titlebar_tree->node, enabled);
    if (!enabled) return;

    struct shady_titlebar_buffer *new_buffer =
        render_titlebar(toplevel, width, height);
    if (!new_buffer) return;

    if (toplevel->titlebar_texture) {
        wlr_texture_destroy(toplevel->titlebar_texture);
        toplevel->titlebar_texture = NULL;
    }
    if (toplevel->server->renderer) {
        toplevel->titlebar_texture = wlr_texture_from_buffer(
            toplevel->server->renderer, &new_buffer->base);
    }

    wlr_scene_buffer_set_buffer(toplevel->titlebar_scene_buffer,
        &new_buffer->base);
    wlr_scene_buffer_set_dest_size(toplevel->titlebar_scene_buffer,
        width, height);
    wlr_scene_buffer_set_opacity(toplevel->titlebar_scene_buffer,
        toplevel->server->config.window_opacity);
    wlr_scene_node_set_position(&toplevel->titlebar_tree->node,
        0, -height);

    if (toplevel->titlebar_buffer) {
        wlr_buffer_drop(toplevel->titlebar_buffer);
    }
    toplevel->titlebar_buffer = &new_buffer->base;
    toplevel->titlebar_width = width;
    toplevel->titlebar_height = height;
}

void shady_titlebar_global_fini(void) {
    PangoFontMap *map = pango_cairo_font_map_get_default();
    if (map && PANGO_IS_FC_FONT_MAP(map)) {
        pango_fc_font_map_shutdown(PANGO_FC_FONT_MAP(map));
    }
    FcFini();
}

void shady_titlebar_fini(struct shady_toplevel *toplevel) {
    if (!toplevel) return;
    if (toplevel->titlebar_texture) {
        wlr_texture_destroy(toplevel->titlebar_texture);
        toplevel->titlebar_texture = NULL;
    }
    if (toplevel->titlebar_scene_buffer) {
        wlr_scene_buffer_set_buffer(toplevel->titlebar_scene_buffer, NULL);
    }
    if (toplevel->titlebar_buffer) {
        wlr_buffer_drop(toplevel->titlebar_buffer);
        toplevel->titlebar_buffer = NULL;
    }
    toplevel->titlebar_scene_buffer = NULL;
    toplevel->titlebar_tree = NULL;
    toplevel->titlebar_width = 0;
    toplevel->titlebar_height = 0;
}
