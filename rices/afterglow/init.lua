-- Afterglow runtime layer.

shady.log("Afterglow: the sun is low")

local workspaces = { "shore", "studio", "harbor" }
local terminal = os.getenv("TERMINAL") or "foot"

shady.rule({ app_id = "firefox", workspace = "shore", maximized = true })
shady.rule({ app_id = "org.mozilla.firefox", workspace = "shore", maximized = true })
shady.rule({ app_id = "discord", workspace = "harbor" })
shady.rule({ app_id = "vesktop", workspace = "harbor" })

if shady.has_capability("spatial") then
    -- Frosted glass: the bar and the shell's panels show the sunset behind
    -- them, softened (see shell.lua, which keeps them translucent).
    shady.layer_effect("shady-shell", { blur = 18, saturation = 1.25, tint = "#1a0e1a30" })
    for _, panel in ipairs({ "quick", "menu", "launcher" }) do
        shady.layer_effect(panel, { blur = 26, saturation = 1.2 })
    end

    -- Standing a little above the water, looking slightly up: the horizon
    -- sits in the lower third so the sky gets the stage, and windows hang
    -- in front of the sun like panes against a window seat.
    shady.camera("yaw", -0.05)
    shady.camera("pitch", -0.07)
    shady.camera("distance", 1.30)
    shady.camera("target_x", 0.0)
    shady.camera("target_y", -0.02)
    shady.camera("target_z", -0.55)
end

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
shady.bind("Super+Space", function() shady.expand_all() end)
shady.bind("Super+Shift+Space", function() shady.fold_all() end)

shady.on("window.mapped", function(window)
    shady.log("Afterglow: " .. (window.title ~= "" and window.title or window.app_id) ..
        " drifts in")
end)
