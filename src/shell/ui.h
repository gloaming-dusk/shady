#ifndef SHADY_SHELL_UI_H
#define SHADY_SHELL_UI_H

/*
 * Widget trees built from Lua tables: a small flexbox-style layout, cairo
 * drawing and hit-testing. A view function returns a fresh tree on every
 * repaint (see lua.c), so trees are immutable once built and simply freed
 * when the next one replaces them.
 *
 * Node types: box (row/column container), text, pip (focus dot), image.
 * The Lua-facing properties are documented in docs/SHELL_LUA_API.md.
 */

#include <cairo/cairo.h>
#include <lua.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <stddef.h>

#include "effects.h"

enum ui_kind { UI_NODE_BOX, UI_NODE_TEXT, UI_NODE_PIP, UI_NODE_IMAGE };
enum ui_direction { UI_ROW, UI_COLUMN };
enum ui_align { UI_START, UI_CENTER, UI_END, UI_STRETCH, UI_SPACE_BETWEEN };

#define UI_MAX_STOPS 4

struct ui_paint {
    int stops; /* 0 = none, 1 = solid, 2+ = gradient */
    double rgba[UI_MAX_STOPS][4];
    char dir; /* 'v'ertical, 'h'orizontal, 'd'iagonal */
};

/* Style fields a node sets; `hover` overrides the ones it sets. */
struct ui_style {
    struct ui_paint background;
    bool has_background;
    double border[4];
    bool has_border;
    double color[4];
    bool has_color;
    double opacity;
    bool has_opacity;
};

struct ui_node {
    enum ui_kind kind;
    struct ui_node **children;
    size_t child_count;

    /* Layout properties; negative sizes mean "natural". */
    double width, height;
    double min_width, max_width, min_height, max_height;
    double grow, shrink;
    double padding[4]; /* top, right, bottom, left */
    double gap;
    enum ui_direction direction;
    enum ui_align align, justify;
    bool clip; /* hide children that do not fit along the main axis */

    struct ui_style style, hover;
    bool has_hover;
    double radius;
    double border_width;

    char *text;
    char *font;
    double letter_spacing;
    bool ellipsize;
    double text_align; /* 0 left, 0.5 centre, 1 right */
    PangoLayout *layout;

    double pip_radius;
    bool lit;

    cairo_surface_t *image;

    int on_click; /* Lua registry ref or LUA_NOREF */
    char *id;

    /* Widget shader (absolute path) and its uniforms. */
    char *shader;
    struct shell_uniform *uniforms;
    size_t uniform_count;

    /* Computed by ui_measure/ui_layout. */
    double nat_w, nat_h;
    double x, y, w, h;
    bool hidden;
};

/* Context the tree is drawn in: theme defaults for colours not set. */
struct ui_defaults {
    double text[4];
    double accent[4];
    const char *font;
};

/* Build a tree from the table at `index`. Returns NULL and pushes nothing on
 * error, writing a message to `error`. */
struct ui_node *ui_build(lua_State *L, int index, char *error, size_t error_size);
void ui_free(lua_State *L, struct ui_node *node);

/* Natural size of the tree, then positions within (x, y, w, h). */
void ui_measure(cairo_t *cr, struct ui_node *node, const struct ui_defaults *defaults);
void ui_layout(cairo_t *cr, struct ui_node *node, double x, double y, double w, double h);
/* `hovered` may be NULL. */
void ui_draw(cairo_t *cr, const struct ui_node *node, const struct ui_node *hovered,
    const struct ui_defaults *defaults);

/* Deepest visible node under (x, y) that is clickable or has an id or a
 * hover style (or, with `clickable`, that has on_click); NULL if none. */
struct ui_node *ui_hit(struct ui_node *node, double x, double y, bool clickable);

/* Read a uniforms table ({name = number | "#colour" | {n1..n4}}) at `index`.
 * Returns false with a message in `error`. */
bool ui_read_uniforms(lua_State *L, int index, struct shell_uniform *out, size_t max,
    size_t *count, char *error, size_t error_size);

/* Colour parsing shared with the Lua API: "#RGB", "#RRGGBB", "#RRGGBBAA". */
bool ui_parse_hex(const char *text, double rgba[4]);
void ui_format_hex(const double rgba[4], char out[10]);

#endif
