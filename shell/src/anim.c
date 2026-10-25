#define _POSIX_C_SOURCE 200809L

#include "anim.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_MAX 256
#define STEP (1.0 / 240.0) /* integration substep, seconds */

struct ui_anims {
    struct ui_anim **states;
    size_t count, capacity;
    /* Open-addressing index into `states`, rebuilt after every step. */
    int32_t *index;
    size_t index_size; /* power of two, > 2 * count */
};

/* ---- springs ---------------------------------------------------------- */

static const struct {
    const char *name;
    struct ui_spring spring;
} presets[] = {
    { "smooth", { 230, 30 } }, /* critically damped */
    { "snappy", { 520, 44 } },
    { "bouncy", { 300, 20 } }, /* overshoots a little */
    { "gentle", { 120, 22 } },
};

bool ui_spring_preset(const char *name, struct ui_spring *out) {
    for (size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
        if (!strcmp(name, presets[i].name)) {
            *out = presets[i].spring;
            return true;
        }
    }
    return false;
}

/* How close to its target a channel must be, with negligible speed, to
 * count as settled: a tenth of a pixel, or about a quarter of a colour step. */
static double epsilon(int ch) {
    if (ch < UI_CH_OPACITY || ch == UI_CH_TX || ch == UI_CH_TY || ch == UI_CH_SHADOW_X ||
            ch == UI_CH_SHADOW_Y || ch == UI_CH_ROTATE)
        return 0.1; /* pixels, degrees */
    return 0.001;
}

/* ---- the state index -------------------------------------------------- */

