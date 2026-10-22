-- Neon Transit runtime layer.

shady.log("Neon Transit online")

local workspaces = { "main", "code", "comms" }
local terminal = os.getenv("TERMINAL") or "foot"

-- Keep common communication apps out of the coding lane.
shady.rule({ app_id = "firefox", workspace = "main", maximized = true })
shady.rule({ app_id = "org.mozilla.firefox", workspace = "main", maximized = true })
shady.rule({ app_id = "discord", workspace = "comms" })
shady.rule({ app_id = "vesktop", workspace = "comms" })

-- The camera stays at its default, so windows are drawn square-on at their
-- real size; pulled in and turned it enlarged them about 1.4 times.

shady.bind("Super+Shift+Escape", function()
    shady.quit()
end)

shady.bind("Super+Return", function()
    shady.spawn(terminal)
end, "Open a terminal")

shady.bind("Super+d", function()
    shady.toggle_launcher()
end, "Open the launcher")

shady.describe("Super+1…" .. #workspaces, "Go to workspace")
shady.describe("Super+Shift+1…" .. #workspaces, "Move window to workspace")
for i, name in ipairs(workspaces) do
    shady.bind("Super+" .. i, function()
        shady.workspace(name)
    end)

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
end, "Maximize the window")

shady.bind("Super+Shift+m", function()
    local window = shady.focused_window()
    if window then window:set_fullscreen() end
end, "Make the window fullscreen")

shady.bind("Super+Space", function()
    shady.expand_all()
end, "Unfold windows")

shady.bind("Super+Shift+Space", function()
    shady.fold_all()
end, "Fold windows into cubes")

shady.bind("Super+r", function()
    shady.respawn_all()
end, "Bring back every window")

shady.bind("Super+i", function()
    shady.log(string.format(
        "transit status: workspace=%s windows=%d outputs=%d",
        shady.current_workspace(), #shady.windows(), #shady.outputs()))
end, "Log the transit status")

shady.on("window.mapped", function(window)
    shady.log("arrival: " .. (window.title ~= "" and window.title or window.app_id))
end)

shady.on("window.focused", function(window)
    shady.log("platform focus: " .. (window.title ~= "" and window.title or window.app_id))
end)

shady.on("workspace.changed", function(name)
    shady.log("line changed: " .. name)
end)
