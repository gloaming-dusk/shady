#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <stdio.h>
#include <string.h>

struct ui_rgb UI_ACCENT = { 0.157, 0.902, 1.000 };
struct ui_rgb UI_ACCENT_2 = { 0.100, 0.360, 0.900 };
struct ui_rgb UI_ACCENT_DEEP = { 0.040, 0.300, 0.420 };
struct ui_rgb UI_SURFACE = { 0.022, 0.030, 0.054 };
struct ui_rgb UI_TEXT = { 0.910, 0.965, 1.000 };
struct ui_rgb UI_TEXT_DIM = { 0.560, 0.650, 0.740 };
struct ui_rgb UI_DANGER = { 1.000, 0.420, 0.450 };

static void ui_theme_color(const char *name, struct ui_rgb *out) {
    const char *value = getenv(name);
    unsigned r, g, b;
    if (!value || value[0] != '#' || strlen(value) != 7 ||
            sscanf(value + 1, "%02x%02x%02x", &r, &g, &b) != 3) {
        if (value && *value)
            fprintf(stderr, "shady-shell: ignoring %s=%s (want #RRGGBB)\n", name, value);
        return;
    }
    *out = (struct ui_rgb){ r / 255.0, g / 255.0, b / 255.0 };
}

void ui_load_theme(void) {
    ui_theme_color("SHADY_SHELL_ACCENT", &UI_ACCENT);
    ui_theme_color("SHADY_SHELL_ACCENT_2", &UI_ACCENT_2);
    ui_theme_color("SHADY_SHELL_ACCENT_DEEP", &UI_ACCENT_DEEP);
    ui_theme_color("SHADY_SHELL_SURFACE", &UI_SURFACE);
    ui_theme_color("SHADY_SHELL_TEXT", &UI_TEXT);
    ui_theme_color("SHADY_SHELL_TEXT_DIM", &UI_TEXT_DIM);
    ui_theme_color("SHADY_SHELL_DANGER", &UI_DANGER);
}

static void add_letter_spacing(PangoLayout *layout) {
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(PANGO_SCALE / 4));
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
}

PangoLayout *make_layout_sized(cairo_t *cr, const char *text,
        bool bold, int size) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    char desc[64];
    snprintf(desc, sizeof(desc), bold ? "Sans SemiBold %d" : "Sans %d", size);
    PangoFontDescription *font = pango_font_description_from_string(desc);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_font_description_free(font);
    add_letter_spacing(layout);
    return layout;
}

/* Bar labels: measured and drawn through the same layout so hit regions
 * always match the rendered text. */
PangoLayout *make_layout(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(
        bold ? "Sans SemiBold 9.5" : "Sans Medium 9.5");
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text ? text : "", -1);
    pango_font_description_free(font);
    add_letter_spacing(layout);
    return layout;
}

int text_width(cairo_t *cr, const char *text, bool bold) {
    PangoLayout *layout = make_layout(cr, text, bold);
    int width = 0;
    pango_layout_get_pixel_size(layout, &width, NULL);
    g_object_unref(layout);
    return width;
}

void draw_layout_alpha(cairo_t *cr, PangoLayout *layout,
        double x, double y, struct ui_rgb color, double alpha) {
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
}

void draw_layout(cairo_t *cr, PangoLayout *layout,
        double x, double y, double r, double g, double b) {
    draw_layout_alpha(cr, layout, x, y, (struct ui_rgb){ r, g, b }, 1.0);
}

/* Vertically centre a layout inside [y, y + height). */
double layout_center_y(PangoLayout *layout, double y, double height) {
    int h = 0;
    pango_layout_get_pixel_size(layout, NULL, &h);
    return y + (height - h) * 0.5;
}

void draw_text_color(cairo_t *cr, const char *text,
        double x, double y, bool bold, int size,
        double r, double g, double b) {
    PangoLayout *layout = make_layout_sized(cr, text, bold, size);
    draw_layout(cr, layout, x, y, r, g, b);
    g_object_unref(layout);
}

void rounded_rect(cairo_t *cr, double x, double y,
        double width, double height, double radius) {
    double r = radius;
    if (r > width * 0.5) r = width * 0.5;
    if (r > height * 0.5) r = height * 0.5;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - r, y + r, r, -G_PI_2, 0);
    cairo_arc(cr, x + width - r, y + height - r, r, 0, G_PI_2);
    cairo_arc(cr, x + r, y + height - r, r, G_PI_2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, G_PI + G_PI_2);
    cairo_close_path(cr);
}

