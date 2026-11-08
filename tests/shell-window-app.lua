-- The application tests/headless-shell-window.sh runs with shady-shell --app.
local typed, width = "", 0
shell.window {
    name = "main",
    title = "Window test",
    app_id = "window-test",
    width = 400,
    height = 300,
    view = function(ctx)
        width = ctx.width
        return shell.box {
            width = ctx.width, height = ctx.height, background = "#223344", id = "all",
            on_click = function()
                shell.compositor_lua(string.format('shady.log("window-test: clicked at width %d")', width))
            end,
        }
    end,
    on_key = function(key, utf8_text)
        typed = typed .. utf8_text
        shell.compositor_lua(string.format('shady.log("window-test: typed %s")', typed))
    end,
}
shell.after(0, function() shell.open("main") end)
