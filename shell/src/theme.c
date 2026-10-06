#define _POSIX_C_SOURCE 200809L

#include "theme.h"

#include <stdio.h>
#include <stdlib.h>
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
