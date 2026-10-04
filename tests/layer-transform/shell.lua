-- tests/headless-layer-transform.sh: a magenta bar whose left 200 px log
-- each click.
shell.bar {
    name = "tf-bar", size = 40,
    view = function()
        return shell.row {
            background = "#ff00ff",
            shell.box { id = "hit", width = 200, height = 40,
                on_click = function() shell.log("clicked") end },
            shell.spacer(),
        }
    end,
}