struct ui_rgb ui_mix(struct ui_rgb a, struct ui_rgb b, double t) {
    return (struct ui_rgb){
        a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
    };
}

/* Rounded fill with a soft vertical sheen: `lift` brightens the top edge. */
void fill_sheen(cairo_t *cr, double x, double y, double w, double h,
        double radius, struct ui_rgb base, double alpha, double lift) {
    struct ui_rgb top = ui_mix(base, (struct ui_rgb){ 1, 1, 1 }, lift);
    cairo_pattern_t *pattern = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(pattern, 0.0, top.r, top.g, top.b, alpha);
    cairo_pattern_add_color_stop_rgba(pattern, 1.0, base.r, base.g, base.b, alpha);
    rounded_rect(cr, x, y, w, h, radius);
    cairo_set_source(cr, pattern);
    cairo_fill(cr);
    cairo_pattern_destroy(pattern);
}

/* 1px outline aligned to the pixel grid. */
void stroke_hairline(cairo_t *cr, double x, double y, double w, double h,
        double radius, struct ui_rgb color, double alpha) {
    cairo_set_line_width(cr, 1.0);
    rounded_rect(cr, x + 0.5, y + 0.5, w - 1.0, h - 1.0, radius);
    cairo_set_source_rgba(cr, color.r, color.g, color.b, alpha);
    cairo_stroke(cr);
}

/* Focus pip, matching the compositor title bar: a lit dot with a halo for
 * the focused item, a dim ring otherwise. */
void draw_pip(cairo_t *cr, double x, double y, double radius, bool lit) {
    if (lit) {
        cairo_pattern_t *halo = cairo_pattern_create_radial(
            x, y, 0, x, y, radius * 3.2);
        cairo_pattern_add_color_stop_rgba(halo, 0.0,
            UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.45);
        cairo_pattern_add_color_stop_rgba(halo, 1.0,
            UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
        cairo_set_source(cr, halo);
        cairo_arc(cr, x, y, radius * 3.2, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(halo);
        cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
        cairo_arc(cr, x, y, radius, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, 1.0);
        cairo_set_source_rgba(cr, UI_TEXT.r, UI_TEXT.g, UI_TEXT.b, 0.28);
        cairo_arc(cr, x, y, radius - 0.5, 0, 2 * G_PI);
        cairo_stroke(cr);
    }
}

/* Popup/panel body: glass card, hairline rim and a brighter top edge. */
void draw_panel(cairo_t *cr, double w, double h, double radius) {
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    fill_sheen(cr, 0, 0, w, h, radius, UI_SURFACE, 0.975, 0.045);
    stroke_hairline(cr, 0, 0, w, h, radius, UI_TEXT, 0.08);
    /* Accent kiss along the top edge, fading out towards the corners. */
    cairo_pattern_t *edge = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgba(edge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
    cairo_pattern_add_color_stop_rgba(edge, 0.5, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.40);
    cairo_pattern_add_color_stop_rgba(edge, 1.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.0);
    cairo_set_source(cr, edge);
    cairo_rectangle(cr, radius, 0, w - 2 * radius, 1);
    cairo_fill(cr);
    cairo_pattern_destroy(edge);
}

/* Highlighted menu/list row: accent-tinted glass plus a focus pip. */
void draw_row_highlight(cairo_t *cr, double x, double y, double w,
        double h, double radius, bool strong, bool danger) {
    struct ui_rgb tint = danger ? ui_mix(UI_SURFACE, UI_DANGER, 0.28) : UI_ACCENT_DEEP;
    fill_sheen(cr, x, y, w, h, radius, tint, strong ? 0.85 : 0.55, 0.06);
    stroke_hairline(cr, x, y, w, h, radius,
        danger ? UI_DANGER : UI_ACCENT, strong ? 0.32 : 0.16);
}

void draw_badge(cairo_t *cr, double x, double y, double size, double radius) {
    cairo_pattern_t *badge = cairo_pattern_create_linear(x, y, x + size, y + size);
    cairo_pattern_add_color_stop_rgba(badge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
    cairo_pattern_add_color_stop_rgba(badge, 1.0, UI_ACCENT_2.r, UI_ACCENT_2.g, UI_ACCENT_2.b, 1.0);
    rounded_rect(cr, x, y, size, size, radius);
    cairo_set_source(cr, badge);
    cairo_fill(cr);
    cairo_pattern_destroy(badge);
    stroke_hairline(cr, x, y, size, size, radius, (struct ui_rgb){ 1, 1, 1 }, 0.25);
}
