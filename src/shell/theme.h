#ifndef SHADY_SHELL_THEME_H
#define SHADY_SHELL_THEME_H

/*
 * Visual language shared with the compositor's window frames: deep navy glass
 * with a faint top sheen, hairline edges, and a cyan accent that appears as a
 * glowing status pip on whatever currently has focus.
 */

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdbool.h>

struct ui_rgb { double r, g, b; };

/* Defaults match Neon Transit; rices override them with SHADY_SHELL_* colour
 * variables (see ui_load_theme). */
extern struct ui_rgb UI_ACCENT;
extern struct ui_rgb UI_ACCENT_2;
extern struct ui_rgb UI_ACCENT_DEEP;
extern struct ui_rgb UI_SURFACE;
extern struct ui_rgb UI_TEXT;
extern struct ui_rgb UI_TEXT_DIM;
extern struct ui_rgb UI_DANGER;

void ui_load_theme(void);
struct ui_rgb ui_mix(struct ui_rgb a, struct ui_rgb b, double t);

PangoLayout *make_layout_sized(cairo_t *cr, const char *text, bool bold, int size);
/* Bar labels: measured and drawn through the same layout so hit regions
 * always match the rendered text. */
PangoLayout *make_layout(cairo_t *cr, const char *text, bool bold);
int text_width(cairo_t *cr, const char *text, bool bold);
void draw_layout_alpha(cairo_t *cr, PangoLayout *layout,
    double x, double y, struct ui_rgb color, double alpha);
void draw_layout(cairo_t *cr, PangoLayout *layout,
    double x, double y, double r, double g, double b);
/* Vertically centre a layout inside [y, y + height). */
double layout_center_y(PangoLayout *layout, double y, double height);
void draw_text_color(cairo_t *cr, const char *text,
    double x, double y, bool bold, int size, double r, double g, double b);

void rounded_rect(cairo_t *cr, double x, double y,
    double width, double height, double radius);
/* Rounded fill with a soft vertical sheen: `lift` brightens the top edge. */
void fill_sheen(cairo_t *cr, double x, double y, double w, double h,
    double radius, struct ui_rgb base, double alpha, double lift);
/* 1px outline aligned to the pixel grid. */
void stroke_hairline(cairo_t *cr, double x, double y, double w, double h,
    double radius, struct ui_rgb color, double alpha);
/* Focus pip, matching the compositor title bar: a lit dot with a halo for
 * the focused item, a dim ring otherwise. */
void draw_pip(cairo_t *cr, double x, double y, double radius, bool lit);
/* Popup/panel body: glass card, hairline rim and a brighter top edge. */
void draw_panel(cairo_t *cr, double w, double h, double radius);
/* Highlighted menu/list row: accent-tinted glass plus a focus pip. */
void draw_row_highlight(cairo_t *cr, double x, double y, double w,
    double h, double radius, bool strong, bool danger);
/* Accent gradient tile with a dark monogram, used as the brand badge. */
void draw_badge(cairo_t *cr, double x, double y, double size, double radius);

#endif
