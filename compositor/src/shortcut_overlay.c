#include "shortcut_overlay.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "shady.h"
#include "ipc/ipc.h"
#include "ipc/shortcuts.h"
#include "modules/desktop/state.h"
#include "render/render.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DRM_FORMAT_ARGB8888_ ((uint32_t)'A' | ((uint32_t)'R' << 8) | \
	((uint32_t)'2' << 16) | ((uint32_t)'4' << 24))

/* Layout, in logical pixels. */
#define PAD 28.0
#define COLUMN_GAP 40.0
#define KEY_GAP 18.0
#define ROW_GAP 7.0
#define GROUP_GAP 14.0
#define TITLE_GAP 18.0
#define KEY_MAX 240.0
#define DESCRIPTION_MAX 340.0
#define KEYCAP_PAD 6.0
#define FONT "Sans 10"
#define FONT_BOLD "Sans Bold 10"
#define FONT_TITLE "Sans Bold 14"

struct overlay_buffer {
	struct wlr_buffer base;
	uint32_t *pixels;
	size_t stride;
};

/* One compositor per process, so one overlay. */
static struct {
	struct overlay_buffer *buffer;
	struct wlr_texture *texture;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *scene_buffer;
	struct wlr_output *output;
	struct wlr_box box; /* layout coordinates */
} overlay;

static void buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct overlay_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	wlr_buffer_finish(&buffer->base);
	free(buffer->pixels);
	free(buffer);
}

static bool buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride) {
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;
	struct overlay_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
	*data = buffer->pixels;
	*format = DRM_FORMAT_ARGB8888_;
	*stride = buffer->stride;
	return true;
}

static void buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	(void)wlr_buffer;
}

static const struct wlr_buffer_impl buffer_impl = {
	.destroy = buffer_destroy,
	.begin_data_ptr_access = buffer_begin_data_ptr_access,
	.end_data_ptr_access = buffer_end_data_ptr_access,
};

/* ---- the list ------------------------------------------------------- */

struct item {
	char *group, *keys, *description;
};

struct list {
	struct item *items;
	size_t count, capacity;
};

static void collect(const struct shady_shortcut *shortcut, void *data) {
	struct list *list = data;
	if (list->count == list->capacity) {
		size_t capacity = list->capacity ? list->capacity * 2 : 32;
		struct item *items = realloc(list->items, capacity * sizeof(*items));
		if (!items) return;
		list->items = items;
		list->capacity = capacity;
	}
	struct item *item = &list->items[list->count];
	item->group = strdup(shortcut->group ? shortcut->group : "");
	item->keys = strdup(shortcut->keys ? shortcut->keys : "");
	item->description = strdup(shortcut->description ? shortcut->description : "");
	if (!item->group || !item->keys || !item->description) {
		free(item->group);
		free(item->keys);
		free(item->description);
		return;
	}
	list->count++;
}

static void list_free(struct list *list) {
	for (size_t i = 0; i < list->count; i++) {
		free(list->items[i].group);
		free(list->items[i].keys);
		free(list->items[i].description);
	}
	free(list->items);
}

/* Groups in the order they first appear, each group's shortcuts in theirs. */
static void group_items(struct list *list) {
	struct item *sorted = malloc(list->count * sizeof(*sorted));
	bool *taken = calloc(list->count, sizeof(*taken));
	if (!sorted || !taken) {
		free(sorted);
		free(taken);
		return;
	}
	size_t n = 0;
	for (size_t i = 0; i < list->count; i++) {
		if (taken[i]) continue;
		for (size_t j = i; j < list->count; j++) {
			if (taken[j] || strcmp(list->items[j].group, list->items[i].group)) continue;
			taken[j] = true;
			sorted[n++] = list->items[j];
		}
	}
	memcpy(list->items, sorted, n * sizeof(*sorted));
	free(sorted);
	free(taken);
}

/* ---- layout and drawing ------------------------------------------------- */

enum line_kind { LINE_GROUP, LINE_ITEM };

struct line {
	enum line_kind kind;
	const struct item *item;
	double x, y;
};

