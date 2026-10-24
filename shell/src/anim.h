#ifndef SHADY_SHELL_ANIM_H
#define SHADY_SHELL_ANIM_H

/*
 * Transitions for widget trees. A view returns a fresh tree on every
 * repaint, so a node is matched with its previous self by key: its `id`, or
 * else its parent's key and its index. Each animated node owns a set of
 * springs (rectangle relative to its parent, opacity, scale, translation
 * and colours) that follow whatever the newest tree says. While any spring
 * moves the owner keeps repainting the last tree without running Lua.
 */

#include <stdbool.h>

#include "ui.h"

/* Gradients are resampled to this many evenly spaced stops so that solid
 * colours and 2-, 3-, 4- and 7-stop gradients blend into each other. */
#define UI_ANIM_STOPS 7

enum {
    UI_CH_X, UI_CH_Y, UI_CH_W, UI_CH_H, /* x and y relative to the parent */
    UI_CH_OPACITY,
    UI_CH_SCALE,
    UI_CH_TX, UI_CH_TY,
    UI_CH_ROTATE,
    UI_CH_SHADOW_X, UI_CH_SHADOW_Y,
    UI_CH_SHADOW, /* rgba */
    UI_CH_BORDER = UI_CH_SHADOW + 4, /* rgba */
    UI_CH_COLOR = UI_CH_BORDER + 4,
    UI_CH_BACKGROUND = UI_CH_COLOR + 4, /* UI_ANIM_STOPS rgba stops */
    UI_CH_COUNT = UI_CH_BACKGROUND + UI_ANIM_STOPS * 4,
};

struct ui_anim {
    char *key;
    double value[UI_CH_COUNT];
    double velocity[UI_CH_COUNT];
    double target[UI_CH_COUNT];
    char dir; /* gradient direction of the background */
    bool seen; /* matched in the current tree */
    /* Where the node was drawn, in buffer pixels, this frame and the last:
     * frames that only move springs repaint the difference. */
    double drawn[4], last_drawn[4];
    bool has_drawn, had_drawn;
};

struct ui_anims;

struct ui_anims *ui_anims_create(void);
void ui_anims_destroy(struct ui_anims *anims);

/*
 * Match the animated nodes of a laid-out tree with their state, aim their
 * springs at the tree's values and advance them by `dt` seconds (0 on the
 * first paint after a pause). With `exiting`, nodes that have an `exit`
 * style aim at it. State of nodes that are gone is dropped. Returns true
 * while something still moves.
 */
bool ui_anims_step(struct ui_anims *anims, struct ui_node *tree,
    const struct ui_node *hovered, const struct ui_defaults *defaults, double dt,
    bool exiting);

/* Damage tracking for frames that only move springs: begin_frame keeps
 * where every node was drawn; after a pass of ui_draw (which records where
 * they are drawn now) damage gives the union of what changed, in buffer
 * pixels, false when nothing was recorded. */
void ui_anims_begin_frame(struct ui_anims *anims);
bool ui_anims_damage(struct ui_anims *anims, double rect[4]);

/* Whether any node in the tree has an `exit` style. */
bool ui_tree_has_exit(const struct ui_node *tree);

/* "smooth", "snappy", "bouncy" or "gentle"; false for other names. */
bool ui_spring_preset(const char *name, struct ui_spring *out);

#endif
