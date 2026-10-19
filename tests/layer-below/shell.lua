-- tests/headless-layer-below.sh: a green full-screen wallpaper on the
-- background layer and a magenta widget on the bottom layer that logs
-- "widget" when clicked.
shell.popup {
    name = "below-wall",
    layer = "background",
    fill = true,
    persistent = true,
    view = function() return shell.box { background = "#00ff80" } end,
}
shell.popup {
    name = "below-widget",
    layer = "bottom",
    anchor = { "top", "left" },
    margin = { top = 520, left = 40 },
    width = 160, height = 80,
    persistent = true,
    view = function()
        return shell.box { width = 160, height = 80, background = "#ff00ff",
            on_click = function() shell.log("widget") end }
    end,
}