static void set_text(PangoLayout *layout, const char *font, const char *text, double width) {
	PangoFontDescription *description = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, description);
	pango_font_description_free(description);
	pango_layout_set_width(layout, width > 0 ? (int)(width * PANGO_SCALE) : -1);
	pango_layout_set_ellipsize(layout, width > 0 ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE);
	pango_layout_set_text(layout, text, -1);
}

static void text_size(PangoLayout *layout, const char *font, const char *text,
		double *width, double *height) {
	set_text(layout, font, text, 0);
	int w, h;
	pango_layout_get_pixel_size(layout, &w, &h);
	if (width) *width = w;
	if (height) *height = h;
}

static void rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

static void draw_text(cairo_t *cr, PangoLayout *layout, const char *font, const char *text,
		double x, double y, double width) {
	set_text(layout, font, text, width);
	cairo_move_to(cr, x, y);
	pango_cairo_show_layout(cr, layout);
}

/* The panel for an output `max_w` by `max_h` logical pixels, drawn at
 * `scale`; its logical size goes to width and height. */
static struct overlay_buffer *render_panel(struct shady_server *server, struct list *list,
		double max_w, double max_h, double scale, int *width, int *height) {
	const struct shady_config *config = &server->config;
	cairo_surface_t *measure_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
	cairo_t *measure = cairo_create(measure_surface);
	PangoLayout *layout = pango_cairo_create_layout(measure);

	double row_h, title_h;
	text_size(layout, FONT_BOLD, "Ag", NULL, &row_h);
	row_h += 2 * 2.0; /* the keycap's height */
	text_size(layout, FONT_TITLE, "Keyboard shortcuts", NULL, &title_h);

	double key_w = 0, description_w = 0;
	for (size_t i = 0; i < list->count; i++) {
		double w;
		text_size(layout, FONT_BOLD, list->items[i].keys, &w, NULL);
		if (w + 2 * KEYCAP_PAD > key_w) key_w = w + 2 * KEYCAP_PAD;
		text_size(layout, FONT, list->items[i].description, &w, NULL);
		if (w > description_w) description_w = w;
	}
	if (key_w > KEY_MAX) key_w = KEY_MAX;
	if (description_w > DESCRIPTION_MAX) description_w = DESCRIPTION_MAX;
	const double column_w = key_w + KEY_GAP + description_w;

	/* Flow the groups down columns as tall as the output allows, and as
	 * many columns as fit across it. */
	double body_max = max_h * 0.84 - 2 * PAD - title_h - TITLE_GAP;
	if (body_max < 4 * row_h) body_max = 4 * row_h;
	size_t max_columns = (size_t)((max_w * 0.92 - 2 * PAD + COLUMN_GAP) / (column_w + COLUMN_GAP));
	if (max_columns < 1) max_columns = 1;

	struct line *lines = calloc(2 * list->count + 1, sizeof(*lines));
	size_t line_count = 0, columns = 1, hidden = 0;
	double y = 0, body_h = 0;
	const char *group = NULL;
	for (size_t i = 0; lines && i < list->count; i++) {
		const struct item *item = &list->items[i];
		bool new_group = !group || strcmp(group, item->group);
		double need = row_h + (new_group ? row_h + (y > 0 ? GROUP_GAP : 0) : 0);
		if (y > 0 && y + need > body_max) {
			if (columns == max_columns) {
				hidden = list->count - i;
				break;
			}
			columns++;
			y = 0;
			new_group = true; /* the heading again, at the top of the column */
		}
		double x = (double)(columns - 1) * (column_w + COLUMN_GAP);
		if (new_group) {
			if (y > 0) y += GROUP_GAP;
			lines[line_count++] = (struct line){ LINE_GROUP, item, x, y };
			y += row_h;
			group = item->group;
		}
		lines[line_count++] = (struct line){ LINE_ITEM, item, x, y };
		y += row_h + ROW_GAP;
		if (y > body_h) body_h = y;
	}

	double footer_h = hidden ? row_h + ROW_GAP : 0;
	double panel_w = 2 * PAD + (double)columns * column_w + (double)(columns - 1) * COLUMN_GAP;
	double hint_w;
	text_size(layout, FONT, "Esc to close", &hint_w, NULL);
	double title_w;
	text_size(layout, FONT_TITLE, "Keyboard shortcuts", &title_w, NULL);
	if (panel_w < 2 * PAD + title_w + 40 + hint_w) panel_w = 2 * PAD + title_w + 40 + hint_w;
	double panel_h = 2 * PAD + title_h + TITLE_GAP + body_h + footer_h;
	*width = (int)ceil(panel_w);
	*height = (int)ceil(panel_h);

	struct overlay_buffer *buffer = NULL;
	int buf_w = (int)ceil(*width * scale), buf_h = (int)ceil(*height * scale);
	if (lines && buf_w > 0 && buf_h > 0 && (buffer = calloc(1, sizeof(*buffer)))) {
		buffer->stride = (size_t)buf_w * 4;
		buffer->pixels = calloc((size_t)buf_h, buffer->stride);
		if (!buffer->pixels) {
			free(buffer);
			buffer = NULL;
		}
	}
	if (!buffer) goto out;
	wlr_buffer_init(&buffer->base, &buffer_impl, buf_w, buf_h);

	cairo_surface_t *surface = cairo_image_surface_create_for_data(
		(unsigned char *)buffer->pixels, CAIRO_FORMAT_ARGB32, buf_w, buf_h, (int)buffer->stride);
	cairo_t *cr = cairo_create(surface);
	cairo_scale(cr, scale, scale);
	pango_cairo_update_layout(cr, layout);

	const float *bg = config->window_titlebar_focus_color;
	const float *fg = config->window_titlebar_text_color;
	const float *accent = config->window_border_focus_color;
	double radius = config->window_corner_radius > 4 ? config->window_corner_radius + 4 : 8;

	rounded_rect(cr, 0.5, 0.5, *width - 1.0, *height - 1.0, radius);
	cairo_set_source_rgba(cr, bg[0] * 0.92, bg[1] * 0.92, bg[2] * 0.92, 0.96);
	cairo_fill_preserve(cr);
	cairo_set_line_width(cr, 1.0);
	cairo_set_source_rgba(cr, accent[0], accent[1], accent[2], 0.55);
	cairo_stroke(cr);

	cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 1.0);
	draw_text(cr, layout, FONT_TITLE, "Keyboard shortcuts", PAD, PAD, 0);
	cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.5);
	draw_text(cr, layout, FONT, "Esc to close", *width - PAD - hint_w,
		PAD + (title_h - row_h) / 2 + 2.0, 0);

	const double top = PAD + title_h + TITLE_GAP;
	for (size_t i = 0; i < line_count; i++) {
		const struct line *line = &lines[i];
		double x = PAD + line->x, ly = top + line->y;
		if (line->kind == LINE_GROUP) {
			cairo_set_source_rgba(cr, accent[0], accent[1], accent[2], 1.0);
			draw_text(cr, layout, FONT_BOLD, line->item->group[0] ? line->item->group : "Other",
				x, ly + 2.0, column_w);
			continue;
		}
		double w;
		text_size(layout, FONT_BOLD, line->item->keys, &w, NULL);
		double cap_w = w + 2 * KEYCAP_PAD;
		if (cap_w > key_w) cap_w = key_w;
		rounded_rect(cr, x, ly, cap_w, row_h, 5.0);
		cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.10);
		cairo_fill(cr);
		cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 1.0);
		draw_text(cr, layout, FONT_BOLD, line->item->keys, x + KEYCAP_PAD, ly + 2.0,
			cap_w - 2 * KEYCAP_PAD);
		cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.82);
		draw_text(cr, layout, FONT, line->item->description, x + key_w + KEY_GAP, ly + 2.0,
			description_w);
	}
	if (hidden) {
		char more[96];
		snprintf(more, sizeof(more), "and %zu more (shadyctl shortcuts lists them all)", hidden);
		cairo_set_source_rgba(cr, fg[0], fg[1], fg[2], 0.5);
		draw_text(cr, layout, FONT, more, PAD, top + body_h, panel_w - 2 * PAD);
	}

	cairo_destroy(cr);
	cairo_surface_flush(surface);
	cairo_surface_destroy(surface);

