#define _POSIX_C_SOURCE 200809L

/* Quick Settings: launcher, window cycling, workspaces and quit, opened
 * from the clock on a bar. */

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

#include "shady-shell-v1-client-protocol.h"
#include "shell.h"
#include "theme.h"

size_t quick_row_count(struct shell *shell) {
    return 3 + shell->workspace_count;
}

static const char *row_label(struct shell *shell, size_t row,
        char *buffer, size_t size) {
    if (row == 0) return "Launcher";
    if (row == 1) return "Next window";
    if (row < 2 + shell->workspace_count) {
        /* The active workspace is marked with a pip, not text. */
        snprintf(buffer, size, "Workspace  %s", shell->workspaces[row - 2]);
        return buffer;
    }
    return "Quit Shady";
}

static void quick_draw(void *data, struct shell_surface *surface, cairo_t *cr,
        double qw, double qh) {
    (void)surface;
    struct shell *shell = data;
    struct quick *quick = &shell->quick;
    draw_panel(cr, qw, qh, 13);
    PangoLayout *title = make_layout_sized(cr, "Quick settings", true, 10);
    draw_layout_alpha(cr, title, 16, layout_center_y(title, 0, 34), UI_TEXT, 0.96);
    g_object_unref(title);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
    cairo_rectangle(cr, 12, 33, qw - 24, 1);
    cairo_fill(cr);

    size_t rows = quick_row_count(shell);
    for (size_t row = 0; row < rows; row++) {
        double y = 34 + row * QUICK_ROW_HEIGHT;
        bool hovered = quick->hovered == (int)row;
        bool destructive = row + 1 == rows;
        bool workspace_row = row >= 2 && row < 2 + shell->workspace_count;
        if (destructive) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_rectangle(cr, 12, y + 1, qw - 24, 1);
            cairo_fill(cr);
        }
        if (hovered)
            draw_row_highlight(cr, 7, y + 3, qw - 14, QUICK_ROW_HEIGHT - 6, 8,
                true, destructive);
        char label_buf[WORKSPACE_NAME_MAX + 32];
        const char *label = row_label(shell, row,
            label_buf, sizeof(label_buf));
        bool active = workspace_row &&
            strcmp(shell->workspaces[row - 2], shell->active_workspace) == 0;
        if (workspace_row)
            draw_pip(cr, 21, y + QUICK_ROW_HEIGHT / 2.0, 2.8, active);
        PangoLayout *layout = make_layout_sized(cr, label, hovered || active, 9);
        draw_layout_alpha(cr, layout, workspace_row ? 32 : 16,
            layout_center_y(layout, y, QUICK_ROW_HEIGHT),
            destructive ? UI_DANGER : UI_TEXT,
            hovered || active ? 1.0 : (destructive ? 0.85 : 0.78));
        g_object_unref(layout);
    }
}

static void quick_hover(void *data, struct shell_surface *surface, double x, double y) {
    struct shell *shell = data;
    int hovered = -1;
    if (x >= 0 && x < surface->width && y >= 34 && y < surface->height) {
        int row = (int)((y - 34) / QUICK_ROW_HEIGHT);
        if (row >= 0 && (size_t)row < quick_row_count(shell)) hovered = row;
    }
    if (hovered != shell->quick.hovered) {
        shell->quick.hovered = hovered;
        shell_surface_redraw(surface);
    }
}

static void quick_leave(void *data, struct shell_surface *surface) {
    struct shell *shell = data;
    if (shell->quick.hovered >= 0) shell_surface_redraw(surface);
    shell->quick.hovered = -1;
}

static void quick_button(void *data, struct shell_surface *surface, uint32_t button,
        bool pressed) {
    (void)surface;
    struct shell *shell = data;
    if (!pressed || button != BTN_LEFT || shell->quick.hovered < 0 || !shell->protocol) return;
    size_t row = (size_t)shell->quick.hovered;
    size_t rows = quick_row_count(shell);
    if (row == 0) {
        quick_hide(shell);
        launcher_toggle(shell);
    } else if (row == 1) {
        shady_shell_v1_cycle_window(shell->protocol);
        shell_flush(shell);
        quick_hide(shell);
    } else if (row < 2 + shell->workspace_count) {
        shady_shell_v1_activate_workspace(shell->protocol, shell->workspaces[row - 2]);
        shell_flush(shell);
        quick_hide(shell);
    } else if (row + 1 == rows) {
        shady_shell_v1_terminate(shell->protocol);
        shell_flush(shell);
    }
}

static void quick_closed(void *data, struct shell_surface *surface) {
    (void)surface;
    quick_hide(data);
}

static const struct shell_surface_handler quick_handler = {
    .draw = quick_draw,
    .closed = quick_closed,
    .pointer_motion = quick_hover,
    .pointer_leave = quick_leave,
    .pointer_button = quick_button,
};

void quick_show(struct shell *shell, struct bar *bar) {
    struct quick *quick = &shell->quick;
    if (quick->surface) return;
    menu_hide(shell);
    quick->hovered = -1;
    quick->bar = bar;
    const struct shell_surface_config config = {
        .name_space = "shady-quick-settings",
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
        .width = QUICK_WIDTH,
        .height = (uint32_t)(34 + quick_row_count(shell) * QUICK_ROW_HEIGHT + 6),
        .margin_top = BAR_HEIGHT + 4,
        .margin_right = 8,
        .keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE,
        .output = bar ? bar->output : NULL,
    };
    quick->surface = shell_surface_create(&shell->core, &config, &quick_handler, shell);
    if (!quick->surface) quick->bar = NULL;
    bars_redraw(shell); /* the clock pill shows Quick Settings is open */
}

void quick_hide(struct shell *shell) {
    struct quick *quick = &shell->quick;
    if (!quick->surface) return;
    shell_surface_destroy(quick->surface);
    quick->surface = NULL;
    quick->bar = NULL;
    quick->hovered = -1;
    bars_redraw(shell);
}

void quick_toggle(struct shell *shell, struct bar *bar) {
    if (shell->quick.surface) quick_hide(shell);
    else quick_show(shell, bar);
}
