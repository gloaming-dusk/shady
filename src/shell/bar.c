#define _POSIX_C_SOURCE 200809L

/* The bar: one per output, with workspaces, the task list and the clock,
 * which opens Quick Settings. */

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "shady-shell-v1-client-protocol.h"
#include "shell.h"
#include "theme.h"

static void bar_draw(void *data, struct shell_surface *surface, cairo_t *cr,
        double w, double h) {
    (void)surface;
    struct bar *bar = data;
    struct shell *shell = bar->shell;

    /* Glass strip: lighter at the top, settling into deep navy. */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_pattern_t *strip = cairo_pattern_create_linear(0, 0, 0, h);
    struct ui_rgb strip_top = ui_mix(UI_SURFACE, (struct ui_rgb){ 1, 1, 1 }, 0.025);
    struct ui_rgb strip_bottom = ui_mix(UI_SURFACE, (struct ui_rgb){ 0, 0, 0 }, 0.35);
    cairo_pattern_add_color_stop_rgba(strip, 0.0, strip_top.r, strip_top.g, strip_top.b, 0.94);
    cairo_pattern_add_color_stop_rgba(strip, 1.0, strip_bottom.r, strip_bottom.g, strip_bottom.b, 0.94);
    cairo_set_source(cr, strip);
    cairo_paint(cr);
    cairo_pattern_destroy(strip);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.045);
    cairo_rectangle(cr, 0, 0, w, 1);
    cairo_fill(cr);
    /* Bottom edge glows with the accent in the middle and fades out. */
    cairo_pattern_t *edge = cairo_pattern_create_linear(0, 0, w, 0);
    cairo_pattern_add_color_stop_rgba(edge, 0.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.06);
    cairo_pattern_add_color_stop_rgba(edge, 0.5, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.42);
    cairo_pattern_add_color_stop_rgba(edge, 1.0, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 0.06);
    cairo_set_source(cr, edge);
    cairo_rectangle(cr, 0, h - 1, w, 1);
    cairo_fill(cr);
    cairo_pattern_destroy(edge);

    /* Brand badge: accent gradient tile with a dark monogram. */
    draw_badge(cr, 8, 6, 26, 8);
    PangoLayout *mono = make_layout_sized(cr, "S", true, 11);
    int mono_w = 0;
    pango_layout_get_pixel_size(mono, &mono_w, NULL);
    draw_layout(cr, mono, 21 - mono_w * 0.5, layout_center_y(mono, 6, 26),
        0.02, 0.06, 0.10);
    g_object_unref(mono);
    PangoLayout *brand = make_layout(cr, "Shady", true);
    draw_layout_alpha(cr, brand, 42, layout_center_y(brand, 0, h), UI_TEXT, 0.92);
    g_object_unref(brand);

    double x = 94.0;
    /* Measure workspace pills first so they can share one recessed track. */
    double ws_x = x;
    for (size_t i = 0; i < shell->workspace_count; i++) {
        bool active = strcmp(shell->workspaces[i], shell->active_workspace) == 0;
        double box_width = text_width(cr, shell->workspaces[i], active) + 22.0;
        bar->workspace_regions[i].x0 = ws_x;
        bar->workspace_regions[i].x1 = ws_x + box_width;
        ws_x += box_width + 5.0;
    }
    if (shell->workspace_count > 0) {
        double track_w = bar->workspace_regions[shell->workspace_count - 1].x1 - x + 6.0;
        rounded_rect(cr, x - 3, 5, track_w, h - 10, 10);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.22);
        cairo_fill(cr);
        stroke_hairline(cr, x - 3, 5, track_w, h - 10, 10,
            (struct ui_rgb){ 1, 1, 1 }, 0.06);
    }
    for (size_t i = 0; i < shell->workspace_count; i++) {
        const char *name = shell->workspaces[i];
        bool active = strcmp(name, shell->active_workspace) == 0;
        bool hovered = bar->hovered_workspace == (int)i;
        double px = bar->workspace_regions[i].x0;
        double box_width = bar->workspace_regions[i].x1 - px;

        if (active) {
            fill_sheen(cr, px, 8, box_width, h - 16, 7, UI_ACCENT_DEEP, 0.92, 0.08);
            stroke_hairline(cr, px, 8, box_width, h - 16, 7, UI_ACCENT, 0.35);
        } else if (hovered) {
            rounded_rect(cr, px, 8, box_width, h - 16, 7);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.06);
            cairo_fill(cr);
        }
        PangoLayout *layout = make_layout(cr, name, active);
        double ty = layout_center_y(layout, 0, h);
        if (active) {
            cairo_set_source_rgba(cr, UI_ACCENT.r, UI_ACCENT.g, UI_ACCENT.b, 1.0);
            cairo_arc(cr, px + 10, h / 2.0, 2.2, 0, 2 * G_PI);
            cairo_fill(cr);
            draw_layout_alpha(cr, layout, px + 17, ty, UI_TEXT, 0.98);
        } else {
            draw_layout_alpha(cr, layout, px + 11, ty, UI_TEXT, hovered ? 0.85 : 0.55);
        }
        g_object_unref(layout);
    }
    x = ws_x;

    bar->task_region_count = 0;
    double task_limit = w - 150.0;
    for (size_t i = 0; i < shell->window_count && x + 72.0 < task_limit; i++) {
        struct shell_window *window = &shell->windows[i];
        if (window->workspace[0] && shell->active_workspace[0] &&
                strcmp(window->workspace, shell->active_workspace) != 0)
            continue;
        const char *label = window->title[0] ? window->title : window->app_id;
        if (!label[0]) label = "Window";
        int measured = text_width(cr, label, window->focused);
        double box_width = measured + 36.0;
        if (box_width < 92.0) box_width = 92.0;
        if (box_width > 200.0) box_width = 200.0;
        if (x + box_width > task_limit) box_width = task_limit - x;
        if (box_width < 72.0) break;

        size_t region = bar->task_region_count++;
        bar->task_regions[region].x0 = x;
        bar->task_regions[region].x1 = x + box_width;
        bar->task_regions[region].window_id = window->id;
        bool hovered = bar->hovered_task == (int)region;

        if (window->focused) {
            fill_sheen(cr, x, 6, box_width, h - 12, 9, UI_ACCENT_DEEP, 0.90, 0.08);
            stroke_hairline(cr, x, 6, box_width, h - 12, 9, UI_ACCENT, 0.38);
        } else {
            rounded_rect(cr, x, 6, box_width, h - 12, 9);
            cairo_set_source_rgba(cr, 1, 1, 1, hovered ? 0.06 : 0.025);
            cairo_fill(cr);
            stroke_hairline(cr, x, 6, box_width, h - 12, 9,
                (struct ui_rgb){ 1, 1, 1 }, hovered ? 0.12 : 0.06);
        }
        draw_pip(cr, x + 13, h / 2.0, 3.0, window->focused);

        PangoLayout *layout = make_layout(cr, label, window->focused);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_width(layout, (int)(box_width - 32.0) * PANGO_SCALE);
        draw_layout_alpha(cr, layout, x + 23, layout_center_y(layout, 0, h),
            UI_TEXT, window->focused ? 0.97 : (hovered ? 0.82 : 0.60));
        g_object_unref(layout);
        x += box_width + 6.0;
    }

    char clock_text[64] = {0};
    char date_text[64] = {0};
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    strftime(clock_text, sizeof(clock_text), "%H:%M", &local);
    strftime(date_text, sizeof(date_text), "%a %d %b", &local);

    PangoLayout *clock = make_layout(cr, clock_text, true);
    PangoLayout *date = make_layout(cr, date_text, false);
    int clock_width = 0, date_width = 0;
    pango_layout_get_pixel_size(clock, &clock_width, NULL);
    pango_layout_get_pixel_size(date, &date_width, NULL);
    double clock_x = w - clock_width - 22.0;
    double date_x = clock_x - date_width - 10.0;
    bar->clock.x0 = date_x - 12;
    bar->clock.x1 = clock_x + clock_width + 10;
    double pill_w = bar->clock.x1 - bar->clock.x0;
    if (shell->quick.surface && shell->quick.bar == bar) {
        fill_sheen(cr, bar->clock.x0, 6, pill_w, h - 12, 9, UI_ACCENT_DEEP, 0.92, 0.08);
        stroke_hairline(cr, bar->clock.x0, 6, pill_w, h - 12, 9, UI_ACCENT, 0.38);
    } else {
        rounded_rect(cr, bar->clock.x0, 6, pill_w, h - 12, 9);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.03);
        cairo_fill(cr);
        stroke_hairline(cr, bar->clock.x0, 6, pill_w, h - 12, 9,
            (struct ui_rgb){ 1, 1, 1 }, 0.07);
    }
    draw_layout_alpha(cr, date, date_x, layout_center_y(date, 0, h), UI_TEXT_DIM, 0.95);
    draw_layout_alpha(cr, clock, clock_x, layout_center_y(clock, 0, h), UI_TEXT, 0.98);
    g_object_unref(date);
    g_object_unref(clock);
}