out:
	free(lines);
	g_object_unref(layout);
	cairo_destroy(measure);
	cairo_surface_destroy(measure_surface);
	return buffer;
}

/* ---- showing and hiding --------------------------------------------- */

static bool reject_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
	(void)buffer; (void)sx; (void)sy;
	return false;
}

bool shady_shortcut_overlay_visible(void) {
	return overlay.buffer != NULL;
}

static void release(void) {
	if (overlay.texture) wlr_texture_destroy(overlay.texture);
	overlay.texture = NULL;
	if (overlay.scene_buffer) wlr_scene_buffer_set_buffer(overlay.scene_buffer, NULL);
	if (overlay.tree) wlr_scene_node_set_enabled(&overlay.tree->node, false);
	if (overlay.buffer) wlr_buffer_drop(&overlay.buffer->base);
	overlay.buffer = NULL;
	overlay.output = NULL;
}

void shady_shortcut_overlay_hide(struct shady_server *server) {
	if (!overlay.buffer) return;
	release();
	shady_render_schedule_all_outputs(server);
}

static struct wlr_output *output_under_cursor(struct shady_server *server) {
	struct wlr_output *output = wlr_output_layout_output_at(server->output_layout,
		server->cursor->x, server->cursor->y);
	if (output) return output;
	struct shady_output *first;
	wl_list_for_each(first, &server->outputs, link) return first->wlr_output;
	return NULL;
}

