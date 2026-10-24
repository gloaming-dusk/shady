-- Tide: shady-shell for everyday use.
--
-- A bar of three glass islands floats over the top edge: the workspaces,
-- with a live minimap of the focused one's strip of columns; the clock; and
-- the system's state. Super+S opens the control center (sliders, toggles,
-- media, notifications), Super+Space the launcher (apps, a calculator,
-- session commands), Super+Shift+E the power menu. Volume and brightness
-- keys show an OSD. The compositor (init.lua) sends keys as `tide.do`.

-- Files next to this one, also when another config dofile()s it.
local HERE = (debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$")) or "."
shell.fonts(HERE .. "/../horizon/fonts")

local a, mix = shell.alpha, shell.mix
local t = shell.theme
local SANS = "Manrope"
local function font(weight, size) return SANS .. " " .. weight .. " " .. size end

local P = {
    text = t.text, dim = t.text_dim, accent = t.accent, accent_2 = t.accent_2,
    deep = t.accent_deep, surface = t.surface, danger = t.danger,
    base = "#11111B", good = "#8BD5A8", warn = "#F2C27B",
}
local WHITE = "#FFFFFF"
local BAR, ISLAND = 44, 32
-- Springs for values the user drives (sliders, OSD): quick enough to read
-- as following the hand, short so few frames are drawn.
local QUICK = { stiffness = 1600, damping = 80 }
local POP = { opacity = 0, scale = 0.97, translate = { 0, -8 } }

local function text(s, props)
    props = props or {}
    props.color = props.color or P.text
    props.font = props.font or font("Medium", 9.5)
    return shell.text(s, props)
end

local function trim(s) return ((s or ""):gsub("^%s+", ""):gsub("%s+$", "")) end

local function glass(alpha)
    return shell.gradient("vertical", a(mix(P.surface, WHITE, 0.035), alpha), a(P.surface, alpha))
end

-- The card every panel sits on.
local function card(props)
    props.radius = props.radius or 18
    props.align = props.align or "stretch"
    props.background = props.background or glass(0.86)
    props.border = props.border or a(P.text, 0.08)
    props.animate = props.animate or "snappy"
    props.enter, props.exit = props.enter or POP, props.exit or POP
    return shell.column(props)
end

-- Whether a program is on PATH. (Not os.execute: the process reaps its
-- children itself, so system() never sees the status.)
local function has(command)
    for dir in (os.getenv("PATH") or ""):gmatch("[^:]+") do
        local f = io.open(dir .. "/" .. command, "r")
        if f then f:close() return true end
    end
    return false
end
local HAVE = {
    playerctl = has("playerctl"), nmcli = has("nmcli"), bluetoothctl = has("bluetoothctl"),
    powerprofiles = has("powerprofilesctl"), wlcopy = has("wl-copy"), swaylock = has("swaylock"),
}

-- ---- system state ----------------------------------------------------------
-- Polled with plain tools. A value the shell itself just changed wins over
-- the poll for a few seconds, so sliders do not jump back.

local override = {}
local function remember(k, v) override[k] = { value = v, at = os.time() } end
local function live(k, polled)
    local o = override[k]
    if o and os.time() - o.at < 3 then return o.value end
    return polled
end

local sys = {}

function sys.volume()
    local out = shell.poll("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", 4000)
    local v = tonumber(out:match("Volume:%s*([%d%.]+)") or "")
    return live("volume", v and math.floor(v * 100 + 0.5)), live("mute", out:find("MUTED") ~= nil)
end

function sys.mic_muted()
    local out = shell.poll("wpctl get-volume @DEFAULT_AUDIO_SOURCE@ 2>/dev/null", 8000)
    return live("mic", out:find("MUTED") ~= nil)
end

local brightness_cmd = string.format("sh %q", shell.data_dir .. "/bin/shady-brightness")
function sys.brightness()
    return live("brightness", tonumber(shell.poll(brightness_cmd .. " get 2>/dev/null", 8000)))
end

function sys.battery()
    local out = shell.poll("cat /sys/class/power_supply/BAT*/capacity /sys/class/power_supply/BAT*/status 2>/dev/null | head -n2 | tr '\\n' ' '", 20000)
    local level, status = out:match("(%d+)%s+(%a+)")
    if not level then return nil end
    return tonumber(level), status
end

function sys.battery_time()
    local out = shell.poll("upower -i $(upower -e 2>/dev/null | grep -m1 BAT) 2>/dev/null | grep -E 'time to (empty|full)' | head -n1", 60000)
    local what, n, unit = out:match("time to (%a+):%s*([%d%.]+)%s*(%a+)")
    if not what then return nil end
    return string.format("%s %s until %s", n, unit, what == "empty" and "empty" or "full")
end

function sys.wifi()
    if not HAVE.nmcli then return nil end
    local state = trim(shell.poll("nmcli -t -f WIFI general 2>/dev/null", 10000))
    local ssid = trim(shell.poll("nmcli -t -f active,ssid device wifi 2>/dev/null | sed -n 's/^yes://p' | head -n1", 10000))
    local wired = trim(shell.poll("nmcli -t -f TYPE,STATE device 2>/dev/null | grep -m1 '^ethernet:connected'", 15000))
    return live("wifi", state == "enabled"), ssid, wired ~= ""
end

function sys.bluetooth()
    if not HAVE.bluetoothctl then return nil end
    local out = shell.poll("bluetoothctl show 2>/dev/null | grep -m1 Powered", 10000)
    if out == "" then return nil end
    local connected = trim(shell.poll("bluetoothctl devices Connected 2>/dev/null | head -n1 | cut -d' ' -f3-", 10000))
    return live("bluetooth", out:find("yes") ~= nil), connected
end

function sys.profile()
    if not HAVE.powerprofiles then return nil end
    return live("profile", trim(shell.poll("powerprofilesctl get 2>/dev/null", 15000)))
end

function sys.media()
    if not HAVE.playerctl then return nil end
    local out = shell.poll("playerctl metadata --format '{{status}}\t{{title}}\t{{artist}}' 2>/dev/null", 2500)
    local status, title, artist = out:match("^(%a+)\t([^\t]*)\t([^\n]*)")
    if not status or status == "Stopped" then return nil end
    return { playing = live("playing", status == "Playing"), title = title, artist = trim(artist) }
end

function sys.uptime()
    local s = tonumber((shell.poll("cut -d' ' -f1 /proc/uptime", 60000)):match("^[%d]+") or "")
    if not s then return "" end
    local h, m = s // 3600, (s % 3600) // 60
    return h > 0 and string.format("up %dh %dm", h, m) or string.format("up %d min", m)
end

local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end

local act = {}
function act.volume(v)
    v = clamp(math.floor(v + 0.5), 0, 150)
    remember("volume", v)
    remember("mute", false)
    shell.spawn(string.format("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0; wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ %.2f", v / 100))
end
function act.mute()
    local _, muted = sys.volume()
    remember("mute", not muted)
    shell.spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")
end
function act.mic_mute()
    remember("mic", not sys.mic_muted())
    shell.spawn("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle")
end
function act.brightness(v)
    v = clamp(math.floor(v + 0.5), 1, 100)
    remember("brightness", v)
    shell.spawn(string.format("%s set %d", brightness_cmd, v))
end
function act.wifi()
    local on = sys.wifi()
    remember("wifi", not on)
    shell.spawn("nmcli radio wifi " .. (on and "off" or "on"))
end
function act.bluetooth()
    local on = sys.bluetooth()
    remember("bluetooth", not on)
    shell.spawn("bluetoothctl power " .. (on and "off" or "on"))
end
local PROFILES = { "power-saver", "balanced", "performance" }
local PROFILE_NAMES = { ["power-saver"] = "Power saver", balanced = "Balanced", performance = "Performance" }
function act.profile()
    local current, next = sys.profile(), PROFILES[1]
    for i, p in ipairs(PROFILES) do if p == current then next = PROFILES[i % #PROFILES + 1] end end
    remember("profile", next)
    shell.spawn("powerprofilesctl set " .. next)
end
function act.media(what)
    if what == "play-pause" then
        local m = sys.media()
        if m then remember("playing", not m.playing) end
    end
    shell.spawn("playerctl " .. what)
end

-- ---- session -----------------------------------------------------------------

local dnd = false
local function caffeine() return shell.compositor_value("tide.caffeine", "0") == "1" end

-- The lock screen is the shell's own (ext-session-lock); it is drawn
-- further down. Defined here so the session commands can use it.
local lock

local SESSION = {
    { id = "lock", label = "Lock", icon = "lock", run = function() lock() end },
    { id = "logout", label = "Log out", icon = "close", run = function() shell.quit() end },
    { id = "suspend", label = "Suspend", icon = "moon", run = function()
        lock()
        shell.spawn("sleep 0.5; systemctl suspend")
    end },
    { id = "reboot", label = "Restart", icon = "sliders", run = function() shell.spawn("systemctl reboot") end },
    { id = "poweroff", label = "Shut down", icon = "power", danger = true,
        run = function() shell.spawn("systemctl poweroff") end },
}

-- ---- wallpaper -------------------------------------------------------------
-- ~/.config/shady/wallpaper.png when there is one, else the night sea shader.

local home = os.getenv("HOME") or ""
local wallpaper = (os.getenv("XDG_CONFIG_HOME") or (home .. "/.config")) .. "/shady/wallpaper.png"
local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

local SEA = not exists(wallpaper) and shell.shader(HERE .. "/shaders/tide.frag") or nil
local SEA_UNIFORMS = { accent = P.accent, accent_2 = P.accent_2, base = "#0F0F1A" }

-- What lies under the wallpaper and the lock screen: the picture, or (with
-- the sea shader on top) nothing; the gradient covers the shm renderer.
local function backdrop(ctx)
    if not SEA then
        return shell.image { left = 0, top = 0, path = wallpaper, width = ctx.width, height = ctx.height }
    end
    return false
end

shell.popup {
    name = "tide-wallpaper",
    layer = "background",
    fill = true,
    outputs = "all",
    input = "widgets",
    shader = SEA,
    uniforms = SEA_UNIFORMS,
    view = function(ctx)
        return shell.stack { width = ctx.width, height = ctx.height, backdrop(ctx) }
    end,
}

-- ---- the strip minimap -----------------------------------------------------

local function strip()
    local raw = shell.compositor_value("layout.strip", "")
    local screen, view, active = raw:match("^(%-?%d+) (%-?%d+) (%d+)")
    local s = { screen = tonumber(screen) or 0, view = tonumber(view) or 0,
        active = tonumber(active) or 0, cols = {} }
    for x, w, rows in raw:gmatch("|(%-?%d+) (%d+) (%d+)") do
        s.cols[#s.cols + 1] = { x = tonumber(x), w = tonumber(w), rows = tonumber(rows) }
    end
    return s
end

local MAP_H = 14
local function minimap()
    local s = strip()
    if #s.cols < 2 or s.screen <= 0 then return false end
    local last = s.cols[#s.cols]
    local origin = math.min(0, s.view)
    local extent = math.max(last.x + last.w, s.view + s.screen) - origin
    local width = clamp(#s.cols * 14, 40, 110)
    local scale = width / extent
    local parts = {}
    for i, c in ipairs(s.cols) do
        local on = i == s.active
        local w = math.max(3, c.w * scale - 2)
        local h = (MAP_H - (c.rows - 1)) / c.rows
        for r = 1, c.rows do
            parts[#parts + 1] = shell.box {
                key = "map:" .. i .. ":" .. r, animate = "snappy",
                left = (c.x - origin) * scale, top = (r - 1) * (h + 1), width = w, height = h,
                radius = 2, background = on and P.accent or a(P.text, 0.30),
            }
        end
    end
    parts[#parts + 1] = shell.box {
        key = "map:view", animate = "snappy",
        left = (s.view - origin) * scale - 2, top = -2, width = s.screen * scale + 2, height = MAP_H + 4,
        radius = 4, border = a(P.accent_2, 0.75), border_width = 1,
    }
    return shell.stack { key = "minimap", width = width, height = MAP_H, parts }
end

-- ---- bar ---------------------------------------------------------------------

local function island(props)
    props.height = ISLAND
    props.radius = props.radius or 16
    props.padding = props.padding or { 0, 12 }
    props.gap = props.gap or 10
    props.align = "center"
    props.background = glass(0.88)
    props.border = a(P.text, 0.07)
    props.animate = "snappy"
    return shell.row(props)
end

local function workspace_names()
    local busy = {}
    for _, w in ipairs(shell.windows()) do busy[w.workspace] = true end
    local active = shell.active_workspace()
    local names = {}
    for _, n in ipairs(shell.workspaces()) do
        if busy[n] or n == active then names[#names + 1] = n end
    end
    table.sort(names, function(x, y)
        local nx, ny = tonumber(x), tonumber(y)
        if nx and ny then return nx < ny end
        return x < y
    end)
    return names, active, busy
end

local function workspaces_island(ctx)
    local names, active = workspace_names()
    local items = {}
    for _, n in ipairs(names) do
        local on = n == active
        local id = "ws:" .. n
        local hovered = ctx.hovered == id
        items[#items + 1] = shell.row {
            id = id, key = id, height = 22, radius = 11, gap = 8, animate = "snappy",
            padding = { 0, on and 10 or 8 },
            background = on and a(P.accent, 0.20) or (hovered and a(P.text, 0.08) or nil),
            border = on and a(P.accent, 0.35) or nil,
            on_click = function() shell.workspace(n) end,
            text(n, { font = font(on and "ExtraBold" or "SemiBold", 9),
                color = on and P.text or a(P.dim, hovered and 1 or 0.85) }),
            on and minimap() or false,
        }
    end
    return island {
        key = "island:left", padding = { 0, 5 }, gap = 3,
        on_scroll = function(dy)
            if math.abs(dy) < 2 then return end
            shell.key(dy > 0 and "Super+Page_Down" or "Super+Page_Up")
        end,
        items,
    }
end

local function focused_title()
    local f = shell.focused()
    if not f then return nil end
    local title = f.title ~= "" and f.title or f.app_id
    return title ~= "" and title or nil
end

local function clock_island(ctx)
    local open = shell.is_open("tide-calendar")
    return island {
        key = "island:clock", id = "clock",
        background = open and a(P.accent, 0.18) or nil,
        on_click = function() shell.toggle("tide-calendar") end,
        text(shell.date("%a %d %b"), { font = font("Medium", 9), color = P.dim }),
        text(shell.date("%H:%M"), { font = font("Bold", 10.5) }),
    }
end

local function battery_icon(level, status)
    local charging = status == "Charging" or status == "Full"
    local low = level <= 15 and not charging
    local color = low and P.danger or (charging and P.good or P.text)
    return shell.row {
        gap = 4, align = "center",
        shell.icon("battery", { size = 15, color = color }),
        text(level .. "%", { font = font("SemiBold", 9), color = color }),
        charging and text("⚡", { font = font("Bold", 8), color = P.good }) or false,
    }
end

local function status_island(ctx)
    local items = {}
    local media = sys.media()
    if media and media.title ~= "" then
        items[#items + 1] = shell.row {
            id = "bar:media", gap = 6, align = "center",
            on_click = function(button)
                if button == "right" then act.media("next") else act.media("play-pause") end
            end,
            shell.icon(media.playing and "music" or "pause", { size = 13, color = P.accent }),
            text(media.title, { font = font("Medium", 9), max_width = 150, color = a(P.text, 0.9) }),
        }
    end
    if dnd then items[#items + 1] = shell.icon("bell", { size = 14, color = P.warn }) end
    if caffeine() then items[#items + 1] = text("☕", { font = font("Medium", 10) }) end
    local wifi_on, ssid, wired = sys.wifi()
    if wifi_on ~= nil then
        items[#items + 1] = shell.icon("wifi", { size = 15,
            color = (wired or (wifi_on and ssid ~= "")) and P.text or a(P.dim, 0.6) })
    end
    local bt_on, bt_dev = sys.bluetooth()
    if bt_on then
        items[#items + 1] = shell.icon("bluetooth", { size = 15,
            color = bt_dev ~= "" and P.accent_2 or P.text })
    end
    local volume, muted = sys.volume()
    if volume then
        items[#items + 1] = shell.row {
            id = "bar:volume", gap = 4, align = "center",
            on_scroll = function(dy) act.volume(volume - dy / 4) end,
            on_click = function(button) if button == "middle" or button == "right" then act.mute() end end,
            shell.icon(muted and "mute" or "volume", { size = 15, color = muted and P.dim or P.text }),
        }
    end
    local level, status = sys.battery()
    if level then items[#items + 1] = battery_icon(level, status) end
    local open = shell.is_open("tide-control")
    return island {
        key = "island:status", id = "status",
        background = open and a(P.accent, 0.18) or nil,
        on_click = function() shell.toggle("tide-control") end,
        items,
        shell.box { width = 1, height = 14, background = a(P.text, 0.12) },
        shell.row {
            id = "bar:power", on_click = function() shell.toggle("tide-power") end,
            shell.icon("power", { size = 14, color = ctx.hovered == "bar:power" and P.danger or P.dim }),
        },
    }
end

shell.bar {
    name = "tide-bar",
    edge = "top",
    size = BAR,
    exclusive = true,
    layer = "top",
    view = function(ctx)
        local title = focused_title()
        return shell.stack {
            align = "start", justify = "center",
            shell.row { left = 8, top = 6, gap = 8, align = "center",
                workspaces_island(ctx),
                title and island { key = "island:title", padding = { 0, 12 },
                    text(title, { font = font("Medium", 9), max_width = math.floor(ctx.width * 0.22),
                        color = a(P.text, 0.82) }) } or false,
            },
            shell.row { top = 6, clock_island(ctx) },
            shell.row { right = 8, top = 6, status_island(ctx) },
        }
    end,
}

-- ---- sliders and tiles -----------------------------------------------------

-- A slider is a row of two growing parts, the filled share and the rest,
-- with the knob between them: no widget needs to know the track's width.
local dragging = {}

local function slider(ctx, id, icon, value, max, on_set, on_icon, muted)
    local f = value and clamp(value / max, 0, 1) or 0
    local hovered = ctx.hovered == id
    local fill = muted and P.dim or P.accent
    -- While dragged the fill is drawn where the pointer is, without a spring.
    local motion = not dragging[id] and QUICK or false
    return shell.row {
        height = 40, gap = 12, align = "center",
        shell.row {
            id = id .. ":icon", width = 34, height = 34, radius = 17, justify = "center",
            background = a(P.text, ctx.hovered == id .. ":icon" and 0.12 or 0.06),
            on_click = on_icon,
            shell.icon(icon, { size = 16, color = muted and P.dim or P.text }),
        },
        shell.row {
            id = id, grow = 1, height = 26, align = "center", gap = 0,
            on_drag = function(info)
                dragging[id] = not info.done or nil
                on_set(info.fx * max)
                shell.redraw()
            end,
            on_scroll = function(dy) on_set((value or 0) - dy / 4 * max / 100); shell.redraw() end,
            shell.box { key = id .. ":a", grow = math.max(f, 0.0001), height = 8, radius = 4,
                animate = motion,
                background = shell.gradient("horizontal", fill, muted and P.dim or P.accent_2) },
            shell.box { key = id .. ":knob", width = hovered and 16 or 12, height = hovered and 16 or 12,
                radius = 8, animate = motion, background = WHITE },
            shell.box { key = id .. ":b", grow = math.max(1 - f, 0.0001), height = 8, radius = 4,
                animate = motion, background = a(P.text, 0.10) },
        },
        text(value and tostring(math.floor(value + 0.5)) or "–", { width = 30, text_align = 1,
            font = font("SemiBold", 9), color = P.dim }),
    }
end

local function tile(ctx, id, icon, title, subtitle, on, on_click)
    local hovered = ctx.hovered == id
    return shell.row {
        id = id, grow = 1, height = 56, radius = 16, padding = { 0, 12 }, gap = 10, animate = "snappy",
        background = on and shell.gradient("diagonal", a(P.accent, 0.34), a(P.accent_2, 0.22))
            or a(P.text, hovered and 0.09 or 0.05),
        border = on and a(P.accent, 0.45) or a(P.text, 0.06),
        on_click = on_click,
        shell.row {
            width = 30, height = 30, radius = 15, justify = "center",
            background = on and a(WHITE, 0.16) or a(P.text, 0.07),
            shell.icon(icon, { size = 15, color = on and WHITE or P.dim }),
        },
        shell.column {
            grow = 1, shrink = 1, align = "start", gap = 1,
            text(title, { font = font("Bold", 9.5) }),
            text(subtitle, { font = font("Medium", 8.5), color = on and a(P.text, 0.8) or P.dim }),
        },
    }
end

local function section(label)
    return text(label:upper(), { font = font("Bold", 7.5), letter_spacing = 1.6, color = a(P.dim, 0.8) })
end

-- ---- notifications ---------------------------------------------------------

shell.notification_server { timeout = 6000 }

local function ago(seconds)
    if seconds < 60 then return "now" end
    if seconds < 3600 then return (seconds // 60) .. "m" end
    if seconds < 86400 then return (seconds // 3600) .. "h" end
    return (seconds // 86400) .. "d"
end

local function activate(n)
    for _, action in ipairs(n.actions) do
        if action.key == "default" then
            shell.invoke_notification(n.id, "default")
            return
        end
    end
    shell.dismiss_notification(n.id)
end

local function note(ctx, n, opts)
    opts = opts or {}
    local id = (opts.prefix or "note:") .. n.id
    local critical = n.urgency == "critical"
    local actions = {}
    for _, action in ipairs(n.actions) do
        if action.key ~= "default" and action.label ~= "" then
            local aid = id .. ":" .. action.key
            actions[#actions + 1] = shell.row {
                id = aid, radius = 10, padding = { 5, 11 }, animate = "snappy",
                background = a(P.text, ctx.hovered == aid and 0.14 or 0.07),
                on_click = function() shell.invoke_notification(n.id, action.key) end,
                text(action.label, { font = font("SemiBold", 8.5) }),
            }
        end
    end
    local close = id .. ":x"
    return shell.column {
        key = id, id = id, align = "stretch", gap = 5, padding = { 11, 13 }, radius = 14,
        animate = "snappy", enter = { opacity = 0, translate = { 24, 0 } },
        background = opts.background or a(P.text, ctx.hovered == id and 0.08 or 0.05),
        border = critical and a(P.danger, 0.6) or a(P.text, 0.06),
        on_click = function() activate(n) end,
        shell.row {
            gap = 8, align = "center",
            shell.pip { radius = 3, lit = n.active, color = critical and P.danger or P.accent },
            text(n.app ~= "" and n.app or "Notification", { font = font("Bold", 8),
                color = P.dim, shrink = 1 }),
            shell.spacer(),
            text(ago(os.time() - n.time), { font = font("Medium", 8), color = a(P.dim, 0.7) }),
            shell.row {
                id = close, width = 18, height = 18, radius = 9, justify = "center",
                background = ctx.hovered == close and a(P.text, 0.12) or nil,
                on_click = function() shell.dismiss_notification(n.id) end,
                shell.icon("close", { size = 10, color = P.dim }),
            },
        },
        n.summary ~= "" and text(n.summary, { font = font("Bold", 9.5), wrap = true, max_lines = 2,
            max_width = opts.width or 320 }) or false,
        n.body ~= "" and text(n.body, { font = font("Medium", 9), color = a(P.text, 0.75), wrap = true,
            max_lines = opts.lines or 3, max_width = opts.width or 320 }) or false,
        #actions > 0 and shell.row { gap = 6, actions } or false,
    }
end

local function sync_toasts()
    local want = false
    if not dnd and not shell.is_open("tide-control") then
        for _, n in ipairs(shell.notifications()) do want = want or n.active end
    end
    if want ~= shell.is_open("tide-toasts") then
        if want then shell.open("tide-toasts") else shell.close("tide-toasts") end
    end
end
shell.on("notifications", sync_toasts)

shell.popup {
    name = "tide-toasts",
    anchor = { "top", "right" },
    margin = { top = BAR + 6, right = 10 },
    width = 356,
    keyboard = "none",
    view = function(ctx)
        local list = {}
        for _, n in ipairs(shell.notifications()) do
            if n.active and #list < 3 then
                list[#list + 1] = note(ctx, n, { prefix = "toast:", background = glass(0.96), width = 300 })
            end
        end
        return shell.column { align = "stretch", gap = 8, list }
    end,
}

-- ---- control center ----------------------------------------------------------

local user = os.getenv("USER") or "you"

local function media_card(ctx)
    local m = sys.media()
    if not m then return false end
    local function button(id, icon, what, big)
        return shell.row {
            id = id, width = big and 38 or 30, height = big and 38 or 30, radius = 19, justify = "center",
            background = big and a(P.accent, 0.3) or a(P.text, ctx.hovered == id and 0.12 or 0.05),
            on_click = function() act.media(what) end,
            shell.icon(icon, { size = big and 16 or 13 }),
        }
    end
    return shell.row {
        radius = 16, padding = { 10, 12 }, gap = 12, align = "center",
        background = shell.gradient("diagonal", a(P.accent_2, 0.14), a(P.text, 0.04)),
        border = a(P.text, 0.06),
        shell.row { width = 40, height = 40, radius = 12, justify = "center",
            background = shell.gradient("diagonal", P.accent, P.accent_2),
            shell.icon("music", { size = 18, color = P.base }) },
        shell.column { grow = 1, shrink = 1, align = "start", gap = 2,
            text(m.title, { font = font("Bold", 9.5), max_width = 170 }),
            text(m.artist ~= "" and m.artist or "Playing", { font = font("Medium", 8.5), color = P.dim, max_width = 170 }),
        },
        button("media:prev", "previous", "previous"),
        button("media:play", m.playing and "pause" or "play", "play-pause", true),
        button("media:next", "next", "next"),
    }
end

local function control_view(ctx)
    local volume, muted = sys.volume()
    local brightness = sys.brightness()
    local wifi_on, ssid, wired = sys.wifi()
    local bt_on, bt_dev = sys.bluetooth()
    local profile = sys.profile()
    local level, status = sys.battery()

    local header = shell.row {
        gap = 12, align = "center",
        shell.row { width = 40, height = 40, radius = 20, justify = "center",
            background = shell.gradient("diagonal", P.accent, P.accent_2),
            text(user:sub(1, 1):upper(), { font = font("ExtraBold", 13), color = P.base }) },
        shell.column { grow = 1, align = "start", gap = 1,
            text(user, { font = font("Bold", 11) }),
            text(sys.uptime(), { font = font("Medium", 8.5), color = P.dim }) },
        shell.row { id = "cc:lock", width = 34, height = 34, radius = 17, justify = "center",
            background = a(P.text, ctx.hovered == "cc:lock" and 0.12 or 0.06),
            on_click = function() lock() end, shell.icon("lock", { size = 15 }) },
        shell.row { id = "cc:power", width = 34, height = 34, radius = 17, justify = "center",
            background = ctx.hovered == "cc:power" and a(P.danger, 0.25) or a(P.text, 0.06),
            on_click = function() shell.close("tide-control"); shell.open("tide-power") end,
            shell.icon("power", { size = 15 }) },
    }

    local tiles = {}
    if wifi_on ~= nil then
        tiles[#tiles + 1] = tile(ctx, "t:wifi", "wifi", "Wi-Fi",
            wired and "Wired" or (wifi_on and (ssid ~= "" and ssid or "Not connected") or "Off"),
            wifi_on, act.wifi)
    end
    if bt_on ~= nil then
        tiles[#tiles + 1] = tile(ctx, "t:bt", "bluetooth", "Bluetooth",
            bt_on and (bt_dev ~= "" and bt_dev or "On") or "Off", bt_on, act.bluetooth)
    end
    tiles[#tiles + 1] = tile(ctx, "t:dnd", "bell", "Do not disturb", dnd and "On" or "Off", dnd,
        function() dnd = not dnd; sync_toasts() end)
    tiles[#tiles + 1] = tile(ctx, "t:caffeine", "sun", "Stay awake", caffeine() and "On" or "Off",
        caffeine(), function() shell.key("Super+Ctrl+Shift+F12") end)
    if profile and profile ~= "" then
        tiles[#tiles + 1] = tile(ctx, "t:profile", "sliders", "Power", PROFILE_NAMES[profile] or profile,
            profile ~= "balanced", act.profile)
    end
    tiles[#tiles + 1] = tile(ctx, "t:shot", "capture", "Screenshot", "Region", false, function()
        shell.close("tide-control")
        shell.after(350, function() shell.key("Print") end)
    end)
    local grid = {}
    for i = 1, #tiles, 2 do
        grid[#grid + 1] = shell.row { gap = 8, align = "stretch", tiles[i], tiles[i + 1] or shell.spacer { grow = 1 } }
    end

    local notes, count = {}, 0
    for _, n in ipairs(shell.notifications()) do
        count = count + 1
        if #notes < 4 then notes[#notes + 1] = note(ctx, n, { width = 300, lines = 2 }) end
    end

    local battery_line = false
    if level then
        local when = sys.battery_time()
        battery_line = shell.row { gap = 8, align = "center",
            battery_icon(level, status),
            text(status == "Charging" and "Charging" or (status == "Full" and "Full" or "On battery"),
                { font = font("Medium", 8.5), color = P.dim }),
            shell.spacer(),
            when and text(when, { font = font("Medium", 8.5), color = P.dim }) or false,
        }
    end

    return card {
        width = 380, padding = 16, gap = 14, background = glass(0.95),
        header,
        shell.column { align = "stretch", gap = 2,
            slider(ctx, "s:volume", muted and "mute" or "volume", volume, 100, act.volume, act.mute, muted),
            brightness and slider(ctx, "s:brightness", "sun", brightness, 100, act.brightness) or false,
        },
        shell.column { align = "stretch", gap = 8, grid },
        media_card(ctx),
        shell.column { align = "stretch", gap = 8,
            shell.row { align = "center",
                section(count > 0 and ("Notifications · " .. count) or "Notifications"),
                shell.spacer(),
                count > 0 and shell.row { id = "cc:clear", radius = 8, padding = { 3, 8 },
                    background = a(P.text, ctx.hovered == "cc:clear" and 0.12 or 0.0),
                    on_click = function() shell.clear_notifications() end,
                    text("Clear", { font = font("SemiBold", 8.5), color = P.dim }) } or false,
            },
            #notes > 0 and shell.column { align = "stretch", gap = 6, notes }
                or text("Nothing new", { font = font("Medium", 9), color = a(P.dim, 0.7) }),
        },
        battery_line,
    }
end

shell.popup {
    name = "tide-control",
    anchor = { "top", "right" },
    margin = { top = BAR + 4, right = 8 },
    keyboard = "on_demand",
    view = control_view,
    on_key = function(key)
        if key == "Escape" then shell.close("tide-control") end
    end,
}

-- ---- calendar ----------------------------------------------------------------

local cal = { offset = 0 }

shell.popup {
    name = "tide-calendar",
    anchor = { "top" },
    margin = { top = BAR + 4 },
    keyboard = "on_demand",
    view = function(ctx)
        local now = os.date("*t")
        local first = os.time { year = now.year, month = now.month + cal.offset, day = 1, hour = 12 }
        local ft = os.date("*t", first)
        local days = os.date("*t", os.time { year = ft.year, month = ft.month + 1, day = 0, hour = 12 }).day
        local lead = (ft.wday + 5) % 7 -- Monday first
        local rows, cells = {}, {}
        local heads = {}
        for _, d in ipairs({ "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" }) do
            heads[#heads + 1] = text(d, { width = 34, text_align = 0.5, font = font("Bold", 8), color = a(P.dim, 0.8) })
        end
        rows[1] = shell.row { gap = 2, heads }
        for _ = 1, lead do cells[#cells + 1] = shell.box { width = 34, height = 30 } end
        for d = 1, days do
            local today = cal.offset == 0 and d == now.day
            cells[#cells + 1] = shell.row {
                width = 34, height = 30, radius = 10, justify = "center",
                background = today and shell.gradient("diagonal", P.accent, P.accent_2) or nil,
                text(tostring(d), { font = font(today and "ExtraBold" or "Medium", 9),
                    color = today and P.base or P.text }),
            }
            if #cells == 7 then
                rows[#rows + 1] = shell.row { gap = 2, cells }
                cells = {}
            end
        end
        if #cells > 0 then rows[#rows + 1] = shell.row { gap = 2, cells } end
        local function nav(id, label, delta)
            return shell.row { id = id, width = 28, height = 28, radius = 14, justify = "center",
                background = a(P.text, ctx.hovered == id and 0.12 or 0.05),
                on_click = function() cal.offset = cal.offset + delta; shell.redraw() end,
                text(label, { font = font("Bold", 10) }) }
        end
        return card {
            padding = 16, gap = 10,
            shell.row { align = "center", gap = 8,
                shell.column { align = "start", gap = 0,
                    text(os.date("%B %Y", first), { font = font("Bold", 12) }),
                    text(os.date("%A, %d %B"), { font = font("Medium", 8.5), color = P.dim }) },
                shell.spacer(),
                nav("cal:prev", "‹", -1), nav("cal:next", "›", 1),
            },
            shell.column { gap = 2, align = "start", rows },
        }
    end,
    on_key = function(key)
        if key == "Escape" then shell.close("tide-calendar"); cal.offset = 0
        elseif key == "Left" then cal.offset = cal.offset - 1
        elseif key == "Right" then cal.offset = cal.offset + 1 end
        shell.redraw()
    end,
}

-- ---- launcher ----------------------------------------------------------------
-- Apps, ordered by how often they were launched; "= 2*21" calculates,
-- "> cmd" runs a command, and the session's commands match by name.

local state_dir = (os.getenv("XDG_STATE_HOME") or (home .. "/.local/state")) .. "/tide"
local counts_path = state_dir .. "/launches"
local counts = {}
do
    local f = io.open(counts_path, "r")
    if f then
        for line in f:lines() do
            local n, name = line:match("^(%d+)\t(.+)$")
            if n then counts[name] = tonumber(n) end
        end
        f:close()
    end
end
local function save_counts()
    os.execute(string.format("mkdir -p %q", state_dir))
    local f = io.open(counts_path, "w")
    if not f then return end
    for name, n in pairs(counts) do f:write(n, "\t", name, "\n") end
    f:close()
end

local L = { query = "", selected = 1 }

local function calculate(expr)
    expr = expr:gsub("^=%s*", "")
    if expr == "" or not expr:match("^[%d%s%.%+%-%*/%%%^%(%)eEpisqrtlogfloorcexpabnm,]+$") then return nil end
    local env = setmetatable({}, { __index = math })
    local fn = load("return " .. expr, "calc", "t", env)
    if not fn then return nil end
    local ok, v = pcall(fn)
    if not ok or type(v) ~= "number" or v ~= v then return nil end
    if v == math.floor(v) and math.abs(v) < 1e15 then return string.format("%d", v) end
    return (string.format("%.10g", v))
end

local function results()
    local q = L.query
    local list = {}
    if q:match("^>") then
        local cmd = trim(q:sub(2))
        if cmd ~= "" then
            list[#list + 1] = { kind = "run", title = cmd, subtitle = "Run command", icon = "grid",
                run = function() shell.spawn(cmd) end }
        end
        return list
    end
    local value = calculate(q)
    if value and (q:match("^=") or q:match("[%+%-%*/%^%%]")) then
        list[#list + 1] = { kind = "calc", title = value, subtitle = "= " .. trim(q:gsub("^=", "")) ..
            (HAVE.wlcopy and "  ·  Enter copies" or ""), icon = "plus",
            run = function()
                if HAVE.wlcopy then shell.spawn(string.format("printf %%s %q | wl-copy", value)) end
            end }
    end
    local lq = q:lower()
    if lq ~= "" then
        for _, s in ipairs(SESSION) do
            if s.label:lower():find(lq, 1, true) == 1 or s.id:find(lq, 1, true) == 1 then
                list[#list + 1] = { kind = "session", title = s.label, subtitle = "Session",
                    icon = s.icon, run = s.run }
            end
        end
    end
    local apps = shell.apps(q, q == "" and 60 or 12)
    table.sort(apps, function(x, y)
        local cx, cy = counts[x.name] or 0, counts[y.name] or 0
        if q ~= "" then
            -- A name that starts with the query beats one that contains it.
            local sx = x.name:lower():find(lq, 1, true) == 1
            local sy = y.name:lower():find(lq, 1, true) == 1
            if sx ~= sy then return sx end
        end
        if cx ~= cy then return cx > cy end
        return x.name < y.name
    end)
    for _, app in ipairs(apps) do
        if #list >= 8 then break end
        local category = (app.categories or ""):match("^([^;]+)") or ""
        list[#list + 1] = { kind = "app", title = app.name, subtitle = category,
            app = app, run = function()
                counts[app.name] = (counts[app.name] or 0) + 1
                save_counts()
                shell.launch(app)
            end }
    end
    return list
end

local function hue(name)
    local h = 0
    for i = 1, #name do h = (h * 31 + name:byte(i)) % 360 end
    return h
end
local SWATCHES = { "#B4A0FF", "#7CB7FF", "#8BD5A8", "#F2C27B", "#FF9EB5", "#86E1E8" }

local function choose(item)
    shell.close("tide-launcher")
    if item then item.run() end
end

shell.on("launcher", function()
    L.query, L.selected = "", 1
    shell.close("tide-control")
    shell.toggle("tide-launcher")
end)

shell.popup {
    name = "tide-launcher",
    width = 560,
    keyboard = "exclusive",
    text_input = true,
    view = function(ctx)
        local list = results()
        if L.selected > #list then L.selected = math.max(#list, 1) end
        local rows = {}
        for i, item in ipairs(list) do
            local id = "res:" .. i
            local sel = i == L.selected
            local lit = sel or ctx.hovered == id
            local swatch = SWATCHES[hue(item.title) % #SWATCHES + 1]
            local glyph = item.kind == "app"
                and text((item.title:match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper(),
                    { font = font("ExtraBold", 11), color = P.base })
                or shell.icon(item.icon or "grid", { size = 15, color = P.base })
            rows[#rows + 1] = shell.row {
                id = id, key = "res:" .. item.kind .. ":" .. item.title, height = 50, radius = 14,
                padding = { 0, 10 }, gap = 12, animate = "snappy",
                background = lit and (sel and shell.gradient("horizontal", a(P.accent, 0.26), a(P.accent_2, 0.12))
                    or a(P.text, 0.06)) or nil,
                border = sel and a(P.accent, 0.4) or nil,
                on_click = function() choose(item) end,
                shell.row { width = 34, height = 34, radius = 11, justify = "center",
                    background = item.kind == "app" and swatch or shell.gradient("diagonal", P.accent, P.accent_2),
                    glyph },
                shell.column { grow = 1, shrink = 1, align = "start", gap = 1,
                    text(item.title, { font = font(sel and "Bold" or "SemiBold", 10) }),
                    item.subtitle ~= "" and text(item.subtitle, { font = font("Medium", 8.5), color = P.dim }) or false },
                sel and text("↵", { font = font("Bold", 11), color = P.accent }) or false,
            }
        end
        if #list == 0 then
            rows[1] = shell.column { padding = { 14, 12 }, align = "start", gap = 3,
                text("Nothing matches", { font = font("Bold", 10) }),
                text("“= 2+2” calculates, “> cmd” runs a command", { font = font("Medium", 8.5), color = P.dim }) }
        end
        local preedit = ctx.preedit or ""
        local field = shell.row {
            height = 46, radius = 14, padding = { 0, 14 }, gap = 10, align = "center",
            background = a(P.base, 0.55), border = a(P.accent, 0.38),
            shell.icon("search", { size = 16, color = P.accent }),
            (L.query ~= "" or preedit ~= "") and shell.row { gap = 1, align = "center",
                text(L.query, { font = font("SemiBold", 11) }),
                preedit ~= "" and text(preedit, { font = font("SemiBold", 11), color = P.accent }) or false,
                shell.box { width = 2, height = 20, radius = 1, background = P.accent, caret = true },
            } or shell.row { gap = 0, align = "center",
                shell.box { width = 0, height = 20, caret = true },
                text("Search apps, = calculate, > run", { font = font("Medium", 10.5), color = a(P.dim, 0.8) }),
            },
        }
        return card {
            width = 560, padding = 14, gap = 10, radius = 22,
            field,
            shell.column { align = "stretch", gap = 3, rows },
        }
    end,
    on_key = function(key, typed)
        local list = results()
        if key == "Escape" then
            shell.close("tide-launcher")
        elseif key == "Return" or key == "KP_Enter" then
            choose(list[L.selected])
        elseif key == "Down" or key == "Tab" then
            if #list > 0 then L.selected = L.selected % #list + 1 end
        elseif key == "Up" or key == "ISO_Left_Tab" then
            if #list > 0 then L.selected = (L.selected - 2) % #list + 1 end
        elseif key == "BackSpace" then
            local cut = utf8.offset(L.query, -1)
            if cut then L.query = L.query:sub(1, cut - 1) end
            L.selected = 1
        elseif typed ~= "" and #L.query + #typed < 120 then
            L.query = L.query .. typed
            L.selected = 1
        end
        shell.redraw()
    end,
}

-- ---- power menu ----------------------------------------------------------------

local power = { selected = 1 }

shell.popup {
    name = "tide-power",
    keyboard = "exclusive",
    view = function(ctx)
        local buttons = {}
        for i, s in ipairs(SESSION) do
            local id = "pw:" .. s.id
            local sel = i == power.selected or ctx.hovered == id
            local tint = s.danger and P.danger or P.accent
            buttons[#buttons + 1] = shell.column {
                id = id, width = 96, height = 104, radius = 20, gap = 10, justify = "center", align = "center",
                animate = "snappy",
                background = sel and shell.gradient("diagonal", a(tint, 0.32), a(P.accent_2, 0.12)) or a(P.text, 0.05),
                border = sel and a(tint, 0.5) or a(P.text, 0.06),
                scale = sel and 1.0 or 0.97,
                on_click = function() shell.close("tide-power"); s.run() end,
                shell.row { width = 44, height = 44, radius = 22, justify = "center",
                    background = sel and a(WHITE, 0.14) or a(P.text, 0.06),
                    shell.icon(s.icon, { size = 20, color = sel and WHITE or P.text }) },
                text(s.label, { font = font("Bold", 9.5) }),
            }
        end
        return card {
            padding = 18, gap = 14, radius = 26, align = "center",
            text("See you soon, " .. user, { font = font("Bold", 12) }),
            shell.row { gap = 10, buttons },
            text("←  →  choose     ↵  confirm     Esc  cancel", { font = font("Medium", 8.5), color = P.dim }),
        }
    end,
    on_key = function(key, typed)
        if key == "Escape" then shell.close("tide-power")
        elseif key == "Left" or key == "ISO_Left_Tab" then power.selected = (power.selected - 2) % #SESSION + 1
        elseif key == "Right" or key == "Tab" then power.selected = power.selected % #SESSION + 1
        elseif key == "Return" or key == "KP_Enter" then
            shell.close("tide-power")
            SESSION[power.selected].run()
        else
            for i, s in ipairs(SESSION) do
                if typed ~= "" and s.label:lower():sub(1, 1) == typed:lower() then power.selected = i end
            end
        end
        shell.redraw()
    end,
}

-- ---- OSD -----------------------------------------------------------------------

local osd = { kind = "volume", value = 0, muted = false, timer = nil }

local function show_osd(kind, value, muted)
    osd.kind, osd.value, osd.muted = kind, value or 0, muted
    if not shell.is_open("tide-osd") then shell.open("tide-osd") end
    if osd.timer then shell.cancel(osd.timer) end
    osd.timer = shell.after(1400, function()
        osd.timer = nil
        shell.close("tide-osd")
    end)
    shell.redraw()
end

shell.popup {
    name = "tide-osd",
    anchor = { "bottom" },
    margin = { bottom = 80 },
    keyboard = "none",
    view = function()
        local icon = osd.kind == "brightness" and "sun" or (osd.muted and "mute" or "volume")
        local f = clamp(osd.value / 100, 0, 1)
        return card {
            width = 260, padding = { 12, 16 }, radius = 22, background = glass(0.95),
            shell.row { gap = 12, align = "center",
                shell.icon(icon, { size = 18, color = osd.muted and P.dim or P.text }),
                shell.row { grow = 1, height = 6, gap = 0, align = "center",
                    shell.box { key = "osd:a", grow = math.max(f, 0.0001), height = 6, radius = 3, animate = QUICK,
                        background = shell.gradient("horizontal", osd.muted and P.dim or P.accent,
                            osd.muted and P.dim or P.accent_2) },
                    shell.box { key = "osd:b", grow = math.max(1 - f, 0.0001), height = 6, radius = 3,
                        animate = QUICK, background = a(P.text, 0.12) },
                },
                text(tostring(math.floor(osd.value + 0.5)), { width = 28, text_align = 1, font = font("Bold", 9.5) }),
            },
        }
    end,
}

-- ---- lock screen ----------------------------------------------------------------
-- The clock large over the wallpaper, a field for the password, and the
-- fingerprint reader listening at the same time when there is one. Any key
-- types into the field; Enter checks it, Escape clears it.

local LK = { password = "", busy = false, message = "", error = false, shake = 0,
    fingerprint = nil, at = 0 }
local function file_exists(p) local f = io.open(p, "r"); if f then f:close() end; return f ~= nil end
local FINGERPRINT_SERVICE = "shady-lock-fingerprint"

local function listen_fingerprint()
    if LK.fingerprint or not file_exists("/etc/pam.d/" .. FINGERPRINT_SERVICE) then return end
    LK.fingerprint = shell.authenticate(nil, function(ok)
        LK.fingerprint = nil
        if ok then
            shell.unlock()
        elseif shell.locked() then
            -- Wrong finger or a timeout: listen again a little later.
            shell.after(1500, listen_fingerprint)
        end
    end, { service = FINGERPRINT_SERVICE, on_message = function(_, text)
        if not LK.busy then LK.message, LK.error = text, false end
    end })
end

local function reset_lock()
    LK.password, LK.busy, LK.message, LK.error = "", false, "", false
end

lock = function()
    shell.close("tide-control")
    shell.close("tide-power")
    shell.close("tide-launcher")
    if shell.locked() then return end
    reset_lock()
    LK.at = os.time()
    if shell.lock() then
        listen_fingerprint()
    elseif HAVE.swaylock then
        shell.spawn("pidof swaylock >/dev/null || swaylock -f -c 11111b")
    else
        shell.notify("Cannot lock the screen", "The compositor offers no session lock.", "Tide")
    end
end

local function submit()
    if LK.busy or LK.password == "" then return end
    LK.busy, LK.message, LK.error = true, "Checking…", false
    local password = LK.password
    shell.authenticate(password, function(ok, reason)
        LK.busy = false
        if ok then
            if LK.fingerprint then shell.cancel_authentication(LK.fingerprint); LK.fingerprint = nil end
            reset_lock()
            shell.unlock()
        else
            LK.password, LK.error = "", true
            LK.message = reason ~= "" and reason or "That was not it"
            LK.shake = LK.shake + 1
        end
    end)
end

shell.on("unlocked", function()
    if LK.fingerprint then shell.cancel_authentication(LK.fingerprint); LK.fingerprint = nil end
    reset_lock()
end)

shell.popup {
    name = "tide-lock",
    lock = true,
    shader = SEA,
    uniforms = SEA_UNIFORMS,
    keyboard = "exclusive",
    text_input = false,
    view = function(ctx)
        local level, status = sys.battery()
        local dots = {}
        local n = utf8.len(LK.password) or #LK.password
        for i = 1, math.min(n, 24) do
            dots[#dots + 1] = shell.box { key = "dot:" .. i, width = 8, height = 8, radius = 4,
                background = P.text, enter = { opacity = 0, scale = 0.2 }, animate = "snappy" }
        end
        local field = shell.row {
            key = "lock:field:" .. LK.shake, width = 300, height = 48, radius = 24,
            padding = { 0, 20 }, gap = 8, align = "center", justify = n > 0 and "center" or "start",
            background = glass(0.72), border = LK.error and a(P.danger, 0.7) or a(P.text, 0.14),
            enter = LK.shake > 0 and { translate = { 14, 0 } } or nil, animate = "bouncy",
            n > 0 and dots or shell.row { gap = 10, align = "center",
                shell.icon("lock", { size = 15, color = P.dim }),
                text("Password", { font = font("Medium", 10.5), color = a(P.dim, 0.85) }) },
        }
        return shell.stack {
            width = ctx.width, height = ctx.height, justify = "center",
            backdrop(ctx),
            shell.box { left = 0, right = 0, top = 0, bottom = 0,
                background = shell.gradient("vertical", a(P.base, 0.35), a(P.base, 0.15), a(P.base, 0.55)) },
            shell.column {
                top = math.floor(ctx.height * 0.17), align = "center", gap = 0,
                enter = { opacity = 0, translate = { 0, -20 } }, animate = "gentle",
                text(shell.date("%H:%M"), { font = SANS .. " ExtraLight 96", letter_spacing = -2 }),
                text(shell.date("%A, %d %B"), { font = font("Medium", 13), color = a(P.text, 0.8) }),
            },
            shell.column {
                bottom = math.floor(ctx.height * 0.22), align = "center", gap = 14,
                enter = { opacity = 0, translate = { 0, 20 } }, animate = "gentle",
                shell.row { width = 64, height = 64, radius = 32, justify = "center",
                    background = shell.gradient("diagonal", P.accent, P.accent_2),
                    text(user:sub(1, 1):upper(), { font = font("ExtraBold", 22), color = P.base }) },
                text(user, { font = font("Bold", 12) }),
                field,
                text(LK.message ~= "" and LK.message or " ", { font = font("Medium", 9.5),
                    color = LK.error and P.danger or P.dim }),
            },
            level and shell.row { right = 28, bottom = 24, gap = 6, align = "center",
                battery_icon(level, status) } or false,
        }
    end,
    on_key = function(key, typed)
        if key == "Return" or key == "KP_Enter" then
            submit()
        elseif key == "Escape" then
            LK.password, LK.message, LK.error = "", "", false
        elseif key == "BackSpace" then
            local cut = utf8.offset(LK.password, -1)
            if cut then LK.password = LK.password:sub(1, cut - 1) end
        elseif typed ~= "" and not LK.busy and #LK.password < 512 then
            LK.password = LK.password .. typed
            if LK.error then LK.message, LK.error = "", false end
        end
        shell.redraw()
    end,
}

-- A shell started (or restarted) while the session is locked takes the
-- lock over, so it can still be unlocked.
shell.on("value", function(k, v)
    if k == "session.lock" and v == "abandoned" then lock() end
end)

-- ---- keys from the compositor ------------------------------------------------

local function handle(action)
    local verb, arg = action:match("^(%S+)%s*(%S*)")
    if verb == "volume" then
        local v = sys.volume() or 50
        act.volume(v + (tonumber(arg) or 0))
        local nv = sys.volume()
        show_osd("volume", nv, false)
    elseif verb == "mute" then
        act.mute()
        local v, m = sys.volume()
        show_osd("volume", v, m)
    elseif verb == "mic-mute" then
        act.mic_mute()
        shell.notify(sys.mic_muted() and "Microphone muted" or "Microphone on", "", "Audio")
    elseif verb == "brightness" then
        local b = sys.brightness() or 50
        act.brightness(b + (tonumber(arg) or 0))
        show_osd("brightness", sys.brightness(), false)
    elseif verb == "media" then
        act.media(arg)
    elseif verb == "control" then
        shell.close("tide-launcher")
        shell.toggle("tide-control")
    elseif verb == "notifications" then
        shell.toggle("tide-control")
    elseif verb == "power" then
        power.selected = 1
        shell.toggle("tide-power")
    elseif verb == "lock" then
        lock()
    end
end

-- Values already set are replayed when the shell connects; those are not
-- key presses.
local started = os.time()
shell.on("value", function(k, v)
    if not v then return end
    if k == "tide.do" then
        if os.time() - started < 2 then return end
        handle((v:gsub("%s+%d+$", "")))
    elseif k == "screenshot.saved" and os.time() - started >= 2 then
        shell.notify("Screenshot saved", (v:gsub("^" .. home, "~")), "Screenshot")
    end
end)

-- The control center replaces the toasts while it is open.
shell.on("change", sync_toasts)

-- ---- shortcuts (Super+Shift+/) -------------------------------------------------

require("shortcut_overlay").install {
    name = "tide-shortcuts",
    palette = { text = P.text, text_dim = P.dim, accent = P.accent, surface = P.surface },
    title = "Shortcuts",
    title_font = font("ExtraBold", 15),
    font = font("Medium", 9.5),
    bold = font("Bold", 9.5),
    columns = 3,
    order = { "Windows", "Workspaces", "Shell", "Shady" },
    cap = function(p)
        return { radius = 7, font = font("Bold", 8.5), color = p.text,
            background = a(P.text, 0.07), border = a(P.text, 0.12) }
    end,
    frame = function(body)
        return card { radius = 24, padding = 24, body }
    end,
}