static void bar_hover(void *data, struct shell_surface *surface, double x, double y) {
    struct bar *bar = data;
    struct shell *shell = bar->shell;
    int hovered_workspace = -1;
    int hovered_task = -1;
    if (y >= 0 && y < surface->height) {
        for (size_t i = 0; i < shell->workspace_count; i++) {
            if (x >= bar->workspace_regions[i].x0 && x < bar->workspace_regions[i].x1) {
                hovered_workspace = (int)i;
                break;
            }
        }
        if (hovered_workspace < 0) {
            for (size_t i = 0; i < bar->task_region_count; i++) {
                if (x >= bar->task_regions[i].x0 && x < bar->task_regions[i].x1) {
                    hovered_task = (int)i;
                    break;
                }
            }
        }
    }
    if (hovered_workspace != bar->hovered_workspace || hovered_task != bar->hovered_task) {
        bar->hovered_workspace = hovered_workspace;
        bar->hovered_task = hovered_task;
        shell_surface_redraw(surface);
    }
}

static void bar_leave(void *data, struct shell_surface *surface) {
    struct bar *bar = data;
    if (bar->hovered_workspace >= 0 || bar->hovered_task >= 0) shell_surface_redraw(surface);
    bar->hovered_workspace = -1;
    bar->hovered_task = -1;
}

