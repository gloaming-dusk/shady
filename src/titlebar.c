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

/*
 * Title bars are rasterized at twice their logical size. In spatial mode a
 * window close to the camera magnifies its title bar, and the extra texels
 * keep the label crisp; the scene-graph path downsamples via dest size.
 */
#define SHADY_TITLEBAR_SCALE 2

static struct shady_titlebar_buffer *render_titlebar(
        struct shady_toplevel *toplevel, int width, int height) {
    if (width <= 0 || height <= 0) return NULL;

    struct shady_titlebar_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return NULL;

    const int scale = SHADY_TITLEBAR_SCALE;
    const int buf_w = width * scale;
    const int buf_h = height * scale;
    buffer->stride = (size_t)buf_w * 4;
    buffer->pixels = calloc((size_t)buf_h, buffer->stride);
    if (!buffer->pixels) {
        free(buffer);
        return NULL;
    }

    wlr_buffer_init(&buffer->base, &titlebar_buffer_impl, buf_w, buf_h);

    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        (unsigned char *)buffer->pixels, CAIRO_FORMAT_ARGB32,
        buf_w, buf_h, (int)buffer->stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        wlr_buffer_drop(&buffer->base);
        return NULL;
    }

    cairo_t *cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    const struct shady_config *config = &toplevel->server->config;
    const bool focused = titlebar_focused(toplevel);
    const float *bg = focused
        ? config->window_titlebar_focus_color
        : config->window_titlebar_color;
    const float *fg = config->window_titlebar_text_color;
    const float *accent = focused
        ? config->window_border_focus_color
        : config->window_border_color;

    /* Soft vertical sheen: a touch lighter at the top, base colour below. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *sheen = cairo_pattern_create_linear(0, 0, 0, height);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0,
        bg[0] + (1.0 - bg[0]) * 0.06, bg[1] + (1.0 - bg[1]) * 0.06,
        bg[2] + (1.0 - bg[2]) * 0.06, 0.97);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, bg[0], bg[1], bg[2], 0.97);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_line_width(cr, 1.0);
    /* Hairline highlight along the top edge, as if light catches a bevel. */
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, focused ? 0.07 : 0.04);
    cairo_move_to(cr, 0, 0.5);
    cairo_line_to(cr, width, 0.5);
    cairo_stroke(cr);
    /* Separator between title bar and client contents. */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.32);
    cairo_move_to(cr, 0, height - 0.5);
    cairo_line_to(cr, width, height - 0.5);
    cairo_stroke(cr);

    const char *title = toplevel->xdg_toplevel->title;
    if (!title || !*title) title = toplevel->xdg_toplevel->app_id;
    if (!title || !*title) title = "Shady";

    /* Status pip: lit with the focus accent, a dim ring otherwise. Inset so
     * it clears the rounded frame corner. */
    const double pip_r = height >= 24 ? 3.5 : 3.0;
    const double pip_x = 16.0;
    const double pip_y = height * 0.5;
    if (focused) {
        cairo_pattern_t *halo = cairo_pattern_create_radial(
            pip_x, pip_y, 0, pip_x, pip_y, pip_r * 3.2);
        cairo_pattern_add_color_stop_rgba(halo, 0.0,
            accent[0], accent[1], accent[2], 0.45);
        cairo_pattern_add_color_stop_rgba(halo, 1.0,
            accent[0], accent[1], accent[2], 0.0);
        cairo_set_source(cr, halo);
        cairo_arc(cr, pip_x, pip_y, pip_r * 3.2, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(halo);
        cairo_set_source_rgba(cr, accent[0], accent[1], accent[2], 1.0);
        cairo_arc(cr, pip_x, pip_y, pip_r, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.28);
        cairo_arc(cr, pip_x, pip_y, pip_r - 0.5, 0, 2 * G_PI);
        cairo_stroke(cr);
    }

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(
        focused ? "Sans SemiBold 9.5" : "Sans Medium 9.5");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(PANGO_SCALE / 4));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    pango_layout_set_text(layout, title, -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    const double text_x = pip_x + pip_r + 9.0;
    int avail = width - (int)text_x - 14;
    pango_layout_set_width(layout, (avail > 8 ? avail : 8) * PANGO_SCALE);

    int text_w = 0, text_h = 0;
    pango_layout_get_pixel_size(layout, &text_w, &text_h);
    (void)text_w;
    double y = (height - text_h) * 0.5;
    if (y < 0) y = 0;

    cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], focused ? 0.96 : 0.58);
    cairo_move_to(cr, text_x, y);
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
