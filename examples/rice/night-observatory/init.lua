-- Night Observatory runtime layer.

shady.log("Night Observatory is awake")

-- Window rules are evaluated before the new window is focused. Keep these
-- deliberately light so the rice remains useful even without these apps.
shady.rule({ app_id = "firefox", workspace = "web", maximized = true })
shady.rule({ app_id = "org.mozilla.firefox", workspace = "web", maximized = true })

-- A slightly elevated, off-axis view gives the 3D window shells some shape
-- without making normal pointer interaction feel like a game.
if shady.has_capability("spatial") then
    shady.camera("yaw", -0.06)
    shady.camera("pitch", -0.035)
    shady.camera("distance", 1.55)
    shady.camera("target_x", 0.0)
    shady.camera("target_y", 0.0)
    shady.camera("target_z", -0.64)
end

local expanded = false
local workspace_names = { "main", "code", "web" }
local terminal = os.getenv("TERMINAL") or "foot"
local function describe_desktop()
    local windows = shady.windows()
    local outputs = shady.outputs()
    shady.log(string.format("observatory: %d window(s), %d output(s)",
        #windows, #outputs))

    for i, window in ipairs(windows) do
        shady.log(string.format("  star %02d  %-18s  z=%s",
            i,
            window.app_id ~= "" and window.app_id or window.title,
            window.z and string.format("%.2f", window.z) or "2d"))
    end
end

-- There is always a way back into the desktop, even when no windows exist.
shady.bind("Super+Return", function()
    local ok = shady.spawn(terminal)
    shady.log("terminal spawn: " .. tostring(ok))
end)

shady.bind("Super+d", function()
    shady.toggle_launcher()
end)

for i, name in ipairs(workspace_names) do
    shady.bind("Super+" .. i, function()
        shady.workspace(name)
        shady.log("workspace: " .. name)
    end)

    shady.bind("Super+Shift+" .. i, function()
        local window = shady.focused_window()
        if window and window:move_to_workspace(name) then
            -- Night Observatory uses move-and-follow semantics: sending a
            -- focused window to another workspace also takes you there.
            shady.workspace(name)
            window:focus()
            shady.log("moved focus and followed to workspace: " .. name)
        end
    end)
end

shady.bind("Super+Ctrl+m", function()
    local window = shady.focused_window()
    if window then window:maximize() end
end)

shady.bind("Super+Shift+m", function()
    local window = shady.focused_window()
    if window then window:set_fullscreen() end
end)

-- "Overview": unfold every cube so the whole constellation becomes readable.
shady.bind("Super+Space", function()
    expanded = not expanded
    if expanded then
        shady.expand_all()
        shady.log("overview: expanded")
    else
        shady.fold_all()
        shady.log("overview: folded")
    end
end)

-- Physics is a toy here, not the default interaction. Turn it on deliberately.
shady.bind("Super+Shift+g", function()
    shady.toggle_gravity()
    shady.log("gravity toggled")
end)

shady.bind("Super+r", function()
    shady.respawn_all()
    shady.log("windows returned to the observatory")
end)

shady.bind("Super+i", describe_desktop)

-- Great while tweaking orbit_layout.c: rebuild the .so, then reload without
-- restarting the compositor.
shady.bind("Super+Shift+r", function()
    local ok = shady.reload_plugin("orbit-layout")
    shady.log("orbit-layout reload: " .. tostring(ok))
end)

shady.bind("Super+Shift+q", function()
    shady.quit()
end)

shady.on("window.mapped", function(window)
    shady.log("new star: " ..
        (window.app_id ~= "" and window.app_id or window.title))
end)

shady.on("window.focused", function(window)
    shady.log("focus: " ..
        (window.app_id ~= "" and window.app_id or window.title))
end)

shady.on("workspace.changed", function(name)
    shady.log("workspace changed: " .. name)
end)

shady.on("output.added", function(output)
    shady.log(string.format("sky opened on %s (%dx%d @ %.2f)",
        output.name, output.width, output.height, output.scale))
end)