static void show(struct shady_server *server) {
	struct wlr_output *output = output_under_cursor(server);
	struct wlr_box area;
	if (!output) return;
	wlr_output_layout_get_box(server->output_layout, output, &area);
	if (wlr_box_empty(&area)) return;

	struct list list = {0};
	shady_shortcuts_for_each(server, collect, &list);
	if (!list.count) {
		list_free(&list);
		return;
	}
	group_items(&list);
	int width = 0, height = 0;
	double scale = output->scale > 0 ? output->scale : 1.0;
	struct overlay_buffer *buffer = render_panel(server, &list, area.width, area.height,
		scale, &width, &height);
	list_free(&list);
	if (!buffer) {
		wlr_log(WLR_ERROR, "shortcuts: cannot draw the overlay");
		return;
	}

	release();
	overlay.buffer = buffer;
	overlay.output = output;
	overlay.box = (struct wlr_box){
		.x = area.x + (area.width - width) / 2,
		.y = area.y + (area.height - height) / 2,
		.width = width,
		.height = height,
	};

	/* For the scene graph (the 2D renderer); the GL one draws the texture. */
	if (!overlay.tree) {
		overlay.tree = wlr_scene_tree_create(&server->scene->tree);
		overlay.scene_buffer = overlay.tree
			? wlr_scene_buffer_create(overlay.tree, NULL) : NULL;
		if (overlay.scene_buffer) overlay.scene_buffer->point_accepts_input = reject_input;
	}
	if (overlay.scene_buffer) {
		wlr_scene_buffer_set_buffer(overlay.scene_buffer, &buffer->base);
		wlr_scene_buffer_set_dest_size(overlay.scene_buffer, width, height);
		wlr_scene_node_set_position(&overlay.tree->node, overlay.box.x, overlay.box.y);
		wlr_scene_node_raise_to_top(&overlay.tree->node);
		wlr_scene_node_set_enabled(&overlay.tree->node, true);
	}
	shady_render_schedule_all_outputs(server);
}

void shady_shortcuts_toggle(struct shady_server *server) {
	if (overlay.buffer) {
		shady_shortcut_overlay_hide(server);
		return;
	}
	if (shady_ipc_shortcuts_toggle(server)) return;
	struct shady_desktop_state *desktop = shady_desktop_state(server);
	if (desktop && desktop->session_locked) return;
	show(server);
}

bool shady_shortcut_overlay_for_output(struct shady_server *server,
		struct wlr_output *output, struct wlr_texture **texture, struct wlr_box *box) {
	if (!overlay.buffer || overlay.output != output) return false;
	if (!overlay.texture)
		overlay.texture = wlr_texture_from_buffer(server->renderer, &overlay.buffer->base);
	if (!overlay.texture) return false;
	*texture = overlay.texture;
	*box = overlay.box;
	return true;
}

void shady_shortcut_overlay_output_removed(struct shady_server *server,
		struct wlr_output *output) {
	if (overlay.output == output) shady_shortcut_overlay_hide(server);
}

void shady_shortcut_overlay_finish(void) {
	release();
	/* The scene and its nodes go with the compositor. */
	overlay.tree = NULL;
	overlay.scene_buffer = NULL;
}
