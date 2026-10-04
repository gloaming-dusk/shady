-- tests/headless-published-values.sh: log compositor values as they change.
local seen = {}
local function note(key)
    local v = shell.compositor_value(key, "<none>")
    if seen[key] ~= v then
        seen[key] = v
        shell.log(key .. "=" .. v)
    end
end
shell.bar {
    name = "values-bar", size = 30,
    view = function()
        note("probe.greeting")
        note("test.hour")
        return shell.row { background = "#202020", shell.text(shell.compositor_value("test.hour", "")) }
    end,
}