static uint32_t hash(const char *s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static void reindex(struct ui_anims *anims) {
    size_t want = 16;
    while (want < anims->count * 2 + 1) want *= 2;
    if (want != anims->index_size) {
        int32_t *grown = realloc(anims->index, want * sizeof(*grown));
        if (!grown) {
            /* Without an index every node starts afresh; nothing breaks. */
            free(anims->index);
            anims->index = NULL;
            anims->index_size = 0;
            return;
        }
        anims->index = grown;
        anims->index_size = want;
    }
    for (size_t i = 0; i < anims->index_size; i++) anims->index[i] = -1;
    size_t mask = anims->index_size - 1;
    for (size_t i = 0; i < anims->count; i++) {
        size_t slot = hash(anims->states[i]->key) & mask;
        while (anims->index[slot] >= 0) slot = (slot + 1) & mask;
        anims->index[slot] = (int32_t)i;
    }
}

static struct ui_anim *find(struct ui_anims *anims, const char *key) {
    if (!anims->index_size) return NULL;
    size_t mask = anims->index_size - 1;
    for (size_t slot = hash(key) & mask; anims->index[slot] >= 0; slot = (slot + 1) & mask) {
        struct ui_anim *state = anims->states[anims->index[slot]];
        if (!strcmp(state->key, key)) return state;
    }
    return NULL;
}

/* A new state, indexed right away so a duplicate key finds it. */
static struct ui_anim *add(struct ui_anims *anims, const char *key) {
    if (anims->count == anims->capacity) {
        size_t next = anims->capacity ? anims->capacity * 2 : 16;
        struct ui_anim **grown = realloc(anims->states, next * sizeof(*grown));
        if (!grown) return NULL;
        anims->states = grown;
        anims->capacity = next;
    }
    struct ui_anim *state = calloc(1, sizeof(*state));
    if (!state || !(state->key = strdup(key))) {
        free(state);
        return NULL;
    }
    anims->states[anims->count++] = state;
    if (anims->count * 2 + 1 > anims->index_size) {
        reindex(anims);
    } else {
        size_t mask = anims->index_size - 1;
        size_t slot = hash(key) & mask;
        while (anims->index[slot] >= 0) slot = (slot + 1) & mask;
        anims->index[slot] = (int32_t)(anims->count - 1);
    }
    return state;
}

struct ui_anims *ui_anims_create(void) {
    return calloc(1, sizeof(struct ui_anims));
}

void ui_anims_destroy(struct ui_anims *anims) {
    if (!anims) return;
    for (size_t i = 0; i < anims->count; i++) {
        free(anims->states[i]->key);
        free(anims->states[i]);
    }
    free(anims->states);
    free(anims->index);
    free(anims);
}

/* ---- targets ---------------------------------------------------------- */

/* Sample a gradient at UI_ANIM_STOPS evenly spaced positions. */
static void resample(const struct ui_paint *paint, double out[UI_ANIM_STOPS][4]) {
    for (int i = 0; i < UI_ANIM_STOPS; i++) {
        double t = (double)i / (UI_ANIM_STOPS - 1);
        if (paint->stops <= 1) {
            memcpy(out[i], paint->rgba[0], sizeof(out[i]));
            continue;
        }
        double at = t * (paint->stops - 1);
        int lo = (int)floor(at);
        if (lo >= paint->stops - 1) lo = paint->stops - 2;
        double f = at - lo;
        for (int c = 0; c < 4; c++)
            out[i][c] = paint->rgba[lo][c] * (1 - f) + paint->rgba[lo + 1][c] * f;
    }
}

/* A colour that is absent fades to the transparent version of the current
 * one instead of to transparent black. */
static void set_colour(double *target, const double *value, const double rgba[4], bool present) {
    if (present) {
        memcpy(target, rgba, 4 * sizeof(double));
    } else {
        memcpy(target, value, 3 * sizeof(double));
        target[3] = 0;
    }
}

static void set_targets(struct ui_anim *state, const struct ui_node *node,
        const struct ui_style *s, const double origin[2], const struct ui_defaults *defaults) {
    double *t = state->target;
    t[UI_CH_X] = node->x - origin[0];
    t[UI_CH_Y] = node->y - origin[1];
    t[UI_CH_W] = node->w;
    t[UI_CH_H] = node->h;
    t[UI_CH_OPACITY] = s->has_opacity ? s->opacity : 1.0;
    t[UI_CH_SCALE] = s->has_scale ? s->scale : 1.0;
    t[UI_CH_TX] = s->has_translate ? s->translate[0] : 0;
    t[UI_CH_TY] = s->has_translate ? s->translate[1] : 0;
    t[UI_CH_ROTATE] = s->has_rotate ? s->rotate : 0;
    t[UI_CH_SHADOW_X] = s->has_shadow ? s->shadow[0] : state->value[UI_CH_SHADOW_X];
    t[UI_CH_SHADOW_Y] = s->has_shadow ? s->shadow[1] : state->value[UI_CH_SHADOW_Y];
    set_colour(&t[UI_CH_SHADOW], &state->value[UI_CH_SHADOW], s->shadow_color,
        s->has_shadow && s->shadow_color[3] > 0);
    set_colour(&t[UI_CH_BORDER], &state->value[UI_CH_BORDER], s->border, s->has_border);
    const double *fallback = node->kind == UI_NODE_PIP ? defaults->accent : defaults->text;
    memcpy(&t[UI_CH_COLOR], s->has_color ? s->color : fallback, 4 * sizeof(double));

    bool painted = s->has_background && s->background.stops > 0;
    if (painted) {
        double stops[UI_ANIM_STOPS][4];
        resample(&s->background, stops);
        for (int i = 0; i < UI_ANIM_STOPS; i++)
            memcpy(&t[UI_CH_BACKGROUND + 4 * i], stops[i], sizeof(stops[i]));
    } else {
        for (int i = 0; i < UI_ANIM_STOPS; i++) {
            double *stop = &t[UI_CH_BACKGROUND + 4 * i];
            memcpy(stop, &state->value[UI_CH_BACKGROUND + 4 * i], 3 * sizeof(double));
            stop[3] = 0;
        }
    }
    /* A solid colour has no direction, so it keeps the last one. */
    char dir = painted && s->background.stops > 1 ? s->background.dir : state->dir;
    if (dir != state->dir) {
        /* Gradients do not turn: a solid colour blends into the new
         * direction, a gradient jumps to it. */
        bool uniform = true;
        for (int i = UI_CH_BACKGROUND + 4; i < UI_CH_COUNT && uniform; i++)
            uniform = fabs(state->value[i] - state->value[i - 4]) < 1e-6;
        if (!uniform) {
            for (int i = UI_CH_BACKGROUND; i < UI_CH_COUNT; i++) {
                state->value[i] = t[i];
                state->velocity[i] = 0;
            }
        }
        state->dir = dir;
    }
}

/* ---- stepping --------------------------------------------------------- */

static bool advance(struct ui_anim *state, const struct ui_spring *spring, double dt) {
    bool moving = false;
    for (int ch = 0; ch < UI_CH_COUNT; ch++) {
        double x = state->value[ch], v = state->velocity[ch], target = state->target[ch];
        double eps = epsilon(ch);
        if (fabs(x - target) < eps && fabs(v) < eps * 10) {
            state->value[ch] = target;
            state->velocity[ch] = 0;
            continue;
        }
        for (double left = dt; left > 1e-9; left -= STEP) {
            double h = left < STEP ? left : STEP;
            v += (-spring->stiffness * (x - target) - spring->damping * v) * h;
            x += v * h;
        }
        state->value[ch] = x;
        state->velocity[ch] = v;
        moving = true;
    }
    return moving;
}

struct walk {
    struct ui_anims *anims;
    const struct ui_node *hovered;
    const struct ui_defaults *defaults;
    double dt;
    bool exiting;
    bool moving;
};

static void walk_node(struct walk *w, struct ui_node *node, const char *key,
        const double origin[2]) {
    node->anim = NULL;
    if (node->hidden) return;
    if (node->animate) {
        struct ui_anim *state = find(w->anims, key);
        bool fresh = false;
        if (!state || state->seen) {
            /* Ids are not guaranteed unique; a second node with a key that
             * is already taken this paint simply does not animate. */
            if (!state && (state = add(w->anims, key))) fresh = true;
            else state = NULL;
        }
        if (state) {
            state->seen = true;
            struct ui_style s;
            ui_resolve_style(node, w->hovered, &s);
            if (w->exiting && node->has_exit) ui_merge_style(&s, &node->exit);
            if (fresh) {
                /* Start from the enter style, or right at the target. */
                struct ui_style start = s;
                if (node->has_enter) ui_merge_style(&start, &node->enter);
                set_targets(state, node, &start, origin, w->defaults);
                memcpy(state->value, state->target, sizeof(state->value));
            }
            set_targets(state, node, &s, origin, w->defaults);
            if (advance(state, &node->spring, w->dt)) w->moving = true;
            node->anim = state;
        }
    }
    /* Children are positioned relative to this node's layout origin, so a
     * moving parent carries them along. */
    double here[2] = { node->x, node->y };
    char child_key[KEY_MAX];
    for (size_t i = 0; i < node->child_count; i++) {
        struct ui_node *child = node->children[i];
        if (child->key) snprintf(child_key, sizeof(child_key), "@%s", child->key);
        else if (child->id) snprintf(child_key, sizeof(child_key), "#%s", child->id);
        else snprintf(child_key, sizeof(child_key), "%s/%zu", key, i);
        walk_node(w, child, child_key, here);
    }
}

bool ui_anims_step(struct ui_anims *anims, struct ui_node *tree,
        const struct ui_node *hovered, const struct ui_defaults *defaults, double dt,
        bool exiting) {
    if (!anims || !tree) return false;
    for (size_t i = 0; i < anims->count; i++) anims->states[i]->seen = false;
    struct walk w = { .anims = anims, .hovered = hovered, .defaults = defaults, .dt = dt,
        .exiting = exiting };
    const double origin[2] = { 0, 0 };
    char key[KEY_MAX];
    if (tree->key) snprintf(key, sizeof(key), "@%s", tree->key);
    else if (tree->id) snprintf(key, sizeof(key), "#%s", tree->id);
    else snprintf(key, sizeof(key), "root");
    walk_node(&w, tree, key, origin);

    /* Forget nodes that are gone. */
    size_t kept = 0;
    for (size_t i = 0; i < anims->count; i++) {
        struct ui_anim *state = anims->states[i];
        if (state->seen) {
            anims->states[kept++] = state;
        } else {
            free(state->key);
            free(state);
        }
    }
    anims->count = kept;
    reindex(anims);
    return w.moving;
}

bool ui_tree_has_exit(const struct ui_node *tree) {
    if (!tree) return false;
    if (tree->has_exit) return true;
    for (size_t i = 0; i < tree->child_count; i++)
        if (ui_tree_has_exit(tree->children[i])) return true;
    return false;
}

void ui_anims_begin_frame(struct ui_anims *anims) {
    if (!anims) return;
    for (size_t i = 0; i < anims->count; i++) {
        struct ui_anim *a = anims->states[i];
        memcpy(a->last_drawn, a->drawn, sizeof(a->drawn));
        a->had_drawn = a->has_drawn;
        a->has_drawn = false;
    }
}

static void grow(double rect[4], bool *any, const double r[4]) {
    if (!*any) {
        memcpy(rect, r, 4 * sizeof(double));
        *any = true;
        return;
    }
    double x2 = fmax(rect[0] + rect[2], r[0] + r[2]);
    double y2 = fmax(rect[1] + rect[3], r[1] + r[3]);
    rect[0] = fmin(rect[0], r[0]);
    rect[1] = fmin(rect[1], r[1]);
    rect[2] = x2 - rect[0];
    rect[3] = y2 - rect[1];
}

bool ui_anims_damage(struct ui_anims *anims, double rect[4]) {
    bool any = false;
    if (!anims) return false;
    for (size_t i = 0; i < anims->count; i++) {
        struct ui_anim *a = anims->states[i];
        if (a->has_drawn == a->had_drawn &&
                (!a->has_drawn || !memcmp(a->drawn, a->last_drawn, sizeof(a->drawn))) &&
                !memcmp(a->value, a->target, sizeof(a->value)))
            continue;
        if (a->had_drawn) grow(rect, &any, a->last_drawn);
        if (a->has_drawn) grow(rect, &any, a->drawn);
    }
    if (!any) {
        rect[0] = rect[1] = rect[2] = rect[3] = 0;
        return true; /* nothing moved: an empty damage */
    }
    return true;
}
