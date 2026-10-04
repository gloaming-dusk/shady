#define _POSIX_C_SOURCE 200809L

/* The task context menu: window actions, move targets and close, opened
 * by right-clicking a task in the bar. */

#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>

#include "shady-shell-v1-client-protocol.h"
#include "shell.h"
#include "theme.h"

size_t menu_row_count(struct shell *shell) {
    return 4 + shell->workspace_count;
}

static const char *row_label(struct shell *shell,
        struct shell_window *window, size_t row, char *buffer, size_t size) {
    if (row == 0) return "Focus";
    if (row == 1) return window && window->maximized ? "Restore" : "Maximize";
    if (row == 2) return window && window->fullscreen ? "Exit fullscreen" : "Fullscreen";
    if (row < 3 + shell->workspace_count) {
        size_t ws = row - 3;
        snprintf(buffer, size, "Move to %s", shell->workspaces[ws]);
        return buffer;
    }
    return "Close";
}

static void menu_draw(void *data, struct shell_surface *surface, cairo_t *cr,
        double cw, double ch) {
    (void)surface;
    struct shell *shell = data;
    struct menu *menu = &shell->menu;
    struct shell_window *window = shell_find_window(shell, menu->window_id);
    draw_panel(cr, cw, ch, 11);

    size_t rows = menu_row_count(shell);
    for (size_t row = 0; row < rows; row++) {
        double y = row * CONTEXT_ROW_HEIGHT;
        bool hovered = menu->hovered == (int)row;
        bool destructive = row + 1 == rows;
        bool workspace_row = row >= 3 && row < 3 + shell->workspace_count;
        /* Group dividers: window actions | move targets | close. */
        if (row == 3 || destructive) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_rectangle(cr, 12, y, cw - 24, 1);
            cairo_fill(cr);
        }
        if (hovered)
            draw_row_highlight(cr, 6, y + 3, cw - 12, CONTEXT_ROW_HEIGHT - 6, 7,
                true, destructive);
        char label_buf[WORKSPACE_NAME_MAX + 32];
        const char *label = row_label(shell, window, row,
            label_buf, sizeof(label_buf));
        bool current = workspace_row && window &&
            strcmp(window->workspace, shell->workspaces[row - 3]) == 0;
        if (workspace_row)
            draw_pip(cr, 18, y + CONTEXT_ROW_HEIGHT / 2.0, 2.5, current);
        PangoLayout *layout = make_layout_sized(cr, label, hovered, 9);
        draw_layout_alpha(cr, layout, workspace_row ? 28 : 14,
            layout_center_y(layout, y, CONTEXT_ROW_HEIGHT),
            destructive ? UI_DANGER : UI_TEXT,
            hovered ? 1.0 : (destructive ? 0.85 : 0.78));
        g_object_unref(layout);
    }
}

static void menu_hover(void *data, struct shell_surface *surface, double x, double y) {
    struct shell *shell = data;
    int hovered = -1;
    if (x >= 0 && x < surface->width && y >= 0 && y < surface->height) {
        int row = (int)(y / CONTEXT_ROW_HEIGHT);
        if (row >= 0 && (size_t)row < menu_row_count(shell)) hovered = row;
    }
    if (hovered != shell->menu.hovered) {
        shell->menu.hovered = hovered;
        shell_surface_redraw(surface);
    }
}

static void menu_leave(void *data, struct shell_surface *surface) {
    struct shell *shell = data;
    if (shell->menu.hovered >= 0) shell_surface_redraw(surface);
    shell->menu.hovered = -1;
}

static void menu_button(void *data, struct shell_surface *surface, uint32_t button,
        bool pressed) {
    (void)surface;
    struct shell *shell = data;
    struct menu *menu = &shell->menu;
    if (!pressed || button != BTN_LEFT || menu->hovered < 0 || !shell->protocol) return;
    size_t row = (size_t)menu->hovered;
    size_t rows = menu_row_count(shell);
    uint32_t id = menu->window_id;
    if (row == 0)
        shady_shell_v1_activate_window(shell->protocol, id);
    else if (row == 1)
        shady_shell_v1_toggle_maximize(shell->protocol, id);
    else if (row == 2)
        shady_shell_v1_toggle_fullscreen(shell->protocol, id);
    else if (row < 3 + shell->workspace_count)
        shady_shell_v1_move_window_to_workspace(shell->protocol, id, shell->workspaces[row - 3]);
    else if (row + 1 == rows)
        shady_shell_v1_close_window(shell->protocol, id);
    shell_flush(shell);
    menu_hide(shell);
}

static void menu_closed(void *data, struct shell_surface *surface) {
    (void)surface;
    menu_hide(data);
}

static const struct shell_surface_handler menu_handler = {
    .draw = menu_draw,
    .closed = menu_closed,
    .pointer_motion = menu_hover,
    .pointer_leave = menu_leave,
    .pointer_button = menu_button,
};

void menu_show(struct shell *shell, struct bar *bar, uint32_t window_id, int x) {
    menu_hide(shell);
    struct menu *menu = &shell->menu;
    menu->window_id = window_id;
    menu->hovered = -1;
    const struct shell_surface_config config = {
        .name_space = "shady-window-menu",
        .layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
        .width = CONTEXT_WIDTH,
        .height = (uint32_t)(menu_row_count(shell) * CONTEXT_ROW_HEIGHT),
        .margin_top = BAR_HEIGHT + 4,
        .margin_left = x < 0 ? 0 : x,
        .keyboard = ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE,
        .output = bar ? bar->output : NULL,
    };
    menu->surface = shell_surface_create(&shell->core, &config, &menu_handler, shell);
}

void menu_hide(struct shell *shell) {
    struct menu *menu = &shell->menu;
    shell_surface_destroy(menu->surface);
    menu->surface = NULL;
    menu->window_id = 0;
    menu->hovered = -1;
}
