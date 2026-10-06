#define _GNU_SOURCE

#include "script.h"

#include <errno.h>
#include <fcntl.h>
#include <lauxlib.h>
#include <limits.h>
#include <lua.h>
#include <lualib.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "shady-shell-v1-client-protocol.h"
#include "compositor.h"
#include "plugins.h"
#include "render.h"
#include "shell.h"
#include "theme.h"
#include "ui.h"

#define MAX_DEFS 32
#define MAX_DEF_OUTPUTS 8
#define NAME_MAX_LEN 64
#define POLL_BUFFER (64 * 1024)
#define LISTEN_RESTART_MS 2000
#define RELOAD_DEBOUNCE_MS 150

/* A bar or popup declared by shell.bar{} / shell.popup{}. */
struct view_def {
    bool popup;
    char name[NAME_MAX_LEN];
    int view; /* registry ref */
    int on_key; /* registry ref or LUA_NOREF */
    enum zwlr_layer_shell_v1_layer layer;
    uint32_t anchor;
    int32_t margin[4]; /* top, right, bottom, left */
    enum zwlr_layer_surface_v1_keyboard_interactivity keyboard;
    uint32_t width, height; /* 0: natural size (popup) or stretched (bar) */
    int32_t exclusive;
    bool all_outputs;
    char outputs[MAX_DEF_OUTPUTS][NAME_MAX_LEN];
    size_t output_count;
    char *shader; /* whole-surface shader, absolute path */
    int uniforms; /* registry ref: table or function returning one */
    /* A popup kept open on one output: opened when an output appears and
     * moved to another when its output goes away. */
    bool persistent;
};

/* A live surface showing one view_def. */
struct view {
    struct wl_list link;
    struct shell_lua *lua;
    struct view_def *def;
    struct shell_output *output;
    struct shell_surface *surface;
    struct ui_node *tree;
    int args; /* popup argument table, registry ref or LUA_NOREF */
    struct ui_node *hovered;
    char hover_id[NAME_MAX_LEN];
    bool pointer_in;
    double px, py;
    char last_error[256];
};

/* shell.poll / shell.listen: a command whose output is a string value. */
struct poll {
    struct wl_list link;
    struct shell_lua *lua;
    char *command;
    bool listen;
    uint32_t interval_ms;
    char *value;
    pid_t pid;
    int fd;
    struct shell_watch *watch;
    struct shell_timer *timer;
    char buffer[POLL_BUFFER];
    size_t length;
};

/* One loaded generation of the config. A reload builds a new one and swaps
 * it in only if the config ran without errors. */
struct shell_lua {
    struct shell *shell;
    lua_State *L;
    struct view_def defs[MAX_DEFS];
    size_t def_count;
    struct wl_list views; /* view.link */
    struct wl_list polls; /* poll.link */
    int handlers; /* registry ref: event name -> list of functions */
    struct shell_timer *date_timer;
    int in_view; /* >0 while a view function runs */
};

/* Config file location and reload watching outlive generations. */
static struct {
    struct shell *shell;
    char config[PATH_MAX + 16];
    char data_dir[PATH_MAX];
    int inotify;
    struct shell_watch *watch;
    struct shell_timer *debounce;
} host = { .inotify = -1 };

static const char *REGISTRY_KEY = "shady.shell";

static void view_destroy(struct view *view);

/* ---- helpers ---------------------------------------------------------- */

static struct shell_lua *lua_of(lua_State *L) {
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    struct shell_lua *lua = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return lua;
}

static int traceback(lua_State *L) {
    const char *message = lua_tostring(L, 1);
    luaL_traceback(L, L, message ? message : "(error object is not a string)", 1);
    return 1;
}

/* Call the function below `nargs` arguments with a traceback; on error log
 * it (once per distinct message when `last` is given) and return false. */
static bool call(lua_State *L, int nargs, int nresults, const char *what, char *last,
        size_t last_size) {
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int rc = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (rc == LUA_OK) {
        if (last) last[0] = '\0';
        return true;
    }
    const char *message = lua_tostring(L, -1);
    if (!message) message = "unknown error";
    if (!last || strncmp(last, message, last_size - 1) != 0) {
        fprintf(stderr, "shady-shell: %s: %s\n", what, message);
        if (last) snprintf(last, last_size, "%s", message);
    }
    lua_pop(L, 1);
    return false;
}

static void redraw_all(struct shell_lua *lua) {
    struct view *view;
    wl_list_for_each(view, &lua->views, link) shell_surface_redraw(view->surface);
}

static struct shell_output *find_output(struct shell *shell, const char *name) {
    struct shell_output *output;
    wl_list_for_each(output, &shell->core.outputs, link)
        if (output->ready && name && !strcmp(output->name, name)) return output;
    return NULL;
}

static struct view_def *find_def(struct shell_lua *lua, const char *name, bool popup) {
    for (size_t i = 0; i < lua->def_count; i++)
        if (lua->defs[i].popup == popup && !strcmp(lua->defs[i].name, name)) return &lua->defs[i];
    return NULL;
}

static struct view *find_popup(struct shell_lua *lua, const char *name) {
    struct view *view;
    wl_list_for_each(view, &lua->views, link)
        if (view->def->popup && !strcmp(view->def->name, name)) return view;
    return NULL;
}

static void push_hex(lua_State *L, const double rgba[4]) {
    char hex[10];
    ui_format_hex(rgba, hex);
    lua_pushstring(L, hex);
}

static void ui_defaults(struct ui_defaults *d) {
    d->text[0] = UI_TEXT.r; d->text[1] = UI_TEXT.g; d->text[2] = UI_TEXT.b; d->text[3] = 1;
    d->accent[0] = UI_ACCENT.r; d->accent[1] = UI_ACCENT.g; d->accent[2] = UI_ACCENT.b;
    d->accent[3] = 1;
    d->font = "Sans Medium 9.5";
    d->draw_custom = shell_plugins_draw_widget;
}

/* ---- views ------------------------------------------------------------ */

