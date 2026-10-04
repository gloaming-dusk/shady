#define _POSIX_C_SOURCE 200809L

#include "ui.h"

#include <lauxlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "theme.h"

#define UI_MAX_DEPTH 48
#define UI_MAX_NODES 4096
#define IMAGE_CACHE 32

/* ---- colours ---------------------------------------------------------- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ui_parse_hex(const char *text, double rgba[4]) {
    if (!text || text[0] != '#') return false;
    size_t len = strlen(text + 1);
    int v[8];
    if (len != 3 && len != 6 && len != 8) return false;
    for (size_t i = 0; i < len; i++)
        if ((v[i] = hex_digit(text[1 + i])) < 0) return false;
    if (len == 3) {
        for (int i = 0; i < 3; i++) rgba[i] = v[i] * 17 / 255.0;
        rgba[3] = 1.0;
        return true;
    }
    for (int i = 0; i < 3; i++) rgba[i] = (v[2 * i] * 16 + v[2 * i + 1]) / 255.0;
    rgba[3] = len == 8 ? (v[6] * 16 + v[7]) / 255.0 : 1.0;
    return true;
}

static int channel(double c) {
    if (c < 0) c = 0;
    if (c > 1) c = 1;
    return (int)lround(c * 255.0);
}

void ui_format_hex(const double rgba[4], char out[10]) {
    if (rgba[3] >= 1.0)
        snprintf(out, 10, "#%02x%02x%02x", channel(rgba[0]), channel(rgba[1]), channel(rgba[2]));
    else
        snprintf(out, 10, "#%02x%02x%02x%02x", channel(rgba[0]), channel(rgba[1]),
            channel(rgba[2]), channel(rgba[3]));
}

/* ---- building --------------------------------------------------------- */

struct build {
    lua_State *L;
    char *error;
    size_t error_size;
    size_t nodes;
    bool failed;
};

static void build_error(struct build *b, const char *fmt, const char *detail) {
    if (b->failed) return;
    b->failed = true;
    snprintf(b->error, b->error_size, fmt, detail ? detail : "");
}

static double get_number(lua_State *L, int t, const char *key, double fallback) {
    lua_getfield(L, t, key);
    double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
    lua_pop(L, 1);
    return v;
}

