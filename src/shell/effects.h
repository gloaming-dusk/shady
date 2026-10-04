#ifndef SHADY_SHELL_EFFECTS_H
#define SHADY_SHELL_EFFECTS_H

/*
 * Shader effects on a surface: an optional shader for the whole surface and
 * any number of widget shaders, each drawing over its widget's rectangle.
 * The Lua runtime fills a surface's list on every repaint; the GL renderer
 * composites with it, and keeps doing so every frame while any of the
 * shaders uses u_time. See docs/SHELL_LUA_API.md, "Shader effects".
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#define SHELL_EFFECT_UNIFORMS 16
#define SHELL_UNIFORM_NAME_MAX 40

/* A user uniform: Lua key `strength` becomes GLSL `u_strength`. */
struct shell_uniform {
    char name[SHELL_UNIFORM_NAME_MAX]; /* includes the u_ prefix */
    int size; /* 1..4 floats */
    float value[4];
};

struct shell_effect {
    char *shader; /* absolute path of the fragment shader, owned */
    double x, y, width, height; /* logical px within the surface */
    double radius;
    struct shell_uniform uniforms[SHELL_EFFECT_UNIFORMS];
    size_t uniform_count;
};

struct shell_effects {
    struct shell_effect surface; /* shader == NULL: plain composition */
    struct shell_effect *widgets;
    size_t count, capacity;
};

static inline void shell_effect_reset(struct shell_effect *effect) {
    free(effect->shader);
    effect->shader = NULL;
    effect->uniform_count = 0;
}

void shell_effects_clear(struct shell_effects *effects);
void shell_effects_finish(struct shell_effects *effects);
/* A zeroed slot at the end of the widget list, or NULL. */
struct shell_effect *shell_effects_add(struct shell_effects *effects);
bool shell_effects_any(const struct shell_effects *effects);

#endif
