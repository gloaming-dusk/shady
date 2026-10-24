-- Tide runtime layer: the keys (niri's, so the hands need not relearn),
-- window rules, idle and the hardware keys. The shell (shell.lua) owns the
-- system state: volume, brightness, the lock screen and so on; keys reach it
-- as published values.

local terminal = os.getenv("TERMINAL") or "ghostty"
local L = shady.layout

-- Whether a program is on PATH. (Not os.execute: the process reaps its
-- children itself, so system() never sees the status.)
local function has(command)
    for dir in (os.getenv("PATH") or ""):gmatch("[^:]+") do
        local f = io.open(dir .. "/" .. command, "r")
        if f then f:close() return true end
    end
    return false
end

-- ---- talking to the shell ----------------------------------------------
-- `tide.do` carries an action and a counter, so pressing the same key twice
-- is still a change the shell hears about.

local serial = 0
local function shell_do(action)
    serial = serial + 1
    shady.publish("tide.do", action .. " " .. serial)
end

local function lock() shell_do("lock") end

-- ---- windows -------------------------------------------------------------

local function focused() return shady.focused_window() end

shady.bind("Super+Return", function() shady.spawn(terminal) end, "Open a terminal")
shady.bind("Super+q", function()
    local w = focused()
    if w then w:close() end
end, "Close the window")

local function key(keys, fn, description, group)
    shady.bind(keys, fn, description and { description = description, group = group or "Windows" } or nil)
end

-- Focus and move, with arrows or hjkl.
for _, k in ipairs({ { "Left", "h", "left" }, { "Right", "l", "right" },
        { "Up", "k", "up" }, { "Down", "j", "down" } }) do
    local arrow, vim, dir = k[1], k[2], k[3]
    key("Super+" .. arrow, function() L.focus(dir) end)
    key("Super+" .. vim, function() L.focus(dir) end)
    key("Super+Ctrl+" .. arrow, function() L.move(dir) end)
    key("Super+Ctrl+" .. vim, function() L.move(dir) end)
end
shady.describe("Super+←↓↑→ / hjkl", "Focus the column or window there", "Windows")
shady.describe("Super+Ctrl+←↓↑→ / hjkl", "Move the column or window there", "Windows")
key("Super+Home", function() L.focus("first") end, "Focus the first column")
key("Super+End", function() L.focus("last") end, "Focus the last column")
key("Super+Ctrl+Home", function() L.move("first") end)
key("Super+Ctrl+End", function() L.move("last") end)

key("Super+bracketleft", function() L.consume_or_expel("left") end, "Join or leave the column on the left")
key("Super+bracketright", function() L.consume_or_expel("right") end, "Join or leave the column on the right")
key("Super+Shift+comma", function() L.consume_or_expel("left") end)
key("Super+period", function() L.consume_or_expel("right") end)

key("Super+r", function() L.cycle_width(true) end, "Next preset width")
key("Super+Shift+r", function() L.cycle_width(false) end)
key("Super+Ctrl+r", function() L.reset_height() end, "Reset window heights")
key("Super+minus", function() L.set_width("-10%") end, "Narrower column")
key("Super+equal", function() L.set_width("+10%") end, "Wider column")
key("Super+Shift+minus", function() L.set_height("-10%") end)
key("Super+Shift+equal", function() L.set_height("+10%") end)
key("Super+f", function() L.full_width() end, "Full-width column")
key("Super+Ctrl+f", function() L.full_width() end)
key("Super+m", function() L.full_width() end)
key("Super+Shift+f", function()
    local w = focused()
    if w then w:set_fullscreen() end
end, "Fullscreen")
key("Super+c", function() L.center() end, "Centre the column")
key("Super+v", function() L.toggle_floating() end, "Float or tile the window")
key("Super+o", function() L.overview() end, "Overview of the strip")

-- ---- workspaces: numbered, made as they are needed ------------------------

shady.workspace("1")
for i = 1, 9 do
    local name = tostring(i)
    key("Super+" .. i, function() shady.workspace(name) end)
    key("Super+Ctrl+" .. i, function()
        local w = focused()
        if w then w:move_to_workspace(name) end
    end)
    key("Super+Shift+" .. i, function()
        local w = focused()
        if w and w:move_to_workspace(name) then
            shady.workspace(name)
            w:focus()
        end
    end)
end
shady.describe("Super+1…9", "Go to workspace", "Workspaces")
shady.describe("Super+Ctrl+1…9", "Send the window to workspace", "Workspaces")
key("Super+Page_Down", function() L.workspace(1) end, "Workspace below", "Workspaces")
key("Super+Page_Up", function() L.workspace(-1) end, "Workspace above", "Workspaces")
key("Super+u", function() L.workspace(1) end)
key("Super+i", function() L.workspace(-1) end)
key("Super+Ctrl+Page_Down", function() L.workspace(1, true) end, "Take the window down", "Workspaces")
key("Super+Ctrl+Page_Up", function() L.workspace(-1, true) end, "Take the window up", "Workspaces")
key("Super+Ctrl+u", function() L.workspace(1, true) end)
key("Super+Ctrl+i", function() L.workspace(-1, true) end)
shady.describe("Super+wheel", "Workspaces (Shift: columns)", "Workspaces")
shady.describe("Three-finger swipe", "Scroll the strip, or change workspace", "Workspaces")

-- ---- shell ---------------------------------------------------------------

key("Super+space", function() shady.toggle_launcher() end, "Launcher", "Shell")
key("Super+d", function() shady.toggle_launcher() end)
key("Super+s", function() shell_do("control") end, "Control center", "Shell")
key("Super+n", function() shell_do("notifications") end, "Notifications", "Shell")
key("Super+comma", function() shell_do("control") end)
key("Super+Shift+e", function() shell_do("power") end, "Power menu", "Shell")
key("Ctrl+Alt+Delete", function() shell_do("power") end)
key("Super+Alt+l", lock, "Lock the screen", "Shell")
key("Super+Shift+p", function() shady.output_power(false) end, "Turn the screen off", "Shell")
key("Super+Shift+Escape", function() shady.quit() end)

-- Hardware keys: the shell changes the value and shows it.
for keysym, action in pairs({
    XF86AudioRaiseVolume = "volume +5", XF86AudioLowerVolume = "volume -5",
    XF86AudioMute = "mute", XF86AudioMicMute = "mic-mute",
    XF86MonBrightnessUp = "brightness +5", XF86MonBrightnessDown = "brightness -5",
    XF86AudioPlay = "media play-pause", XF86AudioPause = "media play-pause",
    XF86AudioNext = "media next", XF86AudioPrev = "media previous",
}) do
    shady.bind(keysym, function() shell_do(action) end)
end

-- ---- screenshots -----------------------------------------------------------

local shots = (os.getenv("XDG_PICTURES_DIR") or ((os.getenv("HOME") or "") .. "/Pictures")) .. "/Screenshots"
local function shot_path()
    return string.format("%s/Screenshot from %s.png", shots, os.date("%Y-%m-%d %H-%M-%S"))
end
local function grim(geometry)
    local path = shot_path()
    local cmd = string.format("mkdir -p %q && grim %s %q && (wl-copy < %q || true) && " ..
        "notify-send -a Screenshot 'Screenshot saved' %q 2>/dev/null",
        shots, geometry and ("-g " .. geometry) or "", path, path, path)
    shady.spawn(cmd)
end
key("Print", function()
    if has("grim") and has("slurp") then
        local path = shot_path()
        shady.spawn(string.format("mkdir -p %q && g=$(slurp) && grim -g \"$g\" %q && " ..
            "(wl-copy < %q || true) && notify-send -a Screenshot 'Screenshot saved' %q 2>/dev/null",
            shots, path, path, path))
    else
        shady.screenshot()
    end
end, "Screenshot of a region", "Shell")
key("Ctrl+Print", function()
    if has("grim") then grim(nil) else shady.screenshot() end
end, "Screenshot of the screen", "Shell")
key("Alt+Print", function()
    local w = focused()
    if w and has("grim") then
        grim(string.format("'%d,%d %dx%d'", w.x, w.y, w.width, w.height))
    else
        shady.screenshot()
    end
end, "Screenshot of the window", "Shell")

-- ---- rules -----------------------------------------------------------------

for _, app in ipairs({ "pavucontrol", "org.pulseaudio.pavucontrol", "blueman-manager",
        "nm-connection-editor", "org.gnome.Calculator", "xdg-desktop-portal-gtk",
        "polkit-gnome-authentication-agent-1", "dev.noctalia.Noctalia" }) do
    shady.rule({ app_id = app, floating = true })
end
shady.rule({ title = "Picture-in-Picture", floating = true })
shady.rule({ title = "Picture in picture", floating = true })

-- ---- idle --------------------------------------------------------------------
-- Lock after five minutes without input, turn the screen off a little later.
-- A playing video holds both back (idle-inhibit).

local idle_lock = tonumber(os.getenv("TIDE_IDLE_LOCK") or "") or 300
local idle_off = tonumber(os.getenv("TIDE_IDLE_SCREEN_OFF") or "") or 330
if idle_lock > 0 then shady.idle(idle_lock, lock) end
if idle_off > 0 then
    shady.idle(idle_off, function() shady.output_power(false) end,
        function() shady.output_power(true) end)
end

-- Caffeine (the control center's toggle presses this key): stay awake.
local caffeine = false
shady.publish("tide.caffeine", "0")
shady.bind("Super+Ctrl+Shift+F12", function()
    caffeine = not caffeine
    shady.idle_inhibit(caffeine)
    shady.publish("tide.caffeine", caffeine and "1" or "0")
end)

-- Frosted glass is redrawn on every frame the screen changes, so only the
-- panels that open briefly over the windows get it. The bar lies over the
-- wallpaper, where blur shows nothing, and the control center animates its
-- sliders: both use more opaque glass instead (shell.lua).
if shady.has_capability("spatial") then
    for _, name in ipairs({ "tide-launcher", "tide-power", "tide-calendar", "tide-shortcuts" }) do
        shady.layer_effect(name, { blur = 26, saturation = 1.2, tint = "#0c0c1430" })
    end
end
