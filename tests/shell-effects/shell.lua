-- tests/headless-shell-effects.sh: one bar with a surface shader and three
-- widget shaders. Boxes sit at x = 10, 80, 150 (60 wide) on a grey bar.
shell.bar {
    name = "fx-bar",
    size = 40,
    shader = shell.shader("edge.frag"),
    uniforms = { color = "#00ff00" },
    view = function()
        return shell.row {
            padding = { 0, 10 }, gap = 10, background = "#202020",
            shell.box { width = 60, height = 30, background = "#ffffff",
                shader = shell.shader("tint.frag"), uniforms = { color = "#ff00ff" } },
            shell.box { width = 60, height = 30, background = "#ffffff",
                shader = shell.shader("pulse.frag") },
            shell.box { width = 60, height = 30, background = "#ffffff",
                shader = shell.shader("broken.frag") },
            shell.spacer(),
        }
    end,
}
