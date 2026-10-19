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

enum ui_kind { UI_NODE_BOX, UI_NODE_TEXT, UI_NODE_PIP, UI_NODE_IMAGE, UI_NODE_CUSTOM, UI_NODE_ICON };

/* A key/value table copied from Lua: plugin options and the properties of
 * plugin-drawn widgets (shady/shell_plugin.h calls it shady_shell_props). */
struct ui_prop {
    char *key;
    char *string; /* numbers and booleans are kept as text too */
    double number;
    bool is_number;
};

struct shady_shell_props_handle {
    struct ui_prop *items;
    size_t count;
};
/* A stack lays every child over the same area, placed by its own
 * left/top/right/bottom or else by the stack's align and justify. */
enum ui_direction { UI_ROW, UI_COLUMN, UI_STACK };
enum ui_align { UI_START, UI_CENTER, UI_END, UI_STRETCH, UI_SPACE_BETWEEN };

#define UI_MAX_STOPS 4

struct ui_paint {
    int stops; /* 0 = none, 1 = solid, 2+ = gradient */
    double rgba[UI_MAX_STOPS][4];
    char dir; /* 'v'ertical, 'h'orizontal, 'd'iagonal */
};

/* Style fields a node sets; `hover`, `enter` and `exit` override the ones
 * they set. `scale`, `rotate` (degrees, both about the node's centre) and
 * `translate` move only the drawing: layout and hit-testing keep the
 * untransformed rectangle. `shadow` is a hard, offset copy of the node's
 * shape drawn behind it. */
struct ui_style {
    struct ui_paint background;
    bool has_background;
    double border[4];
    bool has_border;
    double color[4];
    bool has_color;
    double opacity;
    bool has_opacity;
    double scale;
    bool has_scale;
    double translate[2];
    bool has_translate;
    double rotate;
    bool has_rotate;
    double shadow[2]; /* offset */
    double shadow_color[4];
    bool has_shadow;
};

/* A damped spring; see anim.h. */
struct ui_spring {
    double stiffness, damping;
};

struct ui_anim;

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
    /* Placement inside a stack; NAN when unset. */
    double left, top, right, bottom;

    struct ui_style style, hover;
    bool has_hover;

    /* Transitions: `animate` springs the node's rectangle and style towards
     * each new tree's values; `enter` is where a new node starts from and
     * `exit` where a closing popup's nodes go. */
    bool animate;
    struct ui_spring spring;
    struct ui_style enter, exit;
    bool has_enter, has_exit;
    char *key; /* identity for transitions; defaults to `id`, then the tree path */
    struct ui_anim *anim; /* this paint's state, owned by the view's ui_anims */
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

    char *icon; /* UI_NODE_ICON: a built-in icon name */
    double stroke; /* icon line width */

    char *widget; /* plugin widget type (UI_NODE_CUSTOM) */
    struct shady_shell_props_handle props;

    int on_click; /* Lua registry refs or LUA_NOREF */
    int on_scroll;
    int on_drag;
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
    /* Draws a plugin widget in its own coordinates; may be NULL. */
    void (*draw_custom)(const char *type, cairo_t *cr, double width, double height,
        const struct shady_shell_props_handle *props);
};

/* Build a tree from the table at `index`. Returns NULL and pushes nothing on
 * error, writing a message to `error`. */
struct ui_node *ui_build(lua_State *L, int index, char *error, size_t error_size);
void ui_free(lua_State *L, struct ui_node *node);

/* Natural size of the tree, then positions within (x, y, w, h). */
void ui_measure(cairo_t *cr, struct ui_node *node, const struct ui_defaults *defaults);
void ui_layout(cairo_t *cr, struct ui_node *node, double x, double y, double w, double h);
/* The node's style with `hover` applied when it is the hovered node. */
void ui_resolve_style(const struct ui_node *node, const struct ui_node *hovered,
    struct ui_style *out);
/* Copy into `out` the fields `over` sets. */
void ui_merge_style(struct ui_style *out, const struct ui_style *over);
/* `hovered` may be NULL. Animated nodes draw from their ui_anim state. */
void ui_draw(cairo_t *cr, const struct ui_node *node, const struct ui_node *hovered,
    const struct ui_defaults *defaults);

/* Deepest visible node under (x, y) that is clickable or has an id or a
 * hover style (or, with `clickable`, that has on_click); NULL if none. */
struct ui_node *ui_hit(struct ui_node *node, double x, double y, bool clickable);
enum ui_handler { UI_ON_CLICK, UI_ON_SCROLL, UI_ON_DRAG };
/* Deepest visible node under (x, y) with that handler; NULL if none. */
struct ui_node *ui_hit_handler(struct ui_node *node, double x, double y, enum ui_handler which);
/* The names shell.icon{} knows, NULL-terminated. */
extern const char *const ui_icon_names[];

/* Read a uniforms table ({name = number | "#colour" | {n1..n4}}) at `index`.
 * Returns false with a message in `error`. */
bool ui_read_uniforms(lua_State *L, int index, struct shell_uniform *out, size_t max,
    size_t *count, char *error, size_t error_size);

/* Copy the string, number and boolean fields of the table at `index`. */
void ui_read_props(lua_State *L, int index, struct shady_shell_props_handle *out);
void ui_free_props(struct shady_shell_props_handle *props);
const struct ui_prop *ui_find_prop(const struct shady_shell_props_handle *props,
    const char *key);

/* Colour parsing shared with the Lua API: "#RGB", "#RRGGBB", "#RRGGBBAA". */
bool ui_parse_hex(const char *text, double rgba[4]);
void ui_format_hex(const double rgba[4], char out[10]);

#endif