static bool get_bool(lua_State *L, int t, const char *key, bool fallback) {
    lua_getfield(L, t, key);
    bool v = lua_isnil(L, -1) ? fallback : lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

/* A copy of the string field, or NULL. Numbers are converted. */
static char *get_string(lua_State *L, int t, const char *key) {
    lua_getfield(L, t, key);
    char *v = NULL;
    if (lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER)
        v = strdup(lua_tostring(L, -1));
    lua_pop(L, 1);
    return v;
}

/* The colour at the top of the stack: "#hex" or {r, g, b[, a]} in 0..1. */
static bool read_color(lua_State *L, double rgba[4]) {
    if (lua_type(L, -1) == LUA_TSTRING) return ui_parse_hex(lua_tostring(L, -1), rgba);
    if (!lua_istable(L, -1)) return false;
    for (int i = 0; i < 4; i++) {
        lua_rawgeti(L, -1, i + 1);
        rgba[i] = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : (i == 3 ? 1.0 : 0.0);
        lua_pop(L, 1);
    }
    return true;
}

static bool get_color(struct build *b, int t, const char *key, double rgba[4]) {
    lua_State *L = b->L;
    lua_getfield(L, t, key);
    bool present = !lua_isnil(L, -1) && lua_toboolean(L, -1);
    bool ok = present && read_color(L, rgba);
    if (present && !ok) build_error(b, "bad colour for '%s'", key);
    lua_pop(L, 1);
    return ok;
}

/* A solid colour or {gradient = "vertical"|"horizontal"|"diagonal", c1, c2, ...}. */
static bool get_paint(struct build *b, int t, const char *key, struct ui_paint *paint) {
    lua_State *L = b->L;
    lua_getfield(L, t, key);
    bool present = !lua_isnil(L, -1) && lua_toboolean(L, -1);
    bool ok = false;
    if (present && lua_istable(L, -1)) {
        lua_getfield(L, -1, "gradient");
        const char *dir = lua_tostring(L, -1);
        lua_pop(L, 1);
        if (dir) {
            paint->dir = dir[0] == 'h' ? 'h' : dir[0] == 'd' ? 'd' : 'v';
            paint->stops = 0;
            int n = (int)lua_rawlen(L, -1);
            ok = n >= 2 && n <= UI_MAX_STOPS;
            for (int i = 1; ok && i <= n; i++) {
                lua_rawgeti(L, -1, i);
                ok = read_color(L, paint->rgba[paint->stops++]);
                lua_pop(L, 1);
            }
        } else {
            ok = read_color(L, paint->rgba[0]);
            paint->stops = 1;
        }
    } else if (present) {
        ok = read_color(L, paint->rgba[0]);
        paint->stops = 1;
    }
    if (present && !ok) build_error(b, "bad colour or gradient for '%s'", key);
    lua_pop(L, 1);
    return ok;
}

static void read_style(struct build *b, int t, struct ui_style *style) {
    style->has_background = get_paint(b, t, "background", &style->background);
    style->has_border = get_color(b, t, "border", style->border);
    style->has_color = get_color(b, t, "color", style->color);
    lua_getfield(b->L, t, "opacity");
    style->has_opacity = lua_isnumber(b->L, -1);
    style->opacity = style->has_opacity ? lua_tonumber(b->L, -1) : 1.0;
    lua_pop(b->L, 1);
}

static void read_padding(lua_State *L, int t, double out[4]) {
    lua_getfield(L, t, "padding");
    if (lua_isnumber(L, -1)) {
        double p = lua_tonumber(L, -1);
        for (int i = 0; i < 4; i++) out[i] = p;
    } else if (lua_istable(L, -1)) {
        double v[4] = {0};
        int n = (int)lua_rawlen(L, -1);
        for (int i = 0; i < n && i < 4; i++) {
            lua_rawgeti(L, -1, i + 1);
            v[i] = lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
        if (n == 2) { out[0] = out[2] = v[0]; out[1] = out[3] = v[1]; }
        else if (n == 4) memcpy(out, v, sizeof(v));
        else if (n == 1) for (int i = 0; i < 4; i++) out[i] = v[0];
    }
    lua_pop(L, 1);
}

static enum ui_align parse_align(const char *s, enum ui_align fallback) {
    if (!s) return fallback;
    if (!strcmp(s, "start")) return UI_START;
    if (!strcmp(s, "center")) return UI_CENTER;
    if (!strcmp(s, "end")) return UI_END;
    if (!strcmp(s, "stretch")) return UI_STRETCH;
    if (!strcmp(s, "space-between")) return UI_SPACE_BETWEEN;
    return fallback;
}

static const char *field_string(lua_State *L, int t, const char *key, char *buf, size_t size) {
    lua_getfield(L, t, key);
    const char *s = lua_tostring(L, -1);
    if (s) snprintf(buf, size, "%s", s);
    lua_pop(L, 1);
    return s ? buf : NULL;
}

static struct {
    char *path;
    cairo_surface_t *surface;
} image_cache[IMAGE_CACHE];

static cairo_surface_t *load_image(const char *path) {
    for (int i = 0; i < IMAGE_CACHE; i++)
        if (image_cache[i].path && !strcmp(image_cache[i].path, path))
            return cairo_surface_reference(image_cache[i].surface);
    cairo_surface_t *surface = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    static int next;
    int slot = next++ % IMAGE_CACHE;
    free(image_cache[slot].path);
    if (image_cache[slot].surface) cairo_surface_destroy(image_cache[slot].surface);
    image_cache[slot].path = strdup(path);
    image_cache[slot].surface = cairo_surface_reference(surface);
    return surface;
}

bool ui_read_uniforms(lua_State *L, int index, struct shell_uniform *out, size_t max,
        size_t *count, char *error, size_t error_size) {
    *count = 0;
    if (lua_isnil(L, index)) return true;
    if (!lua_istable(L, index)) {
        snprintf(error, error_size, "uniforms must be a table");
        return false;
    }
    index = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, index)) {
        const char *key = lua_type(L, -2) == LUA_TSTRING ? lua_tostring(L, -2) : NULL;
        if (!key || strlen(key) + 3 > SHELL_UNIFORM_NAME_MAX || *count == max) {
            snprintf(error, error_size, key ? "too many uniforms or name too long: %s"
                : "uniform names must be strings%s", key ? key : "");
            lua_pop(L, 2);
            return false;
        }
        struct shell_uniform *u = &out[*count];
        snprintf(u->name, sizeof(u->name), "u_%s", key);
        double rgba[4];
        if (lua_type(L, -1) == LUA_TNUMBER) {
            u->size = 1;
            u->value[0] = (float)lua_tonumber(L, -1);
        } else if (lua_type(L, -1) == LUA_TSTRING && ui_parse_hex(lua_tostring(L, -1), rgba)) {
            u->size = 4;
            for (int i = 0; i < 4; i++) u->value[i] = (float)rgba[i];
        } else if (lua_istable(L, -1) && lua_rawlen(L, -1) >= 1 && lua_rawlen(L, -1) <= 4) {
            u->size = (int)lua_rawlen(L, -1);
            for (int i = 0; i < u->size; i++) {
                lua_rawgeti(L, -1, i + 1);
                u->value[i] = (float)lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        } else {
            snprintf(error, error_size, "uniform %s must be a number, a colour or 1-4 numbers", key);
            lua_pop(L, 2);
            return false;
        }
        (*count)++;
        lua_pop(L, 1);
    }
    return true;
}

static void add_child(struct build *b, struct ui_node *parent, struct ui_node *child, size_t *cap) {
    if (parent->child_count == *cap) {
        size_t next = *cap ? *cap * 2 : 4;
        struct ui_node **grown = realloc(parent->children, next * sizeof(*grown));
        if (!grown) {
            build_error(b, "out of memory%s", NULL);
            ui_free(b->L, child);
            return;
        }
        parent->children = grown;
        *cap = next;
    }
    parent->children[parent->child_count++] = child;
}

static struct ui_node *build_node(struct build *b, int t, int depth);

/* Array entries are children; `false`/`nil` holes are skipped and a plain
 * array (a table without `type`) is spliced in, so views can mix literal
 * widgets with lists built in loops. */
static void build_children(struct build *b, struct ui_node *parent, int t, int depth,
        size_t *cap) {
    lua_State *L = b->L;
    lua_Integer n = (lua_Integer)lua_rawlen(L, t);
    for (lua_Integer i = 1; i <= n && !b->failed; i++) {
        lua_rawgeti(L, t, i);
        int child = lua_gettop(L);
        if (lua_istable(L, child)) {
            lua_getfield(L, child, "type");
            bool is_widget = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (is_widget) {
                struct ui_node *node = build_node(b, child, depth + 1);
                if (node) add_child(b, parent, node, cap);
            } else if (depth + 1 < UI_MAX_DEPTH) {
                build_children(b, parent, child, depth + 1, cap);
            }
        } else if (!lua_isnil(L, child) && lua_toboolean(L, child)) {
            build_error(b, "children must be widgets, got %s", luaL_typename(L, child));
        }
        lua_pop(L, 1);
    }
}

static struct ui_node *build_node(struct build *b, int t, int depth) {
    lua_State *L = b->L;
    if (depth >= UI_MAX_DEPTH) {
        build_error(b, "widget tree is too deep%s", NULL);
        return NULL;
    }
    if (++b->nodes > UI_MAX_NODES) {
        build_error(b, "widget tree is too large%s", NULL);
        return NULL;
    }
    luaL_checkstack(L, 8, NULL);
    t = lua_absindex(L, t);
    char kind[16] = "box";
    field_string(L, t, "type", kind, sizeof(kind));

    struct ui_node *node = calloc(1, sizeof(*node));
    if (!node) {
        build_error(b, "out of memory%s", NULL);
        return NULL;
    }
    node->on_click = LUA_NOREF;
    if (!strcmp(kind, "box")) node->kind = UI_NODE_BOX;
    else if (!strcmp(kind, "text")) node->kind = UI_NODE_TEXT;
    else if (!strcmp(kind, "pip")) node->kind = UI_NODE_PIP;
    else if (!strcmp(kind, "image")) node->kind = UI_NODE_IMAGE;
    else {
        build_error(b, "unknown widget type '%s'", kind);
        free(node);
        return NULL;
    }

    node->width = get_number(L, t, "width", -1);
    node->height = get_number(L, t, "height", -1);
    node->min_width = get_number(L, t, "min_width", 0);
    node->max_width = get_number(L, t, "max_width", 0);
    node->min_height = get_number(L, t, "min_height", 0);
    node->max_height = get_number(L, t, "max_height", 0);
    node->grow = get_number(L, t, "grow", 0);
    node->shrink = get_number(L, t, "shrink", node->kind == UI_NODE_TEXT ? 1 : 0);
    node->gap = get_number(L, t, "gap", 0);
    read_padding(L, t, node->padding);
    char buf[32];
    const char *dir = field_string(L, t, "direction", buf, sizeof(buf));
    node->direction = dir && !strcmp(dir, "column") ? UI_COLUMN : UI_ROW;
    node->align = parse_align(field_string(L, t, "align", buf, sizeof(buf)), UI_CENTER);
    node->justify = parse_align(field_string(L, t, "justify", buf, sizeof(buf)), UI_START);
    node->clip = get_bool(L, t, "clip", false);
    node->radius = get_number(L, t, "radius", 0);
    node->border_width = get_number(L, t, "border_width", 1);
    read_style(b, t, &node->style);
    lua_getfield(L, t, "hover");
    if (lua_istable(L, -1)) {
        node->has_hover = true;
        read_style(b, lua_gettop(L), &node->hover);
    }
    lua_pop(L, 1);
    node->id = get_string(L, t, "id");
    lua_getfield(L, t, "on_click");
    if (lua_isfunction(L, -1)) node->on_click = luaL_ref(L, LUA_REGISTRYINDEX);
    else lua_pop(L, 1);
    lua_getfield(L, t, "shader");
    if (lua_istable(L, -1)) lua_getfield(L, -1, "path");
    else lua_pushvalue(L, -1);
    if (lua_type(L, -1) == LUA_TSTRING) node->shader = strdup(lua_tostring(L, -1));
    lua_pop(L, 2);
    if (node->shader) {
        struct shell_uniform uniforms[SHELL_EFFECT_UNIFORMS];
        size_t count = 0;
        char message[128];
        lua_getfield(L, t, "uniforms");
        if (!ui_read_uniforms(L, -1, uniforms, SHELL_EFFECT_UNIFORMS, &count, message,
                sizeof(message)))
            build_error(b, "%s", message);
        lua_pop(L, 1);
        if (count && (node->uniforms = malloc(count * sizeof(*uniforms)))) {
            memcpy(node->uniforms, uniforms, count * sizeof(*uniforms));
            node->uniform_count = count;
        }
    }

    switch (node->kind) {
    case UI_NODE_TEXT:
        node->text = get_string(L, t, "text");
        node->font = get_string(L, t, "font");
        node->letter_spacing = get_number(L, t, "letter_spacing", 0.25);
        node->ellipsize = get_bool(L, t, "ellipsize", true);
        node->text_align = get_number(L, t, "text_align", 0);
        break;
    case UI_NODE_PIP:
        node->pip_radius = get_number(L, t, "radius", 3);
        node->radius = 0;
        node->lit = get_bool(L, t, "lit", false);
        break;
    case UI_NODE_IMAGE: {
        char *path = get_string(L, t, "path");
        if (path) node->image = load_image(path);
        free(path);
        break;
    }
    case UI_NODE_BOX: {
        size_t cap = 0;
        build_children(b, node, t, depth, &cap);
        break;
    }
    }
    if (b->failed) {
        ui_free(L, node);
        return NULL;
    }
    return node;
}

struct ui_node *ui_build(lua_State *L, int index, char *error, size_t error_size) {
    struct build b = { .L = L, .error = error, .error_size = error_size };
    if (!lua_istable(L, index)) {
        snprintf(error, error_size, "view must return a widget, got %s", luaL_typename(L, index));
        return NULL;
    }
    return build_node(&b, index, 0);
}

void ui_free(lua_State *L, struct ui_node *node) {
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) ui_free(L, node->children[i]);
    free(node->children);
    if (node->on_click != LUA_NOREF && L) luaL_unref(L, LUA_REGISTRYINDEX, node->on_click);
    if (node->layout) g_object_unref(node->layout);
    if (node->image) cairo_surface_destroy(node->image);
    free(node->text);
    free(node->font);
    free(node->shader);
    free(node->uniforms);
    free(node->id);
    free(node);
}

