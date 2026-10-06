-- Neon Transit runtime layer.

shady.log("Neon Transit online")

local workspaces = { "main", "code", "comms" }
local terminal = os.getenv("TERMINAL") or "foot"

-- Keep common communication apps out of the coding lane.
shady.rule({ app_id = "firefox", workspace = "main", maximized = true })
shady.rule({ app_id = "org.mozilla.firefox", workspace = "main", maximized = true })
shady.rule({ app_id = "discord", workspace = "comms" })
shady.rule({ app_id = "vesktop", workspace = "comms" })

if shady.has_capability("spatial") then
    -- Slightly closer, flatter and more head-on than Night Observatory.
    shady.camera("yaw", 0.035)
    shady.camera("pitch", -0.018)
    shady.camera("distance", 1.34)
    shady.camera("target_x", 0.0)
    shady.camera("target_y", -0.015)
    shady.camera("target_z", -0.58)
end

shady.bind("Super+Shift+Escape", function()
    shady.quit()
end)

shady.bind("Super+Return", function()
    shady.spawn(terminal)
end)

shady.bind("Super+d", function()
    shady.toggle_launcher()
end)

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
end)

shady.bind("Super+Shift+m", function()
    local window = shady.focused_window()
    if window then window:set_fullscreen() end
end)

shady.bind("Super+Space", function()
    shady.expand_all()
end)

shady.bind("Super+Shift+Space", function()
    shady.fold_all()
end)

shady.bind("Super+r", function()
    shady.respawn_all()
end)

shady.bind("Super+i", function()
    shady.log(string.format(
        "transit status: workspace=%s windows=%d outputs=%d",
        shady.current_workspace(), #shady.windows(), #shady.outputs()))
end)

shady.on("window.mapped", function(window)
    shady.log("arrival: " .. (window.title ~= "" and window.title or window.app_id))
end)

shady.on("window.focused", function(window)
    shady.log("platform focus: " .. (window.title ~= "" and window.title or window.app_id))
end)

shady.on("workspace.changed", function(name)
    shady.log("line changed: " .. name)
end)