/* Run the view function and build its tree; NULL on error. */
static struct ui_node *build_tree(struct view *view, double width, double height) {
    struct shell_lua *lua = view->lua;
    lua_State *L = lua->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, view->def->view);
    lua_createtable(L, 0, 6);
    lua_pushstring(L, view->def->name);
    lua_setfield(L, -2, "name");
    if (view->output) {
        lua_pushstring(L, view->output->name);
        lua_setfield(L, -2, "output");
    }
    lua_pushnumber(L, width);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, height);
    lua_setfield(L, -2, "height");
    lua_pushinteger(L, view->surface ? view->surface->scale : 1);
    lua_setfield(L, -2, "scale");
    if (view->hover_id[0]) {
        lua_pushstring(L, view->hover_id);
        lua_setfield(L, -2, "hovered");
    }
    if (view->args != LUA_NOREF) lua_rawgeti(L, LUA_REGISTRYINDEX, view->args);
    else lua_pushnil(L);

    char what[NAME_MAX_LEN + 16];
    snprintf(what, sizeof(what), "view '%s'", view->def->name);
    lua->in_view++;
    bool ok = call(L, 2, 1, what, view->last_error, sizeof(view->last_error));
    lua->in_view--;
    if (!ok) return NULL;
    char error[256];
    struct ui_node *tree = ui_build(L, -1, error, sizeof(error));
    lua_pop(L, 1);
    if (!tree && strcmp(view->last_error, error) != 0) {
        fprintf(stderr, "shady-shell: %s: %s\n", what, error);
        snprintf(view->last_error, sizeof(view->last_error), "%s", error);
    }
    return tree;
}

static void collect_widgets(struct shell_effects *effects, const struct ui_node *node) {
    if (node->hidden) return;
    if (node->shader) {
        struct shell_effect *effect = shell_effects_add(effects);
        if (effect && (effect->shader = strdup(node->shader))) {
            effect->x = node->x;
            effect->y = node->y;
            effect->width = node->w;
            effect->height = node->h;
            effect->radius = node->radius;
            effect->uniform_count = node->uniform_count;
            if (node->uniform_count)
                memcpy(effect->uniforms, node->uniforms,
                    node->uniform_count * sizeof(*node->uniforms));
        } else if (effect) {
            effects->count--;
        }
    }
    for (size_t i = 0; i < node->child_count; i++) collect_widgets(effects, node->children[i]);
}

/* Hand this paint's shaders to the renderer: the surface's own, then every
 * visible widget's, in drawing order. */
static void collect_effects(struct view *view, struct shell_surface *surface,
        const struct ui_node *tree, double width, double height) {
    struct shell_effects *effects = &surface->effects;
    shell_effects_clear(effects);
    const struct view_def *def = view->def;
    if (def->shader && (effects->surface.shader = strdup(def->shader))) {
        effects->surface.width = width;
        effects->surface.height = height;
        if (def->uniforms != LUA_NOREF) {
            lua_State *L = view->lua->L;
            lua_rawgeti(L, LUA_REGISTRYINDEX, def->uniforms);
            bool ok = true;
            if (lua_isfunction(L, -1))
                ok = call(L, 0, 1, "uniforms", view->last_error, sizeof(view->last_error));
            char error[128];
            if (ok && !ui_read_uniforms(L, -1, effects->surface.uniforms, SHELL_EFFECT_UNIFORMS,
                    &effects->surface.uniform_count, error, sizeof(error)))
                fprintf(stderr, "shady-shell: %s: %s\n", def->name, error);
            if (ok) lua_pop(L, 1);
        }
    }
    if (tree) collect_widgets(effects, tree);
    static bool warned;
    struct shell_renderer *renderer = surface->core->renderer;
    if (!warned && shell_effects_any(effects) && !renderer->impl->composite) {
        warned = true;
        fprintf(stderr, "shady-shell: shader effects need the gl renderer; drawing without them\n");
    }
}

static void view_draw(void *data, struct shell_surface *surface, cairo_t *cr,
        double width, double height) {
    struct view *view = data;
    lua_State *L = view->lua->L;
    ui_free(L, view->tree);
    view->tree = NULL;
    view->hovered = NULL;
    struct ui_defaults defaults;
    ui_defaults(&defaults);

    struct ui_node *tree = build_tree(view, width, height);
    if (!tree) return;
    ui_measure(cr, tree, &defaults);
    if (view->def->popup) {
        /* Popups without a fixed size follow their content. */
        uint32_t want_w = view->def->width ? view->def->width : (uint32_t)ceil(tree->nat_w);
        uint32_t want_h = view->def->height ? view->def->height : (uint32_t)ceil(tree->nat_h);
        if (want_w && want_h) shell_surface_set_size(surface, want_w, want_h);
    }
    ui_layout(cr, tree, 0, 0, width, height);
    view->tree = tree;
    if (view->pointer_in) view->hovered = ui_hit(tree, view->px, view->py, false);
    ui_draw(cr, tree, view->hovered, &defaults);
    collect_effects(view, surface, tree, width, height);
}

static void set_hover(struct view *view, struct ui_node *node) {
    if (node == view->hovered) return;
    view->hovered = node;
    snprintf(view->hover_id, sizeof(view->hover_id), "%s", node && node->id ? node->id : "");
    shell_surface_redraw(view->surface);
}

static void view_motion(void *data, struct shell_surface *surface, double x, double y) {
    (void)surface;
    struct view *view = data;
    view->pointer_in = true;
    view->px = x;
    view->py = y;
    if (view->tree) set_hover(view, ui_hit(view->tree, x, y, false));
}

static void view_leave(void *data, struct shell_surface *surface) {
    (void)surface;
    struct view *view = data;
    view->pointer_in = false;
    set_hover(view, NULL);
}

static const char *button_name(uint32_t button) {
    switch (button) {
    case 0x110: return "left"; /* BTN_LEFT */
    case 0x111: return "right";
    case 0x112: return "middle";
    default: return "other";
    }
}

static void view_button(void *data, struct shell_surface *surface, uint32_t button,
        bool pressed) {
    (void)surface;
    struct view *view = data;
    if (!pressed || !view->tree) return;
    struct ui_node *hit = ui_hit(view->tree, view->px, view->py, true);
    if (!hit) return;
    lua_State *L = view->lua->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, hit->on_click);
    lua_pushstring(L, button_name(button));
    lua_createtable(L, 0, 5);
    lua_pushnumber(L, hit->x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, hit->y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, hit->w);
    lua_setfield(L, -2, "width");
    lua_pushnumber(L, hit->h);
    lua_setfield(L, -2, "height");
    if (view->output) {
        lua_pushstring(L, view->output->name);
        lua_setfield(L, -2, "output");
    }
    /* The handler may close this very view; nothing touches it afterwards. */
    call(L, 2, 0, "on_click", NULL, 0);
}

static void view_key(void *data, struct shell_surface *surface, xkb_keysym_t sym,
        const char *utf8) {
    (void)surface;
    struct view *view = data;
    if (view->def->on_key == LUA_NOREF) return;
    lua_State *L = view->lua->L;
    char name[64];
    if (xkb_keysym_get_name(sym, name, sizeof(name)) < 0) name[0] = '\0';
    lua_rawgeti(L, LUA_REGISTRYINDEX, view->def->on_key);
    lua_pushstring(L, name);
    lua_pushstring(L, utf8);
    if (view->args != LUA_NOREF) lua_rawgeti(L, LUA_REGISTRYINDEX, view->args);
    else lua_pushnil(L);
    call(L, 3, 0, "on_key", NULL, 0);
}