/* ---- measuring and layout --------------------------------------------- */

static double clamp_size(double v, double lo, double hi) {
    if (hi > 0 && v > hi) v = hi;
    if (v < lo) v = lo;
    return v;
}

static double main_of(const struct ui_node *n, enum ui_direction d, bool natural) {
    return d == UI_ROW ? (natural ? n->nat_w : n->w) : (natural ? n->nat_h : n->h);
}

void ui_measure(cairo_t *cr, struct ui_node *node, const struct ui_defaults *defaults) {
    double w = 0, h = 0;
    switch (node->kind) {
    case UI_NODE_TEXT: {
        if (node->layout) g_object_unref(node->layout);
        node->layout = pango_cairo_create_layout(cr);
        PangoFontDescription *font = pango_font_description_from_string(
            node->font ? node->font : defaults->font);
        pango_layout_set_font_description(node->layout, font);
        pango_font_description_free(font);
        if (node->letter_spacing != 0) {
            PangoAttrList *attrs = pango_attr_list_new();
            pango_attr_list_insert(attrs,
                pango_attr_letter_spacing_new((int)(node->letter_spacing * PANGO_SCALE)));
            pango_layout_set_attributes(node->layout, attrs);
            pango_attr_list_unref(attrs);
        }
        pango_layout_set_text(node->layout, node->text ? node->text : "", -1);
        if (node->max_width > 0 && node->ellipsize) {
            pango_layout_set_ellipsize(node->layout, PANGO_ELLIPSIZE_END);
            pango_layout_set_width(node->layout, (int)(node->max_width * PANGO_SCALE));
        }
        int pw = 0, ph = 0;
        pango_layout_get_pixel_size(node->layout, &pw, &ph);
        w = pw;
        h = ph;
        break;
    }
    case UI_NODE_PIP:
        w = h = node->pip_radius * 2;
        break;
    case UI_NODE_IMAGE:
        if (node->image) {
            w = cairo_image_surface_get_width(node->image);
            h = cairo_image_surface_get_height(node->image);
        }
        break;
    case UI_NODE_BOX: {
        double main = 0, cross = 0;
        size_t visible = 0;
        for (size_t i = 0; i < node->child_count; i++) {
            struct ui_node *c = node->children[i];
            ui_measure(cr, c, defaults);
            double cm = main_of(c, node->direction, true);
            double cc = node->direction == UI_ROW ? c->nat_h : c->nat_w;
            main += cm;
            if (cc > cross) cross = cc;
            visible++;
        }
        if (visible > 1) main += node->gap * (double)(visible - 1);
        if (node->direction == UI_ROW) {
            w = main + node->padding[1] + node->padding[3];
            h = cross + node->padding[0] + node->padding[2];
        } else {
            h = main + node->padding[0] + node->padding[2];
            w = cross + node->padding[1] + node->padding[3];
        }
        break;
    }
    }
    if (node->width >= 0) w = node->width;
    if (node->height >= 0) h = node->height;
    node->nat_w = clamp_size(w, node->min_width, node->max_width);
    node->nat_h = clamp_size(h, node->min_height, node->max_height);
}

