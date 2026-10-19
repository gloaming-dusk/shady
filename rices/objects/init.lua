-- Objects runtime layer: workspaces and key bindings.

shady.log("Objects: cards on the table")

local workspaces = { "main", "work", "play" }
local terminal = os.getenv("TERMINAL") or "foot"

-- "main" exists from the start; add the others after it and come back.
for i = 2, #workspaces do shady.workspace(workspaces[i]) end
shady.workspace(workspaces[1])

shady.bind("Super+Shift+Escape", function() shady.quit() end)
shady.bind("Super+Return", function() shady.spawn(terminal) end)
shady.bind("Super+d", function() shady.toggle_launcher() end)

for i, name in ipairs(workspaces) do
    shady.bind("Super+" .. i, function() shady.workspace(name) end)
    shady.bind("Super+Shift+" .. i, function()
        local window = shady.focused_window()
        if window and window:move_to_workspace(name) then
            shady.workspace(name)
            window:focus()
        end
    end)
end

shady.bind("Super+m", function()
    local window = shady.focused_window()
    if window then window:maximize() end
end)
shady.bind("Super+Shift+m", function()
    local window = shady.focused_window()
    if window then window:set_fullscreen() end
end)
