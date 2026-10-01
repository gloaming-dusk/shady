-- Night Observatory runtime layer.

shady.log("Night Observatory is awake")

-- A slightly elevated, off-axis view gives the 3D window shells some shape
-- without making normal pointer interaction feel like a game.
if shady.has_capability("spatial") then
    shady.camera("yaw", -0.20)
    shady.camera("pitch", -0.08)
    shady.camera("distance", 2.35)
    shady.camera("target_x", 0.0)
    shady.camera("target_y", 0.0)
    shady.camera("target_z", -0.72)
end

local expanded = false
local terminal = os.getenv("TERMINAL") or "foot"
local launcher = os.getenv("SHADY_LAUNCHER") or
    "if command -v fuzzel >/dev/null 2>&1; then exec fuzzel; " ..
    "elif command -v wofi >/dev/null 2>&1; then exec wofi --show drun; " ..
    "elif command -v bemenu-run >/dev/null 2>&1; then exec bemenu-run; " ..
    "else exec " .. terminal .. "; fi"

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
    local ok = shady.spawn(launcher)
    shady.log("launcher spawn: " .. tostring(ok))
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

shady.on("output.added", function(output)
    shady.log(string.format("sky opened on %s (%dx%d @ %.2f)",
        output.name, output.width, output.height, output.scale))
end)