static void set_rect(cairo_t *cr, struct ui_node *node, double x, double y, double w, double h);

static void layout_box(cairo_t *cr, struct ui_node *node) {
    bool row = node->direction == UI_ROW;
    double ix = node->x + node->padding[3];
    double iy = node->y + node->padding[0];
    double iw = node->w - node->padding[1] - node->padding[3];
    double ih = node->h - node->padding[0] - node->padding[2];
    double inner_main = row ? iw : ih;
    double inner_cross = row ? ih : iw;
    size_t n = node->child_count;
    if (n == 0) return;

    double sizes[n];
    double total = node->gap * (double)(n - 1), grow = 0, shrink = 0;
    for (size_t i = 0; i < n; i++) {
        struct ui_node *c = node->children[i];
        sizes[i] = main_of(c, node->direction, true);
        total += sizes[i];
        grow += c->grow;
        shrink += c->shrink * sizes[i];
    }
    double free_space = inner_main - total;
    if (free_space > 0 && grow > 0) {
        for (size_t i = 0; i < n; i++)
            sizes[i] += free_space * node->children[i]->grow / grow;
        free_space = 0;
    } else if (free_space < 0 && shrink > 0) {
        double deficit = -free_space;
        for (size_t i = 0; i < n; i++) {
            struct ui_node *c = node->children[i];
            double floor_size = row ? c->min_width : c->min_height;
            double cut = deficit * c->shrink * sizes[i] / shrink;
            double next = sizes[i] - cut;
            if (next < floor_size) next = floor_size;
            free_space += sizes[i] - next;
            sizes[i] = next;
        }
    }

    double pos = row ? ix : iy;
    double spacing = node->gap;
    if (free_space > 0) {
        if (node->justify == UI_CENTER) pos += free_space / 2;
        else if (node->justify == UI_END) pos += free_space;
        else if (node->justify == UI_SPACE_BETWEEN && n > 1)
            spacing += free_space / (double)(n - 1);
    }
    double limit = (row ? ix : iy) + inner_main + 0.5;
    for (size_t i = 0; i < n; i++) {
        struct ui_node *c = node->children[i];
        if (node->clip && pos + sizes[i] > limit) {
            /* This child and every later one is out of room. */
            for (size_t j = i; j < n; j++) node->children[j]->hidden = true;
            break;
        }
        double cross = row ? c->nat_h : c->nat_w;
        if (cross > inner_cross) cross = inner_cross; /* never wider than the box */
        double offset = 0;
        if (node->align == UI_STRETCH) cross = inner_cross;
        else if (node->align == UI_CENTER) offset = (inner_cross - cross) / 2;
        else if (node->align == UI_END) offset = inner_cross - cross;
        if (row) set_rect(cr, c, pos, iy + offset, sizes[i], cross);
        else set_rect(cr, c, ix + offset, pos, cross, sizes[i]);
        pos += sizes[i] + spacing;
    }
}