static void bar_button(void *data, struct shell_surface *surface, uint32_t button,
        bool pressed) {
    struct bar *bar = data;
    struct shell *shell = bar->shell;
    double x = shell->core.pointer_x, y = shell->core.pointer_y;
    if (!pressed || !shell->protocol || y < 0 || y >= surface->height) return;

    if (button == BTN_LEFT && x >= bar->clock.x0 && x < bar->clock.x1) {
        menu_hide(shell);
        quick_toggle(shell, bar);
        return;
    }
    if (button == BTN_LEFT) {
        for (size_t i = 0; i < shell->workspace_count; i++) {
            if (x >= bar->workspace_regions[i].x0 && x < bar->workspace_regions[i].x1) {
                shady_shell_v1_activate_workspace(shell->protocol, shell->workspaces[i]);
                shell_flush(shell);
                return;
            }
        }
    }
    if (bar->hovered_task >= 0 && (size_t)bar->hovered_task < bar->task_region_count) {
        const struct task_region *task = &bar->task_regions[bar->hovered_task];
        if (button == BTN_LEFT) {
            menu_hide(shell);
            shady_shell_v1_activate_window(shell->protocol, task->window_id);
            shell_flush(shell);
        } else if (button == BTN_RIGHT) {
            menu_show(shell, bar, task->window_id, (int)task->x0);
        }
    }
}

static void bar_closed(void *data, struct shell_surface *surface) {
    (void)surface;
    bar_destroy(data);
}

static const struct shell_surface_handler bar_handler = {
    .draw = bar_draw,
    .closed = bar_closed,
    .pointer_motion = bar_hover,
    .pointer_leave = bar_leave,
    .pointer_button = bar_button,
};

void bar_create(struct shell *shell, struct shell_output *output) {
    struct bar *bar = calloc(1, sizeof(*bar));
    if (!bar) return;
    bar->shell = shell;
    bar->output = output;
    bar->hovered_workspace = -1;
    bar->hovered_task = -1;
    const struct shell_surface_config config = {
        .name_space = "shady-shell",
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .height = BAR_HEIGHT,
        .exclusive_zone = BAR_HEIGHT,
        .keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE,
        .output = output,
    };
    bar->surface = shell_surface_create(&shell->core, &config, &bar_handler, bar);
    if (!bar->surface) {
        free(bar);
        return;
    }
    wl_list_insert(shell->bars.prev, &bar->link);
    fprintf(stderr, "shady-shell: bar on %s\n", output && output->name[0] ? output->name : "output");
}

void bar_destroy(struct bar *bar) {
    struct shell *shell = bar->shell;
    fprintf(stderr, "shady-shell: bar removed from %s\n",
        bar->output && bar->output->name[0] ? bar->output->name : "output");
    if (shell->quick.bar == bar) quick_hide(shell);
    if (shell->menu.surface && shell->menu.surface->output == bar->output) menu_hide(shell);
    shell_surface_destroy(bar->surface);
    wl_list_remove(&bar->link);
    free(bar);
}

struct bar *bar_for_output(struct shell *shell, struct shell_output *output) {
    struct bar *bar;
    wl_list_for_each(bar, &shell->bars, link)
        if (bar->output == output) return bar;
    return NULL;
}

void bars_redraw(struct shell *shell) {
    struct bar *bar;
    wl_list_for_each(bar, &shell->bars, link) shell_surface_redraw(bar->surface);
}
