-- Afterglow runtime layer.

shady.log("Afterglow: the sun is low")

local workspaces = { "shore", "studio", "harbor" }
local terminal = os.getenv("TERMINAL") or "foot"

-- Open the browser where it was launched. Forcing it onto shore hides its
-- first window while the session is on main (or another workspace).
shady.rule({ app_id = "firefox", maximized = true })
shady.rule({ app_id = "org.mozilla.firefox", maximized = true })
shady.rule({ app_id = "discord", workspace = "harbor" })
shady.rule({ app_id = "vesktop", workspace = "harbor" })

if shady.has_capability("spatial") then
    -- Frosted glass: the bar and the shell's panels show the sunset behind
    -- them, softened (see shell.lua, which keeps them translucent).
    shady.layer_effect("shady-shell", { blur = 18, saturation = 1.25, tint = "#1a0e1a30" })
    for _, panel in ipairs({ "quick", "menu", "launcher" }) do
        shady.layer_effect(panel, { blur = 26, saturation = 1.2 })
    end

    -- The camera stays at its default: windows face it squarely and at
    -- their real size, one screen pixel per pixel. Pulled in and turned,
    -- it made every window about 1.4 times too large and a little skewed,
    -- too big for the screen and soft. The sky and sea need no help.
end

shady.bind("Super+Shift+Escape", function() shady.quit() end)
shady.bind("Super+Return", function() shady.spawn(terminal) end, "Open a terminal")
shady.bind("Super+d", function() shady.toggle_launcher() end, "Open the launcher")
shady.bind("Print", function() shady.screenshot() end, "Save a screenshot to ~/Pictures")

shady.describe("Super+1…" .. #workspaces, "Go to workspace")
shady.describe("Super+Shift+1…" .. #workspaces, "Move window to workspace")
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
end, "Maximize the window")
shady.bind("Super+Shift+m", function()
    local window = shady.focused_window()
    if window then window:set_fullscreen() end
end, "Make the window fullscreen")
shady.bind("Super+Space", function() shady.expand_all() end, "Unfold windows")
shady.bind("Super+Shift+Space", function() shady.fold_all() end, "Fold windows into cubes")

shady.on("window.mapped", function(window)
    shady.log("Afterglow: " .. (window.title ~= "" and window.title or window.app_id) ..
        " drifts in")
end)