static void set_rect(cairo_t *cr, struct ui_node *node, double x, double y, double w, double h) {
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;
    node->hidden = false;
    if (node->kind == UI_NODE_TEXT && node->layout && node->ellipsize && w + 0.5 < node->nat_w) {
        pango_layout_set_ellipsize(node->layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(node->layout, (int)(w * PANGO_SCALE));
    }
    if (node->kind == UI_NODE_BOX) layout_box(cr, node);
}

void ui_layout(cairo_t *cr, struct ui_node *node, double x, double y, double w, double h) {
    set_rect(cr, node, x, y, w, h);
}

/* ---- drawing ---------------------------------------------------------- */

static void set_paint(cairo_t *cr, const struct ui_paint *paint, double x, double y,
        double w, double h) {
    if (paint->stops == 1) {
        cairo_set_source_rgba(cr, paint->rgba[0][0], paint->rgba[0][1], paint->rgba[0][2],
            paint->rgba[0][3]);
        return;
    }
    cairo_pattern_t *pattern = paint->dir == 'h' ? cairo_pattern_create_linear(x, 0, x + w, 0)
        : paint->dir == 'd' ? cairo_pattern_create_linear(x, y, x + w, y + h)
        : cairo_pattern_create_linear(0, y, 0, y + h);
    for (int i = 0; i < paint->stops; i++) {
        const double *c = paint->rgba[i];
        cairo_pattern_add_color_stop_rgba(pattern, (double)i / (paint->stops - 1),
            c[0], c[1], c[2], c[3]);
    }
    cairo_set_source(cr, pattern);
    cairo_pattern_destroy(pattern);
}

static void draw_pip_node(cairo_t *cr, const struct ui_node *node, const double color[4],
        const double ring[4]) {
    double x = node->x + node->w / 2, y = node->y + node->h / 2, r = node->pip_radius;
    cairo_new_path(cr); /* text drawing leaves a current point behind */
    if (node->lit) {
        cairo_pattern_t *halo = cairo_pattern_create_radial(x, y, 0, x, y, r * 3.2);
        cairo_pattern_add_color_stop_rgba(halo, 0.0, color[0], color[1], color[2], 0.45 * color[3]);
        cairo_pattern_add_color_stop_rgba(halo, 1.0, color[0], color[1], color[2], 0.0);
        cairo_set_source(cr, halo);
        cairo_arc(cr, x, y, r * 3.2, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(halo);
        cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
        cairo_arc(cr, x, y, r, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, ring[0], ring[1], ring[2], 0.28 * ring[3]);
        cairo_arc(cr, x, y, r - 0.5, 0, 2 * G_PI);
        cairo_stroke(cr);
    }
}

void ui_draw(cairo_t *cr, const struct ui_node *node, const struct ui_node *hovered,
        const struct ui_defaults *defaults) {
    if (node->hidden) return;
    struct ui_style s = node->style;
    if (node == hovered && node->has_hover) {
        const struct ui_style *h = &node->hover;
        if (h->has_background) { s.background = h->background; s.has_background = true; }
        if (h->has_border) { memcpy(s.border, h->border, sizeof(s.border)); s.has_border = true; }
        if (h->has_color) { memcpy(s.color, h->color, sizeof(s.color)); s.has_color = true; }
        if (h->has_opacity) { s.opacity = h->opacity; s.has_opacity = true; }
    }
    bool group = s.has_opacity && s.opacity < 1.0;
    if (group) cairo_push_group(cr);

    if (s.has_background && s.background.stops > 0) {
        rounded_rect(cr, node->x, node->y, node->w, node->h, node->radius);
        set_paint(cr, &s.background, node->x, node->y, node->w, node->h);
        cairo_fill(cr);
    }
    if (s.has_border && node->border_width > 0) {
        double bw = node->border_width;
        cairo_set_line_width(cr, bw);
        rounded_rect(cr, node->x + bw / 2, node->y + bw / 2, node->w - bw, node->h - bw,
            node->radius);
        cairo_set_source_rgba(cr, s.border[0], s.border[1], s.border[2], s.border[3]);
        cairo_stroke(cr);
    }

    const double *color = s.has_color ? s.color : defaults->text;
    switch (node->kind) {
    case UI_NODE_TEXT:
        if (node->layout && node->text && *node->text) {
            int lw = 0, lh = 0;
            pango_layout_get_pixel_size(node->layout, &lw, &lh);
            double tx = node->x + (node->w - lw) * node->text_align;
            double ty = node->y + (node->h - lh) / 2;
            cairo_set_source_rgba(cr, color[0], color[1], color[2], color[3]);
            cairo_move_to(cr, tx, ty);
            pango_cairo_show_layout(cr, node->layout);
        }
        break;
    case UI_NODE_PIP:
        draw_pip_node(cr, node, s.has_color ? s.color : defaults->accent, defaults->text);
        break;
    case UI_NODE_IMAGE:
        if (node->image) {
            double iw = cairo_image_surface_get_width(node->image);
            double ih = cairo_image_surface_get_height(node->image);
            if (iw > 0 && ih > 0) {
                cairo_save(cr);
                cairo_translate(cr, node->x, node->y);
                cairo_scale(cr, node->w / iw, node->h / ih);
                cairo_set_source_surface(cr, node->image, 0, 0);
                cairo_paint(cr);
                cairo_restore(cr);
            }
        }
        break;
    case UI_NODE_BOX:
        for (size_t i = 0; i < node->child_count; i++)
            ui_draw(cr, node->children[i], hovered, defaults);
        break;
    }

    if (group) {
        cairo_pop_group_to_source(cr);
        cairo_paint_with_alpha(cr, s.opacity);
    }
}

static bool interactive(const struct ui_node *node, bool clickable) {
    if (clickable) return node->on_click != LUA_NOREF;
    return node->on_click != LUA_NOREF || node->id || node->has_hover;
}

struct ui_node *ui_hit(struct ui_node *node, double x, double y, bool clickable) {
    if (!node || node->hidden) return NULL;
    if (x < node->x || y < node->y || x >= node->x + node->w || y >= node->y + node->h)
        return NULL;
    for (size_t i = node->child_count; i > 0; i--) {
        struct ui_node *hit = ui_hit(node->children[i - 1], x, y, clickable);
        if (hit) return hit;
    }
    return interactive(node, clickable) ? node : NULL;
}
