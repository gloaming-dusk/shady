-- tests/headless-layer-transform.sh: a bar whose left 200 px are magenta
-- and log "clicked"; the rest is nearly transparent (so a backdrop effect
-- shows through) and logs "bar". A small overlay-layer popup at x 300-360,
-- y 0-20 sits above it and logs "over".
shell.bar {
    name = "tf-bar", size = 40,
    view = function()
        return shell.row {
            background = "#00000010",
            on_click = function() shell.log("bar") end,
            shell.box { id = "hit", width = 200, height = 40, background = "#ff00ff",
                on_click = function() shell.log("clicked") end },
            shell.spacer(),
        }
    end,
}
shell.popup {
    name = "over",
    layer = "overlay",
    anchor = { "top", "left" },
    margin = { left = 300 },
    width = 60, height = 20,
    view = function()
        return shell.box { width = 60, height = 20, background = "#ffff00",
            on_click = function() shell.log("over") end }
    end,
}
shell.open("over")