static void view_closed(void *data, struct shell_surface *surface) {
    (void)surface;
    view_destroy(data);
}

static const struct shell_surface_handler view_handler = {
    .draw = view_draw,
    .closed = view_closed,
    .pointer_motion = view_motion,
    .pointer_leave = view_leave,
    .pointer_button = view_button,
    .key = view_key,
};

static struct view *view_create(struct shell_lua *lua, struct view_def *def,
        struct shell_output *output, int args, const int32_t *margin,
        uint32_t width, uint32_t height) {
    struct view *view = calloc(1, sizeof(*view));
    if (!view) return NULL;
    view->lua = lua;
    view->def = def;
    view->output = output;
    view->args = args;
    struct shell_surface_config config = {
        .name_space = def->name,
        .layer = def->layer,
        .anchor = def->anchor,
        .width = width,
        .height = height,
        .exclusive_zone = def->exclusive,
        .margin_top = margin[0],
        .margin_right = margin[1],
        .margin_bottom = margin[2],
        .margin_left = margin[3],
        .keyboard = def->keyboard,
        .output = output,
    };
    view->surface = shell_surface_create(&lua->shell->core, &config, &view_handler, view);
    if (!view->surface) {
        free(view);
        return NULL;
    }
    wl_list_insert(lua->views.prev, &view->link);
    return view;
}

static void view_destroy(struct view *view) {
    lua_State *L = view->lua->L;
    shell_surface_destroy(view->surface);
    ui_free(L, view->tree);
    if (view->args != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, view->args);
    wl_list_remove(&view->link);
    free(view);
}

static bool def_wants_output(const struct view_def *def, const struct shell_output *output) {
    if (def->all_outputs) return true;
    for (size_t i = 0; i < def->output_count; i++)
        if (!strcmp(def->outputs[i], output->name)) return true;
    return false;
}

static bool measure_popup(struct shell_lua *lua, struct view_def *def,
        struct shell_output *output, int args, uint32_t *width, uint32_t *height);

/* Open on `output` the persistent popups that are not showing anywhere. */
static void ensure_persistent(struct shell_lua *lua, struct shell_output *output) {
    for (size_t i = 0; i < lua->def_count; i++) {
        struct view_def *def = &lua->defs[i];
        if (!def->popup || !def->persistent || find_popup(lua, def->name)) continue;
        uint32_t width = 0, height = 0;
        if (measure_popup(lua, def, output, LUA_NOREF, &width, &height) &&
                view_create(lua, def, output, LUA_NOREF, def->margin, width, height))
            fprintf(stderr, "shady-shell: %s on %s\n", def->name,
                output->name[0] ? output->name : "output");
    }
}

static void create_bars(struct shell_lua *lua, struct shell_output *output) {
    for (size_t i = 0; i < lua->def_count; i++) {
        struct view_def *def = &lua->defs[i];
        if (def->popup || !def_wants_output(def, output)) continue;
        if (view_create(lua, def, output, LUA_NOREF, def->margin, def->width, def->height))
            fprintf(stderr, "shady-shell: %s on %s\n", def->name,
                output->name[0] ? output->name : "output");
    }
    ensure_persistent(lua, output);
}

/* Natural size of a popup's content, for its first surface. */
static bool measure_popup(struct shell_lua *lua, struct view_def *def,
        struct shell_output *output, int args, uint32_t *width, uint32_t *height) {
    struct view probe = { .lua = lua, .def = def, .output = output, .args = args };
    struct ui_node *tree = build_tree(&probe, 0, 0);
    if (!tree) return false;
    cairo_surface_t *scratch = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(scratch);
    struct ui_defaults defaults;
    ui_defaults(&defaults);
    ui_measure(cr, tree, &defaults);
    *width = def->width ? def->width : (uint32_t)ceil(tree->nat_w);
    *height = def->height ? def->height : (uint32_t)ceil(tree->nat_h);
    ui_free(lua->L, tree);
    cairo_destroy(cr);
    cairo_surface_destroy(scratch);
    return *width > 0 && *height > 0;
}

/* ---- the `shell` API: declarations ------------------------------------ */

static void no_view(lua_State *L, const char *what) {
    if (lua_of(L)->in_view) luaL_error(L, "%s cannot be called from a view function", what);
}

static uint32_t parse_anchor(lua_State *L, int t) {
    uint32_t anchor = 0;
    lua_getfield(L, t, "anchor");
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            const char *edge = lua_tostring(L, -1);
            if (edge && !strcmp(edge, "top")) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
            else if (edge && !strcmp(edge, "bottom")) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
            else if (edge && !strcmp(edge, "left")) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
            else if (edge && !strcmp(edge, "right")) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
            else luaL_error(L, "anchor edges are top, bottom, left and right");
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return anchor;
}

