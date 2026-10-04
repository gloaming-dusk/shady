#ifndef SHADY_SHELL_THEME_H
#define SHADY_SHELL_THEME_H

/*
 * The shell palette. Defaults match Neon Transit; rices override them with
 * SHADY_SHELL_* colour variables (see ui_load_theme), and Lua configs read
 * them as shell.theme.
 */

#include <cairo/cairo.h>
#include <pango/pangocairo.h>

struct ui_rgb { double r, g, b; };

extern struct ui_rgb UI_ACCENT;
extern struct ui_rgb UI_ACCENT_2;
extern struct ui_rgb UI_ACCENT_DEEP;
extern struct ui_rgb UI_SURFACE;
extern struct ui_rgb UI_TEXT;
extern struct ui_rgb UI_TEXT_DIM;
extern struct ui_rgb UI_DANGER;

void ui_load_theme(void);

void rounded_rect(cairo_t *cr, double x, double y,
    double width, double height, double radius);

#endif
