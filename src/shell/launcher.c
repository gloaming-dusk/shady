#define _POSIX_C_SOURCE 200809L

/* The application launcher: a centred overlay with a search field and the
 * matching .desktop entries. It takes keyboard focus while open. */

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "theme.h"

static void launcher_draw(void *data, struct shell_surface *surface, cairo_t *cr,
        double lw, double lh) {
    (void)surface;
    struct shell *shell = data;
    struct launcher *launcher = &shell->launcher;

    draw_panel(cr, lw, lh, 18);

    draw_badge(cr, 22, 18, 34, 10);
    draw_text_color(cr, "S", 34, 24, true, 12, 0.02, 0.06, 0.10);
    draw_text_color(cr, "Applications", 68, 18, true, 13,
        UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
    draw_text_color(cr, "Launch something", 68, 38, false, 9,
        UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);
    /* Keycap hint. */
    PangoLayout *esc = make_layout_sized(cr, "Esc", true, 8);
    int esc_w = 0;
    pango_layout_get_pixel_size(esc, &esc_w, NULL);
    double esc_x = lw - 22 - esc_w - 12;
    rounded_rect(cr, esc_x, 24, esc_w + 12, 20, 5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
    cairo_fill(cr);
    stroke_hairline(cr, esc_x, 24, esc_w + 12, 20, 5, UI_TEXT, 0.14);
    draw_layout_alpha(cr, esc, esc_x + 6, layout_center_y(esc, 24, 20), UI_TEXT_DIM, 1.0);
    g_object_unref(esc);

    /* Search field: recessed well with an accent focus ring. */
    rounded_rect(cr, 22, 68, lw - 44, 48, 12);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
    cairo_fill(cr);
    stroke_hairline(cr, 22, 68, lw - 44, 48, 12, UI_ACCENT, 0.42);
    stroke_hairline(cr, 20, 66, lw - 40, 52, 14, UI_ACCENT, 0.10);
    draw_text_color(cr, "⌕", 38, 79, false, 13,
        UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b);
    if (launcher->search[0]) {
        PangoLayout *query = make_layout_sized(cr, launcher->search, true, 10);
        int query_w = 0;
        pango_layout_get_pixel_size(query, &query_w, NULL);
        draw_layout_alpha(cr, query, 62, layout_center_y(query, 68, 48), UI_TEXT, 1.0);
        g_object_unref(query);
        /* Caret after the query. */
        cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.9);
        cairo_rectangle(cr, 64 + query_w, 82, 1.5, 20);
        cairo_fill(cr);
    } else {
        PangoLayout *hint = make_layout_sized(cr, "Search applications…", false, 10);
        draw_layout_alpha(cr, hint, 62, layout_center_y(hint, 68, 48), UI_TEXT_DIM, 0.75);
        g_object_unref(hint);
    }

    size_t matches[LAUNCHER_RESULTS];
    size_t count = apps_matching(shell, launcher->search, matches);
    if (count == 0) launcher->selected = 0;
    else if (launcher->selected >= count) launcher->selected = count - 1;

    double y = 132.0;
    for (size_t i = 0; i < count; i++) {
        const struct launcher_app *app = &shell->apps[matches[i]];
        bool selected = i == launcher->selected;
        bool hovered = launcher->hovered == (int)i;
        if (selected || hovered)
            draw_row_highlight(cr, 22, y - 7, lw - 44, 46, 11, selected, false);

        /* Monogram tile stands in for an app icon. */
        char initial[8] = {0};
        gunichar first = g_utf8_get_char_validated(app->name, -1);
        if (first != (gunichar)-1 && first != (gunichar)-2 && first != 0)
            g_unichar_to_utf8(g_unichar_toupper(first), initial);
        else
            initial[0] = '?';
        fill_sheen(cr, 34, y - 1, 34, 34, 9,
            selected ? ui_mix(UI_ACCENT_DEEP, UI_ACCENT, 0.18) : ui_mix(UI_SURFACE, UI_TEXT, 0.07),
            1.0, 0.10);
        stroke_hairline(cr, 34, y - 1, 34, 34, 9,
            selected ? UI_ACCENT : UI_TEXT, selected ? 0.45 : 0.10);
        PangoLayout *glyph = make_layout_sized(cr, initial, true, 11);
        int glyph_w = 0;
        pango_layout_get_pixel_size(glyph, &glyph_w, NULL);
        draw_layout_alpha(cr, glyph, 51 - glyph_w * 0.5,
            layout_center_y(glyph, y - 1, 34), UI_TEXT, selected ? 1.0 : 0.75);
        g_object_unref(glyph);

        PangoLayout *name = make_layout_sized(cr, app->name, selected, 10);
        pango_layout_set_ellipsize(name, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(name, (int)(lw - 140) * PANGO_SCALE);
        draw_layout_alpha(cr, name, 80, y, UI_TEXT, selected ? 1.0 : 0.84);
        g_object_unref(name);
        PangoLayout *exec = make_layout_sized(cr, app->exec, false, 8);
        pango_layout_set_ellipsize(exec, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(exec, (int)(lw - 140) * PANGO_SCALE);
        draw_layout_alpha(cr, exec, 80, y + 18, UI_TEXT_DIM, selected ? 0.9 : 0.65);
        g_object_unref(exec);
        if (selected) {
            PangoLayout *enter = make_layout_sized(cr, "↵", true, 10);
            draw_layout_alpha(cr, enter, lw - 52, layout_center_y(enter, y - 7, 46),
                UI_ACCENT, 0.85);
            g_object_unref(enter);
        }
        y += 50.0;
    }

    if (count == 0) {
        draw_text_color(cr, "No applications found", 38, 145, true, 10,
            UI_TEXT.r, UI_TEXT.g, UI_TEXT.b);
        draw_text_color(cr, "Try another name or executable", 38, 165, false, 9,
            UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);
    }

    /* Footer: hairline divider and keyboard hints. */
    double fy = lh - 44.0;
    cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
    cairo_rectangle(cr, 22, fy, lw - 44, 1);
    cairo_fill(cr);
    draw_text_color(cr, "↑ ↓  navigate     ↵  launch     Esc  close", 24,
        fy + 14, false, 8, UI_TEXT_DIM.r, UI_TEXT_DIM.g, UI_TEXT_DIM.b);
}

static void launcher_activate_selected(struct shell *shell) {
    struct launcher *launcher = &shell->launcher;
    size_t matches[LAUNCHER_RESULTS];
    size_t count = apps_matching(shell, launcher->search, matches);
    if (count == 0 || launcher->selected >= count) return;
    apps_spawn(&shell->apps[matches[launcher->selected]]);
    launcher_hide(shell);
}

static void launcher_hover(void *data, struct shell_surface *surface, double x, double y) {
    struct shell *shell = data;
    struct launcher *launcher = &shell->launcher;
    int hovered = -1;
    size_t matches[LAUNCHER_RESULTS];
    size_t count = apps_matching(shell, launcher->search, matches);
    if (y >= 125.0) {
        int row = (int)((y - 125.0) / 50.0);
        double row_y = 125.0 + row * 50.0;
        if (row >= 0 && (size_t)row < count && y < row_y + 46.0 &&
                x >= 22.0 && x < surface->width - 22.0)
            hovered = row;
    }
    if (hovered != launcher->hovered) {
        launcher->hovered = hovered;
        shell_surface_redraw(surface);
    }
}

static void launcher_leave(void *data, struct shell_surface *surface) {
    struct shell *shell = data;
    if (shell->launcher.hovered >= 0) shell_surface_redraw(surface);
    shell->launcher.hovered = -1;
}

static void launcher_button(void *data, struct shell_surface *surface, uint32_t button,
        bool pressed) {
    (void)surface;
    struct shell *shell = data;
    if (!pressed || button != BTN_LEFT || shell->launcher.hovered < 0) return;
    shell->launcher.selected = (size_t)shell->launcher.hovered;
    launcher_activate_selected(shell);
}

static void launcher_key(void *data, struct shell_surface *surface, xkb_keysym_t sym,
        const char *utf8) {
    struct shell *shell = data;
    struct launcher *launcher = &shell->launcher;
    if (sym == XKB_KEY_Escape) {
        launcher_hide(shell);
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        launcher_activate_selected(shell);
        return;
    }
    if (sym == XKB_KEY_Up) {
        if (launcher->selected > 0) launcher->selected--;
        shell_surface_redraw(surface);
        return;
    }
    if (sym == XKB_KEY_Down) {
        size_t matches[LAUNCHER_RESULTS];
        size_t count = apps_matching(shell, launcher->search, matches);
        if (count && launcher->selected + 1 < count) launcher->selected++;
        shell_surface_redraw(surface);
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        size_t len = strlen(launcher->search);
        if (len) {
            do { len--; } while (len && ((unsigned char)launcher->search[len] & 0xC0) == 0x80);
            launcher->search[len] = '\0';
            launcher->selected = 0;
            shell_surface_redraw(surface);
        }
        return;
    }
    size_t n = strlen(utf8);
    size_t len = strlen(launcher->search);
    if (n == 0 || len + n >= sizeof(launcher->search)) return;
    memcpy(launcher->search + len, utf8, n + 1);
    launcher->selected = 0;
    shell_surface_redraw(surface);
}

static void launcher_closed(void *data, struct shell_surface *surface) {
    (void)surface;
    launcher_hide(data);
}

static const struct shell_surface_handler launcher_handler = {
    .draw = launcher_draw,
    .closed = launcher_closed,
    .pointer_motion = launcher_hover,
    .pointer_leave = launcher_leave,
    .pointer_button = launcher_button,
    .key = launcher_key,
};

void launcher_show(struct shell *shell) {
    struct launcher *launcher = &shell->launcher;
    if (launcher->surface) return;
    launcher->search[0] = '\0';
    launcher->selected = 0;
    launcher->hovered = -1;
    const struct shell_surface_config config = {
        .name_space = "shady-launcher",
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .width = LAUNCHER_WIDTH,
        .height = LAUNCHER_HEIGHT,
        .keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE,
    };
    launcher->surface = shell_surface_create(&shell->core, &config, &launcher_handler, shell);
    if (launcher->surface) fprintf(stderr, "shady-shell: launcher opened\n");
}

void launcher_hide(struct shell *shell) {
    struct launcher *launcher = &shell->launcher;
    shell_surface_destroy(launcher->surface);
    launcher->surface = NULL;
    launcher->search[0] = '\0';
    launcher->selected = 0;
    launcher->hovered = -1;
}

void launcher_toggle(struct shell *shell) {
    if (shell->launcher.surface) launcher_hide(shell);
    else launcher_show(shell);
}