/* margin = {top=, right=, bottom=, left=} at index t, into out. */
static void parse_margin(lua_State *L, int t, int32_t out[4]) {
    lua_getfield(L, t, "margin");
    if (lua_istable(L, -1)) {
        static const char *const keys[] = { "top", "right", "bottom", "left" };
        for (int i = 0; i < 4; i++) {
            lua_getfield(L, -1, keys[i]);
            if (lua_isnumber(L, -1)) out[i] = (int32_t)lua_tointeger(L, -1);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

static enum zwlr_layer_shell_v1_layer parse_layer(lua_State *L, int t,
        enum zwlr_layer_shell_v1_layer fallback) {
    lua_getfield(L, t, "layer");
    const char *s = lua_tostring(L, -1);
    enum zwlr_layer_shell_v1_layer layer = fallback;
    if (s && !strcmp(s, "background")) layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    else if (s && !strcmp(s, "bottom")) layer = ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
    else if (s && !strcmp(s, "top")) layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    else if (s && !strcmp(s, "overlay")) layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    else if (s) luaL_error(L, "layer is background, bottom, top or overlay");
    lua_pop(L, 1);
    return layer;
}

static struct view_def *new_def(lua_State *L, bool popup) {
    struct shell_lua *lua = lua_of(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "name");
    const char *name = lua_tostring(L, -1);
    if (!name || !*name) luaL_error(L, "%s needs a name", popup ? "shell.popup" : "shell.bar");
    if (strlen(name) >= NAME_MAX_LEN) luaL_error(L, "name too long");
    if (find_def(lua, name, popup)) luaL_error(L, "'%s' is already defined", name);
    if (lua->def_count == MAX_DEFS) luaL_error(L, "too many bars and popups");
    lua_getfield(L, 1, "view");
    if (!lua_isfunction(L, -1)) luaL_error(L, "'%s' needs a view function", name);
    struct view_def *def = &lua->defs[lua->def_count];
    memset(def, 0, sizeof(*def));
    def->popup = popup;
    snprintf(def->name, sizeof(def->name), "%s", name);
    def->view = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pop(L, 1); /* name */
    lua_getfield(L, 1, "on_key");
    def->on_key = lua_isfunction(L, -1) ? luaL_ref(L, LUA_REGISTRYINDEX) : (lua_pop(L, 1), LUA_NOREF);
    lua_getfield(L, 1, "shader");
    if (lua_istable(L, -1)) lua_getfield(L, -1, "path");
    else lua_pushvalue(L, -1);
    if (lua_type(L, -1) == LUA_TSTRING) def->shader = strdup(lua_tostring(L, -1));
    lua_pop(L, 2);
    lua_getfield(L, 1, "uniforms");
    def->uniforms = lua_istable(L, -1) || lua_isfunction(L, -1)
        ? luaL_ref(L, LUA_REGISTRYINDEX) : (lua_pop(L, 1), LUA_NOREF);
    lua->def_count++;
    return def;
}

/* shell.bar{name=, view=, edge="top"|"bottom", size=38, exclusive=true,
 *           layer="top", outputs="*"|{names}} */
static int l_bar(lua_State *L) {
    struct view_def *def = new_def(L, false);
    lua_getfield(L, 1, "edge");
    const char *edge = luaL_optstring(L, -1, "top");
    bool bottom = !strcmp(edge, "bottom");
    if (!bottom && strcmp(edge, "top") != 0) luaL_error(L, "bar edge is top or bottom");
    lua_pop(L, 1);
    lua_getfield(L, 1, "size");
    def->height = (uint32_t)luaL_optinteger(L, -1, 38);
    lua_pop(L, 1);
    lua_getfield(L, 1, "exclusive");
    bool exclusive = lua_isnil(L, -1) || lua_toboolean(L, -1);
    lua_pop(L, 1);
    def->exclusive = exclusive ? (int32_t)def->height : 0;
    def->anchor = (bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    def->layer = parse_layer(L, 1, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
    def->keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    parse_margin(L, 1, def->margin);
    lua_getfield(L, 1, "outputs");
    if (lua_istable(L, -1)) {
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n && def->output_count < MAX_DEF_OUTPUTS; i++) {
            lua_rawgeti(L, -1, i);
            snprintf(def->outputs[def->output_count++], NAME_MAX_LEN, "%s", luaL_checkstring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        def->all_outputs = true;
    }
    lua_pop(L, 1);
    return 0;
}

/* shell.popup{name=, view=, on_key=, layer="overlay", anchor={...},
 *             margin={...}, width=, height=, keyboard="none"|"exclusive"|"on_demand",
 *             persistent=false} */
static int l_popup(lua_State *L) {
    struct view_def *def = new_def(L, true);
    def->layer = parse_layer(L, 1, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
    def->anchor = parse_anchor(L, 1);
    parse_margin(L, 1, def->margin);
    lua_getfield(L, 1, "width");
    def->width = (uint32_t)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "height");
    def->height = (uint32_t)luaL_optinteger(L, -1, 0);
    lua_pop(L, 1);
    lua_getfield(L, 1, "keyboard");
    const char *keyboard = luaL_optstring(L, -1, "none");
    if (!strcmp(keyboard, "exclusive"))
        def->keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE;
    else if (!strcmp(keyboard, "on_demand"))
        def->keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_ON_DEMAND;
    else if (!strcmp(keyboard, "none"))
        def->keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    else luaL_error(L, "keyboard is none, exclusive or on_demand");
    lua_pop(L, 1);
    lua_getfield(L, 1, "persistent");
    def->persistent = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return 0;
}

/* ---- popups ----------------------------------------------------------- */

static void close_popup(struct shell_lua *lua, const char *name) {
    struct view *view = find_popup(lua, name);
    if (view) view_destroy(view);
}

/* shell.open(name[, args]): args.output (output name) and args.margin
 * override the popup's placement; the whole table is passed to its view. */
static int l_open(lua_State *L) {
    no_view(L, "shell.open");
    struct shell_lua *lua = lua_of(L);
    const char *name = luaL_checkstring(L, 1);
    struct view_def *def = find_def(lua, name, true);
    if (!def) return luaL_error(L, "no popup named '%s'", name);
    close_popup(lua, name);

    int32_t margin[4];
    memcpy(margin, def->margin, sizeof(margin));
    struct shell_output *output = NULL;
    int args = LUA_NOREF;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "output");
        output = find_output(lua->shell, lua_tostring(L, -1));
        lua_pop(L, 1);
        parse_margin(L, 2, margin);
        lua_pushvalue(L, 2);
        args = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    uint32_t width = 0, height = 0;
    if (!measure_popup(lua, def, output, args, &width, &height) ||
            !view_create(lua, def, output, args, margin, width, height)) {
        if (args != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, args);
        lua_pushboolean(L, 0);
        return 1;
    }
    redraw_all(lua); /* views may show which popups are open */
    lua_pushboolean(L, 1);
    return 1;
}

static int l_close(lua_State *L) {
    no_view(L, "shell.close");
    struct shell_lua *lua = lua_of(L);
    struct view *view = find_popup(lua, luaL_checkstring(L, 1));
    if (view) {
        view_destroy(view);
        redraw_all(lua);
    }
    return 0;
}

static int l_toggle(lua_State *L) {
    struct shell_lua *lua = lua_of(L);
    if (find_popup(lua, luaL_checkstring(L, 1))) {
        lua_settop(L, 1);
        l_close(L);
        lua_pushboolean(L, 0);
        return 1;
    }
    return l_open(L);
}

static int l_is_open(lua_State *L) {
    lua_pushboolean(L, find_popup(lua_of(L), luaL_checkstring(L, 1)) != NULL);
    return 1;
}

static int l_redraw(lua_State *L) {
    redraw_all(lua_of(L));
    return 0;
}

static int l_on(lua_State *L) {
    struct shell_lua *lua = lua_of(L);
    const char *event = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_rawgeti(L, LUA_REGISTRYINDEX, lua->handlers);
    if (lua_getfield(L, -1, event) != LUA_TTABLE) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, event);
    }
    lua_pushvalue(L, 2);
    lua_rawseti(L, -2, (lua_Integer)lua_rawlen(L, -2) + 1);
    return 0;
}

static int l_log(lua_State *L) {
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (i > 1) luaL_addchar(&b, ' ');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    fprintf(stderr, "shady-shell: lua: %s\n", lua_tostring(L, -1));
    return 0;
}

/* ---- the `shell` API: compositor model and actions -------------------- */

static int l_workspaces(lua_State *L) {
    struct shell *shell = lua_of(L)->shell;
    lua_createtable(L, (int)shell->workspace_count, 0);
    for (size_t i = 0; i < shell->workspace_count; i++) {
        lua_pushstring(L, shell->workspaces[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

static int l_active_workspace(lua_State *L) {
    lua_pushstring(L, lua_of(L)->shell->active_workspace);
    return 1;
}

static void push_window(lua_State *L, const struct shell_window *w) {
    lua_createtable(L, 0, 7);
    lua_pushinteger(L, w->id);
    lua_setfield(L, -2, "id");
    lua_pushstring(L, w->app_id);
    lua_setfield(L, -2, "app_id");
    lua_pushstring(L, w->title);
    lua_setfield(L, -2, "title");
    lua_pushstring(L, w->workspace);
    lua_setfield(L, -2, "workspace");
    lua_pushboolean(L, w->focused);
    lua_setfield(L, -2, "focused");
    lua_pushboolean(L, w->maximized);
    lua_setfield(L, -2, "maximized");
    lua_pushboolean(L, w->fullscreen);
    lua_setfield(L, -2, "fullscreen");
}

static int l_windows(lua_State *L) {
    struct shell *shell = lua_of(L)->shell;
    lua_createtable(L, (int)shell->window_count, 0);
    for (size_t i = 0; i < shell->window_count; i++) {
        push_window(L, &shell->windows[i]);
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

static int l_focused(lua_State *L) {
    struct shell *shell = lua_of(L)->shell;
    for (size_t i = 0; i < shell->window_count; i++) {
        if (shell->windows[i].focused) {
            push_window(L, &shell->windows[i]);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int l_apps(lua_State *L) {
    struct shell *shell = lua_of(L)->shell;
    const char *query = luaL_optstring(L, 1, "");
    lua_Integer limit = luaL_optinteger(L, 2, MAX_APPS);
    if (limit < 0) limit = 0;
    if (limit > MAX_APPS) limit = MAX_APPS;
    size_t matches[MAX_APPS];
    size_t count = apps_matching(shell, query, matches, (size_t)limit);
    lua_createtable(L, (int)count, 0);
    for (size_t i = 0; i < count; i++) {
        const struct launcher_app *app = &shell->apps[matches[i]];
        lua_createtable(L, 0, 2);
        lua_pushstring(L, app->name);
        lua_setfield(L, -2, "name");
        lua_pushstring(L, app->exec);
        lua_setfield(L, -2, "exec");
        lua_rawseti(L, -2, (lua_Integer)i + 1);
    }
    return 1;
}

/* shell.spawn(command) / shell.launch(app): run through `sh -c`. */
static int l_spawn(lua_State *L) {
    const char *command = NULL;
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "exec");
        command = lua_tostring(L, -1);
    } else {
        command = luaL_checkstring(L, 1);
    }
    if (!command || !*command) return luaL_error(L, "nothing to run");
    apps_spawn(command);
    return 0;
}

static struct shady_shell_v1 *protocol_of(lua_State *L) {
    struct shady_shell_v1 *protocol = lua_of(L)->shell->protocol;
    if (!protocol) luaL_error(L, "not connected to the compositor's shell protocol");
    return protocol;
}

static uint32_t window_arg(lua_State *L) {
    lua_Integer id = lua_istable(L, 1) ? (lua_getfield(L, 1, "id"), lua_tointeger(L, -1))
        : luaL_checkinteger(L, 1);
    if (id <= 0 || id > UINT32_MAX) luaL_error(L, "bad window id");
    return (uint32_t)id;
}

static int l_workspace(lua_State *L) {
    shady_shell_v1_activate_workspace(protocol_of(L), luaL_checkstring(L, 1));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_focus(lua_State *L) {
    shady_shell_v1_activate_window(protocol_of(L), window_arg(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_close_window(lua_State *L) {
    shady_shell_v1_close_window(protocol_of(L), window_arg(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_maximize(lua_State *L) {
    shady_shell_v1_toggle_maximize(protocol_of(L), window_arg(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_fullscreen(lua_State *L) {
    shady_shell_v1_toggle_fullscreen(protocol_of(L), window_arg(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_move(lua_State *L) {
    uint32_t id = window_arg(L);
    shady_shell_v1_move_window_to_workspace(protocol_of(L), id, luaL_checkstring(L, 2));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_cycle(lua_State *L) {
    shady_shell_v1_cycle_window(protocol_of(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

static int l_quit(lua_State *L) {
    shady_shell_v1_terminate(protocol_of(L));
    shell_flush(lua_of(L)->shell);
    return 0;
}

/* ---- the `shell` API: values that change on their own ----------------- */

static void poll_start(struct poll *poll);

static void poll_stop(struct poll *poll) {
    if (poll->watch) shell_watch_remove(poll->watch);
    poll->watch = NULL;
    if (poll->fd >= 0) close(poll->fd);
    poll->fd = -1;
    if (poll->pid > 0) kill(poll->pid, SIGTERM);
    poll->pid = 0;
    poll->length = 0;
}

static void poll_set_value(struct poll *poll, const char *text, size_t length) {
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r' ||
            text[length - 1] == ' ' || text[length - 1] == '\t'))
        length--;
    if (poll->value && strlen(poll->value) == length && !memcmp(poll->value, text, length))
        return;
    char *value = strndup(text, length);
    if (!value) return;
    free(poll->value);
    poll->value = value;
    redraw_all(poll->lua);
}

static void poll_restart(void *data) {
    struct poll *poll = data;
    poll->timer = NULL;
    poll_start(poll);
}

static void poll_readable(int fd, short revents, void *data) {
    (void)revents;
    struct poll *poll = data;
    for (;;) {
        if (poll->length == sizeof(poll->buffer)) {
            /* A line or output this long is truncated, not grown. */
            poll_set_value(poll, poll->buffer, poll->length);
            poll->length = 0;
        }
        ssize_t n = read(fd, poll->buffer + poll->length, sizeof(poll->buffer) - poll->length);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (n > 0) {
            poll->length += (size_t)n;
            if (poll->listen) {
                /* Each complete line replaces the value. */
                char *last_nl = NULL;
                for (size_t i = 0; i < poll->length; i++)
                    if (poll->buffer[i] == '\n') last_nl = poll->buffer + i;
                if (last_nl) {
                    char *line = last_nl;
                    while (line > poll->buffer && line[-1] != '\n') line--;
                    poll_set_value(poll, line, (size_t)(last_nl - line));
                    size_t rest = poll->length - (size_t)(last_nl + 1 - poll->buffer);
                    memmove(poll->buffer, last_nl + 1, rest);
                    poll->length = rest;
                }
            }
            continue;
        }
        /* EOF or error: the command finished. */
        if (!poll->listen) poll_set_value(poll, poll->buffer, poll->length);
        poll->pid = 0; /* already exited; SIGCHLD is ignored so it is reaped */
        poll_stop(poll);
        poll->timer = shell_timer_add(&poll->lua->shell->core,
            poll->listen ? LISTEN_RESTART_MS : poll->interval_ms, poll_restart, poll);
        return;
    }
}

static void poll_start(struct poll *poll) {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) return;
    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }
    if (pid == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        int null = open("/dev/null", O_RDONLY);
        if (null >= 0) dup2(null, STDIN_FILENO);
        signal(SIGCHLD, SIG_DFL);
        execl("/bin/sh", "sh", "-c", poll->command, (char *)NULL);
        _exit(127);
    }
    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    poll->pid = pid;
    poll->fd = pipefd[0];
    poll->length = 0;
    poll->watch = shell_watch_add(&poll->lua->shell->core, poll->fd, POLLIN, poll_readable, poll);
}

static struct poll *find_or_start_poll(lua_State *L, const char *command, bool listen,
        uint32_t interval) {
    struct shell_lua *lua = lua_of(L);
    struct poll *poll;
    wl_list_for_each(poll, &lua->polls, link)
        if (poll->listen == listen && !strcmp(poll->command, command)) return poll;
    poll = calloc(1, sizeof(*poll));
    if (!poll || !(poll->command = strdup(command))) {
        free(poll);
        luaL_error(L, "out of memory");
    }
    poll->lua = lua;
    poll->listen = listen;
    poll->interval_ms = interval;
    poll->fd = -1;
    wl_list_insert(&lua->polls, &poll->link);
    poll_start(poll);
    return poll;
}

/* shell.poll(command, interval_ms): the command's last output, rerun every
 * interval. Returns "" (or `fallback`) until the first run finishes. */
static int l_poll(lua_State *L) {
    const char *command = luaL_checkstring(L, 1);
    lua_Integer interval = luaL_optinteger(L, 2, 1000);
    if (interval < 100) interval = 100;
    struct poll *poll = find_or_start_poll(L, command, false, (uint32_t)interval);
    lua_pushstring(L, poll->value ? poll->value : luaL_optstring(L, 3, ""));
    return 1;
}

/* shell.listen(command): the latest line printed by a long-running command. */
static int l_listen(lua_State *L) {
    struct poll *poll = find_or_start_poll(L, luaL_checkstring(L, 1), true, 0);
    lua_pushstring(L, poll->value ? poll->value : luaL_optstring(L, 2, ""));
    return 1;
}

static void date_tick(void *data) {
    struct shell_lua *lua = data;
    lua->date_timer = NULL;
    redraw_all(lua);
}

/* shell.date(format): strftime of the local time. Views that use it are
 * repainted when the formatted value can next change (each second if the
 * format shows seconds, else each minute). */
static int l_date(lua_State *L) {
    struct shell_lua *lua = lua_of(L);
    const char *format = luaL_optstring(L, 1, "%H:%M");
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm local;
    localtime_r(&now.tv_sec, &local);
    char out[256];
    size_t n = strftime(out, sizeof(out), format, &local);
    lua_pushlstring(L, out, n);

    bool seconds = false;
    for (const char *p = format; *p; p++)
        if (p[0] == '%' && p[1] && strchr("STsrcX", p[1])) seconds = true;
    long ms_into = seconds ? now.tv_nsec / 1000000 :
        (long)local.tm_sec * 1000 + now.tv_nsec / 1000000;
    uint32_t wait = (uint32_t)((seconds ? 1000 : 60000) - ms_into + 5);
    if (lua->date_timer && seconds) {
        shell_timer_cancel(lua->date_timer);
        lua->date_timer = NULL;
    }
    if (!lua->date_timer) lua->date_timer = shell_timer_add(&lua->shell->core, wait, date_tick, lua);
    return 1;
}

/* ---- the `shell` API: native plugins --------------------------------- */

/* shell.plugin(name[, options]): load a native plugin once per shell
 * process. Returns true, or false and a message. */
static int l_plugin(lua_State *L) {
    const char *spec = luaL_checkstring(L, 1);
    struct shady_shell_props_handle options = {0};
    if (lua_istable(L, 2)) ui_read_props(L, 2, &options);
    char dir[sizeof(host.config)];
    snprintf(dir, sizeof(dir), "%s", host.config);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof(dir), ".");
    char error[512];
    bool ok = shell_plugins_load(spec, &options, dir, error, sizeof(error));
    ui_free_props(&options);
    lua_pushboolean(L, ok);
    if (ok) return 1;
    fprintf(stderr, "shady-shell: %s\n", error);
    lua_pushstring(L, error);
    return 2;
}

/* shell.value(key[, fallback]): a value published by a plugin. */
static int l_value(lua_State *L) {
    const char *value = shell_plugins_value(luaL_checkstring(L, 1));
    if (value) lua_pushstring(L, value);
    else if (lua_isnoneornil(L, 2)) lua_pushnil(L);
    else lua_pushvalue(L, 2);
    return 1;
}

/* shell.compositor_value(key[, fallback]): a value the compositor
 * published (shady.publish in its Lua, publish_value in its plugins). */
static int l_compositor_value(lua_State *L) {
    const char *value = shell_compositor_value(luaL_checkstring(L, 1));
    if (value) lua_pushstring(L, value);
    else if (lua_isnoneornil(L, 2)) lua_pushnil(L);
    else lua_pushvalue(L, 2);
    return 1;
}

/* shell.action(name[, argument]): run a plugin action. */
static int l_action(lua_State *L) {
    no_view(L, "shell.action");
    const char *name = luaL_checkstring(L, 1);
    const char *argument = lua_isnoneornil(L, 2) ? "" : luaL_tolstring(L, 2, NULL);
    lua_pushboolean(L, shell_plugins_action(name, argument));
    return 1;
}

/* ---- the `shell` API: shaders ---------------------------------------- */

static bool is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* shell.shader(path): a fragment shader for `shader =` on a widget, bar or
 * popup. Relative paths are looked up next to the config, then in the data
 * directory (which ships shell/shaders/). */
static int l_shader(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    char path[2 * PATH_MAX];
    bool found = false;
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name);
        found = is_file(path);
    } else {
        char dir[sizeof(host.config)];
        snprintf(dir, sizeof(dir), "%s", host.config);
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
        const char *roots[] = { slash ? dir : ".", host.data_dir };
        for (int i = 0; i < 2 && !found; i++) {
            snprintf(path, sizeof(path), "%s/%s", roots[i], name);
            found = is_file(path);
        }
    }
    if (!found) return luaL_error(L, "shader not found: %s", name);
    lua_createtable(L, 0, 2);
    lua_pushstring(L, "shader");
    lua_setfield(L, -2, "type");
    lua_pushstring(L, path);
    lua_setfield(L, -2, "path");
    return 1;
}

/* ---- the `shell` API: colours ----------------------------------------- */

static void check_color(lua_State *L, int index, double rgba[4]) {
    if (!ui_parse_hex(luaL_checkstring(L, index), rgba))
        luaL_argerror(L, index, "colour must be #RGB, #RRGGBB or #RRGGBBAA");
}

/* shell.mix(a, b, t): blend two colours. */
static int l_mix(lua_State *L) {
    double a[4], b[4], out[4];
    check_color(L, 1, a);
    check_color(L, 2, b);
    double t = luaL_checknumber(L, 3);
    for (int i = 0; i < 4; i++) out[i] = a[i] + (b[i] - a[i]) * t;
    push_hex(L, out);
    return 1;
}

/* shell.alpha(colour, a): the colour with its alpha multiplied by a. */
static int l_alpha(lua_State *L) {
    double c[4];
    check_color(L, 1, c);
    c[3] *= luaL_checknumber(L, 2);
    push_hex(L, c);
    return 1;
}

/* ---- generations ------------------------------------------------------ */

/* Widget constructors, written in Lua because they only shape tables. */
static const char *prelude =
    "local shell = ...\n"
    "local function widget(kind, defaults)\n"
    "  return function(props)\n"
    "    props = props or {}\n"
    "    props.type = kind\n"
    "    for k, v in pairs(defaults) do if props[k] == nil then props[k] = v end end\n"
    "    return props\n"
    "  end\n"
    "end\n"
    "shell.box = widget('box', {})\n"
    "shell.row = widget('box', { direction = 'row' })\n"
    "shell.column = widget('box', { direction = 'column' })\n"
    "shell.pip = widget('pip', {})\n"
    "shell.image = widget('image', {})\n"
    "function shell.spacer(props)\n"
    "  props = props or {}\n"
    "  props.type = 'box'\n"
    "  if props.grow == nil and props.width == nil then props.grow = 1 end\n"
    "  return props\n"
    "end\n"
    "function shell.text(text, props)\n"
    "  if type(text) == 'table' then props = text\n"
    "  else props = props or {}; props.text = text end\n"
    "  props.type = 'text'\n"
    "  return props\n"
    "end\n"
    "function shell.widget(kind, props)\n"
    "  props = props or {}\n"
    "  props.type = 'custom'\n"
    "  props.widget = kind\n"
    "  return props\n"
    "end\n"
    "function shell.gradient(direction, ...)\n"
    "  return { gradient = direction, ... }\n"
    "end\n";

static const luaL_Reg api[] = {
    { "log", l_log },
    { "bar", l_bar },
    { "popup", l_popup },
    { "open", l_open },
    { "close", l_close },
    { "toggle", l_toggle },
    { "is_open", l_is_open },
    { "redraw", l_redraw },
    { "on", l_on },
    { "workspaces", l_workspaces },
    { "active_workspace", l_active_workspace },
    { "windows", l_windows },
    { "focused", l_focused },
    { "apps", l_apps },
    { "launch", l_spawn },
    { "spawn", l_spawn },
    { "workspace", l_workspace },
    { "focus", l_focus },
    { "close_window", l_close_window },
    { "maximize", l_maximize },
    { "fullscreen", l_fullscreen },
    { "move", l_move },
    { "cycle", l_cycle },
    { "quit", l_quit },
    { "poll", l_poll },
    { "listen", l_listen },
    { "date", l_date },
    { "shader", l_shader },
    { "plugin", l_plugin },
    { "value", l_value },
    { "compositor_value", l_compositor_value },
    { "action", l_action },
    { "mix", l_mix },
    { "alpha", l_alpha },
    { NULL, NULL },
};

static void push_theme(lua_State *L) {
    static const struct { const char *name; const struct ui_rgb *rgb; } colours[] = {
        { "accent", &UI_ACCENT }, { "accent_2", &UI_ACCENT_2 },
        { "accent_deep", &UI_ACCENT_DEEP }, { "surface", &UI_SURFACE },
        { "text", &UI_TEXT }, { "text_dim", &UI_TEXT_DIM }, { "danger", &UI_DANGER },
    };
    lua_createtable(L, 0, 7);
    for (size_t i = 0; i < sizeof(colours) / sizeof(colours[0]); i++) {
        const double rgba[4] = { colours[i].rgb->r, colours[i].rgb->g, colours[i].rgb->b, 1 };
        push_hex(L, rgba);
        lua_setfield(L, -2, colours[i].name);
    }
}

static void generation_free(struct shell_lua *lua) {
    if (!lua) return;
    struct view *view, *vtmp;
    wl_list_for_each_safe(view, vtmp, &lua->views, link) view_destroy(view);
    struct poll *poll, *ptmp;
    wl_list_for_each_safe(poll, ptmp, &lua->polls, link) {
        poll_stop(poll);
        shell_timer_cancel(poll->timer);
        wl_list_remove(&poll->link);
        free(poll->command);
        free(poll->value);
        free(poll);
    }
    for (size_t i = 0; i < lua->def_count; i++) free(lua->defs[i].shader);
    shell_timer_cancel(lua->date_timer);
    if (lua->L) lua_close(lua->L);
    free(lua);
}

static struct shell_lua *generation_load(struct shell *shell, const char *path) {
    struct shell_lua *lua = calloc(1, sizeof(*lua));
    if (!lua) return NULL;
    lua->shell = shell;
    wl_list_init(&lua->views);
    wl_list_init(&lua->polls);
    lua_State *L = lua->L = luaL_newstate();
    if (!L) {
        free(lua);
        return NULL;
    }
    luaL_openlibs(L);
    lua_pushlightuserdata(L, lua);
    lua_setfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    lua_newtable(L);
    lua->handlers = luaL_ref(L, LUA_REGISTRYINDEX);

    luaL_newlib(L, api);
    push_theme(L);
    lua_setfield(L, -2, "theme");
    lua_pushstring(L, host.data_dir);
    lua_setfield(L, -2, "data_dir");
    lua_pushstring(L, path);
    lua_setfield(L, -2, "config_path");
    lua_pushvalue(L, -1);
    lua_setglobal(L, "shell");
    if (luaL_loadstring(L, prelude) != LUA_OK) abort(); /* a bug in the prelude */
    lua_insert(L, -2);
    lua_call(L, 1, 0);

    /* require() finds modules next to the config and in the data dir. */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof(dir), ".");
    lua_getglobal(L, "package");
    lua_pushfstring(L, "%s/?.lua;%s/?.lua;", dir, host.data_dir);
    lua_getfield(L, -2, "path");
    lua_concat(L, 2);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    if (luaL_loadfile(L, path) != LUA_OK) {
        fprintf(stderr, "shady-shell: %s\n", lua_tostring(L, -1));
        generation_free(lua);
        return NULL;
    }
    char what[PATH_MAX + 8];
    snprintf(what, sizeof(what), "%s", path);
    if (!call(L, 0, 0, what, NULL, 0)) {
        generation_free(lua);
        return NULL;
    }
    return lua;
}

static void generation_start(struct shell *shell, struct shell_lua *lua) {
    shell->lua = lua;
    struct shell_output *output;
    wl_list_for_each(output, &shell->core.outputs, link)
        if (output->ready) create_bars(lua, output);
}

/* ---- config files and reloading --------------------------------------- */

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void resolve_paths(void) {
    const char *data = getenv("SHADY_SHELL_DATA_DIR");
    snprintf(host.data_dir, sizeof(host.data_dir), "%s",
        data && *data ? data : SHADY_SHELL_DATA_DIR);

    const char *config = getenv("SHADY_SHELL_CONFIG");
    if (config && *config) {
        snprintf(host.config, sizeof(host.config), "%s", config);
        return;
    }
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg) snprintf(host.config, sizeof(host.config), "%s/shady/shell.lua", xdg);
    else if (home && *home) snprintf(host.config, sizeof(host.config), "%s/.config/shady/shell.lua", home);
    else host.config[0] = '\0';
    if (!host.config[0] || !file_exists(host.config))
        snprintf(host.config, sizeof(host.config), "%s/default.lua", host.data_dir);
}

static void reload(void *data) {
    (void)data;
    host.debounce = NULL;
    struct shell *shell = host.shell;
    struct shell_lua *next = generation_load(shell, host.config);
    if (!next) {
        fprintf(stderr, "shady-shell: reload failed; keeping the running config\n");
        return;
    }
    generation_free(shell->lua);
    shell->lua = NULL;
    /* Shader files may have changed too: compile them again on next use. */
    struct shell_renderer *renderer = shell->core.renderer;
    if (renderer && renderer->impl->forget_shaders) renderer->impl->forget_shaders(renderer);
    generation_start(shell, next);
    fprintf(stderr, "shady-shell: reloaded %s\n", host.config);
}

static void config_changed(int fd, short revents, void *data) {
    (void)revents;
    (void)data;
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    bool relevant = false;
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n <= 0) break;
        for (char *p = buffer; p < buffer + n;) {
            struct inotify_event *event = (struct inotify_event *)p;
            size_t len = event->len ? strlen(event->name) : 0;
            const char *dot = len ? strrchr(event->name, '.') : NULL;
            if (dot && (!strcmp(dot, ".lua") || !strcmp(dot, ".frag") || !strcmp(dot, ".glsl")))
                relevant = true;
            p += sizeof(*event) + event->len;
        }
    }
    if (!relevant) return;
    /* Editors write in bursts; reload once things settle. */
    shell_timer_cancel(host.debounce);
    host.debounce = shell_timer_add(&host.shell->core, RELOAD_DEBOUNCE_MS, reload, NULL);
}

static void watch_dir_of(const char *path) {
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof(dir), ".");
    if (inotify_add_watch(host.inotify, dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE) < 0)
        fprintf(stderr, "shady-shell: cannot watch %s for changes\n", dir);
}

/* ---- public ----------------------------------------------------------- */

bool shell_lua_init(struct shell *shell) {
    host.shell = shell;
    resolve_paths();
    struct shell_lua *lua = generation_load(shell, host.config);
    if (!lua) {
        char fallback[PATH_MAX + 16];
        snprintf(fallback, sizeof(fallback), "%s/default.lua", host.data_dir);
        if (strcmp(fallback, host.config) != 0) {
            fprintf(stderr, "shady-shell: falling back to %s\n", fallback);
            lua = generation_load(shell, fallback);
        }
        if (!lua) return false;
    } else {
        fprintf(stderr, "shady-shell: loaded %s\n", host.config);
    }
    generation_start(shell, lua);

    host.inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (host.inotify >= 0) {
        watch_dir_of(host.config);
        host.watch = shell_watch_add(&shell->core, host.inotify, POLLIN, config_changed, NULL);
    }
    return true;
}

void shell_lua_finish(struct shell *shell) {
    generation_free(shell->lua);
    shell->lua = NULL;
    shell_timer_cancel(host.debounce);
    host.debounce = NULL;
    shell_watch_remove(host.watch);
    host.watch = NULL;
    if (host.inotify >= 0) close(host.inotify);
    host.inotify = -1;
}

void shell_lua_output_added(struct shell *shell, struct shell_output *output) {
    if (shell->lua) create_bars(shell->lua, output);
}

void shell_lua_output_removed(struct shell *shell, struct shell_output *output) {
    struct shell_lua *lua = shell->lua;
    if (!lua) return;
    struct view *view, *tmp;
    wl_list_for_each_safe(view, tmp, &lua->views, link) {
        if (view->output != output) continue;
        if (!view->def->popup)
            fprintf(stderr, "shady-shell: %s removed from %s\n", view->def->name, output->name);
        view_destroy(view);
    }
    struct shell_output *other;
    wl_list_for_each(other, &shell->core.outputs, link)
        if (other != output && other->ready) ensure_persistent(lua, other);
}

void shell_lua_model_changed(struct shell *shell) {
    if (shell->lua) redraw_all(shell->lua);
}

void shell_lua_emit(struct shell *shell, const char *event) {
    struct shell_lua *lua = shell->lua;
    if (!lua) return;
    lua_State *L = lua->L;
    lua_rawgeti(L, LUA_REGISTRYINDEX, lua->handlers);
    if (lua_getfield(L, -1, event) == LUA_TTABLE) {
        lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, -1, i);
            char what[96];
            snprintf(what, sizeof(what), "'%s' handler", event);
            /* A handler may trigger a reload only through the file system,
             * which happens later, so `lua` stays valid here. */
            call(L, 0, 0, what, NULL, 0);
        }
    }
    lua_pop(L, 2);
}
