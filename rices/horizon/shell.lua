-- Horizon: shady-shell living in Afterglow's sky.
--
-- There is no bar. The time is written large across the sky, the workspaces
-- are lights sitting on the horizon with the open windows reflected just
-- below it, a slab of frosted glass stands in the air for the controls
-- (init.lua turns it in 3D), and the launcher scatters the applications as
-- stars: type a name and the matching stars light up and join into a
-- constellation. Colours follow the sky through its four hours (Super+T).

shell.fonts("fonts")

-- The sky's horizon, as a fraction of the screen height from the top, for
-- Afterglow's camera (rices/afterglow/init.lua).
local HORIZON = 0.575

local SERIF, SERIF_ITALIC, SANS = "Instrument Serif", "Instrument Serif Italic", "Manrope"
local a, mix = shell.alpha, shell.mix

-- The live palette published by the afterglow plugin.
local fallback = {
    text = "#fff1e6", text_dim = "#e7b9b0", accent = "#ffb070", accent_2 = "#ff6f91",
    surface = "#140f1c", sun = "#fff2dc", horizon = "#ffd29a", sky_top = "#160c2e",
    sky_mid = "#6a2b63",
}
local c = setmetatable({}, {
    __index = function(_, name)
        return shell.compositor_value("afterglow." .. name, fallback[name])
    end,
})

local PHRASES = {
    ["golden-hour"] = "the light is golden",
    ["afterglow"] = "the sun is low",
    ["blue-hour"] = "the blue hour",
    ["night"] = "the stars are out",
}
local HOURS = {
    { id = "golden-hour", label = "Golden" },
    { id = "afterglow", label = "Afterglow" },
    { id = "blue-hour", label = "Blue hour" },
    { id = "night", label = "Night" },
}

local function serif(size) return SERIF .. " " .. size end
local function sans(weight, size) return SANS .. " " .. weight .. " " .. size end

local function text(s, props)
    props = props or {}
    props.color = props.color or c.text
    return shell.text(s, props)
end

-- Small caps-like label: spaced, upper case.
local function label(s, props)
    props = props or {}
    props.font = props.font or sans("SemiBold", 8.5)
    props.letter_spacing = props.letter_spacing or 2.4
    props.color = props.color or c.text_dim
    return shell.text(s:upper(), props)
end

local function trim(s)
    return ((s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function hash(s)
    local h = 7
    for i = 1, #s do h = (h * 131 + s:byte(i)) % 1000003 end
    return h
end

local function ago(seconds)
    if seconds < 60 then return "just now" end
    if seconds < 3600 then return (seconds // 60) .. " min ago" end
    if seconds < 86400 then return (seconds // 3600) .. " h ago" end
    return (seconds // 86400) .. " d ago"
end

local function hour()
    return shell.compositor_value("afterglow.hour", "afterglow")
end

-- Afterglow steps one hour per Super+T, from the hour it is heading to.
local function go_to_hour(id)
    local from, to
    for i, h in ipairs(HOURS) do
        if h.id == hour() then from = i end
        if h.id == id then to = i end
    end
    if not from or not to then return end
    for _ = 1, (to - from) % #HOURS do shell.key("Super+t") end
end

-- ---- system state (same tools as the Objects rice; all optional) --------

local override = {}

local function remember(key, value)
    override[key] = { value = value, at = os.time() }
end

local function live(key, polled)
    local o = override[key]
    if o and os.time() - o.at < 3 then return o.value end
    return polled
end

local sys = {}

function sys.volume()
    local out = shell.poll("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", 2000)
    local v = tonumber(out:match("Volume:%s*([%d%.]+)") or "")
    return live("volume", v and math.floor(v * 100 + 0.5)), live("mute", out:find("MUTED") ~= nil)
end

-- brightnessctl when installed, else sysfs + logind: no extra package.
local brightness_cmd = string.format("sh %q", shell.data_dir .. "/bin/shady-brightness")

function sys.brightness()
    local out = shell.poll(brightness_cmd .. " get 2>/dev/null", 3000)
    return live("brightness", tonumber(out))
end

function sys.battery()
    return tonumber(trim(shell.poll("cat /sys/class/power_supply/BAT*/capacity 2>/dev/null | head -n1", 30000)))
end

function sys.wifi()
    local state = trim(shell.poll("nmcli -t -f WIFI general 2>/dev/null", 3000))
    local ssid = trim(shell.poll("nmcli -t -f active,ssid device wifi 2>/dev/null | sed -n 's/^yes://p' | head -n1", 5000))
    return live("wifi", state == "enabled"), ssid, state ~= ""
end

function sys.bluetooth()
    local out = shell.poll("bluetoothctl show 2>/dev/null | grep -m1 Powered", 4000)
    return live("bluetooth", out:find("yes") ~= nil), out ~= ""
end

function sys.media()
    local out = shell.poll("playerctl metadata --format '{{status}}\t{{title}}\t{{artist}}' 2>/dev/null", 2000)
    local status, title, artist = out:match("^(%a+)\t([^\t]*)\t([^\n]*)")
    if not status then return nil end
    return { playing = live("playing", status == "Playing"), title = title, artist = trim(artist) }
end

local function set_volume(v)
    v = math.max(0, math.min(100, math.floor(v + 0.5)))
    remember("volume", v)
    shell.spawn(string.format("wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f", v / 100))
end

-- Screenshots (the Capture tile, Print) are saved by the compositor; say
-- where. Values replayed when the shell (re)connects are not news.
local loaded_at = os.time()
shell.on("value", function(key, path)
    if os.time() - loaded_at < 3 or not path then return end
    if key == "screenshot.saved" then
        local home = os.getenv("HOME") or ""
        if home ~= "" and path:sub(1, #home) == home then path = "~" .. path:sub(#home + 1) end
        shell.notify("Screenshot saved", path, "Capture")
    elseif key == "screenshot.failed" then
        shell.notify("Screenshot failed", "Could not write " .. path, "Capture")
    end
end)

local function set_brightness(v)
    v = math.max(1, math.min(100, math.floor(v + 0.5)))
    remember("brightness", v)
    shell.spawn(string.format("%s set %d", brightness_cmd, v))
end

-- ---- the sky: clock, horizon lights and reflected windows --------------

local function clock()
    local time = shell.date("%H:%M")
    return shell.column {
        left = 60, top = 30, align = "start", gap = 0,
        enter = { opacity = 0, translate = { 0, -14 } }, animate = "gentle",
        text(time, {
            key = "time:" .. time, font = serif(104), letter_spacing = -1,
            enter = { opacity = 0, translate = { 0, 10 } }, animate = "gentle",
        }),
        shell.row {
            gap = 10, align = "center",
            label(shell.date("%A %d %B"), { color = mix(c.text_dim, c.text, 0.3) }),
            text("·", { font = sans("Bold", 10), color = c.text_dim }),
            text(PHRASES[hour()] or "", {
                key = "phrase:" .. hour(), font = SERIF_ITALIC .. " 15", color = c.text,
                enter = { opacity = 0 }, animate = "gentle",
            }),
        },
    }
end

local function lights(hy)
    local active = shell.active_workspace()
    local busy = {}
    for _, w in ipairs(shell.windows()) do busy[w.workspace] = true end
    local items = {}
    for _, name in ipairs(shell.workspaces()) do
        local on = name == active
        items[#items + 1] = shell.column {
            id = "ws:" .. name, gap = 6, align = "center", padding = { 0, 6 },
            on_click = function() shell.workspace(name) end,
            label(name, {
                key = "ws:" .. name .. ":label", height = 16, animate = "smooth",
                font = sans(on and "ExtraBold" or "SemiBold", 8.5),
                color = on and c.text or a(c.text_dim, 0.85),
            }),
            shell.row {
                width = 24, height = 16, justify = "center",
                shell.pip {
                    key = "ws:" .. name .. ":light", animate = "smooth",
                    radius = on and 6 or 3.5, lit = on or busy[name] or false,
                    color = on and c.sun or c.horizon,
                },
            },
        }
    end
    -- The lights sit on the horizon: label (16) + gap (6) + half the pip row.
    return shell.row { key = "lights", top = hy - 30, gap = 52, align = "end", items }
end

local function reflections(hy)
    local active = shell.active_workspace()
    local items = {}
    for _, w in ipairs(shell.windows()) do
        if (w.workspace == "" or active == "" or w.workspace == active) and #items < 6 then
            local name = w.title ~= "" and w.title or (w.app_id ~= "" and w.app_id or "Window")
            items[#items + 1] = shell.row {
                id = "task:" .. w.id, gap = 10, align = "center", animate = "smooth",
                enter = { opacity = 0, translate = { 0, -10 } },
                opacity = w.focused and 1 or 0.62,
                hover = { opacity = 1 },
                on_click = function(button)
                    if button == "left" then shell.focus(w.id)
                    elseif button == "middle" then shell.close_window(w.id) end
                end,
                shell.pip { radius = 2.6, lit = w.focused, color = c.accent },
                text(name, { font = serif(19), max_width = 260 }),
            }
        end
    end
    return shell.row { key = "reflections", top = hy + 18, gap = 38, align = "center", items }
end

shell.popup {
    name = "horizon-desk",
    layer = "bottom",
    fill = true,
    persistent = true,
    outputs = "all",
    input = "widgets",
    view = function(ctx)
        local hy = math.floor(ctx.height * HORIZON)
        local glow = c.horizon
        -- The launcher takes the whole sky; the desk steps back meanwhile.
        local away = shell.is_open("horizon-launcher")
        local parts = {
            clock(),
            shell.box {
                key = "line", left = 0, right = 0, top = hy, height = 1,
                background = shell.gradient("horizontal", a(glow, 0), a(glow, 0.6), a(glow, 0.45), a(glow, 0)),
            },
            lights(hy),
            reflections(hy),
        }
        for _, part in ipairs(parts) do
            part.opacity = away and 0 or (part.opacity or 1)
            part.animate = part.animate or "smooth"
        end
        parts.justify = "center"
        return shell.stack(parts)
    end,
}

-- ---- notifications: words that appear in the sky ------------------------

shell.notification_server()

local function active_notifications(limit)
    local list = {}
    for _, n in ipairs(shell.notifications()) do
        if n.active and #list < limit then list[#list + 1] = n end
    end
    return list
end

local function sync_toasts()
    local want = #active_notifications(1) > 0
    if want and not shell.is_open("horizon-toasts") then
        shell.open("horizon-toasts")
    elseif not want and shell.is_open("horizon-toasts") then
        shell.close("horizon-toasts")
    end
end

-- The history outlives config reloads, so this also runs once below.
shell.on("notifications", sync_toasts)

local function activate(n)
    for _, action in ipairs(n.actions) do
        if action.key == "default" then
            shell.invoke_notification(n.id, "default")
            return
        end
    end
    shell.dismiss_notification(n.id)
end

local function sky_note(ctx, n)
    local id = "toast:" .. n.id
    local critical = n.urgency == "critical"
    local actions = {}
    for _, action in ipairs(n.actions) do
        if action.key ~= "default" and action.label ~= "" then
            local aid = id .. ":" .. action.key
            actions[#actions + 1] = shell.row {
                id = aid, radius = 9, padding = { 5, 11 }, animate = "smooth",
                border = a(c.text, ctx.hovered == aid and 0.6 or 0.28),
                background = ctx.hovered == aid and a(c.text, 0.12) or nil,
                on_click = function() shell.invoke_notification(n.id, action.key) end,
                text(action.label, { font = sans("SemiBold", 9) }),
            }
        end
    end
    return shell.column {
        key = id, id = id, width = 340, align = "end", gap = 4, padding = { 14, 18 }, radius = 18,
        background = shell.gradient("vertical", a(c.surface, 0.42), a(c.surface, 0.30)),
        border = critical and a(c.accent, 0.7) or a(c.text, 0.16),
        animate = "gentle", enter = { opacity = 0, translate = { 0, -14 } },
        hover = { border = a(c.text, 0.4) },
        on_click = function() activate(n) end,
        shell.row {
            gap = 8, align = "center",
            critical and shell.pip { radius = 3, lit = true, color = c.accent } or false,
            label(n.app ~= "" and n.app or "Notification"),
        },
        text(n.summary, { font = serif(22), max_width = 300, text_align = 1 }),
        n.body ~= "" and text(n.body, {
            font = sans("Medium", 9.5), color = a(c.text, 0.8), max_width = 300, text_align = 1,
            wrap = true, max_lines = 3,
        }) or false,
        #actions > 0 and shell.row { gap = 8, padding = { 6, 0, 0, 0 }, actions } or false,
    }
end

shell.popup {
    name = "horizon-toasts",
    anchor = { "top", "right" },
    margin = { top = 76, right = 44 },
    view = function(ctx)
        -- The control center lists them itself; step out of its way.
        if shell.is_open("horizon-control") then return shell.box { width = 1, height = 1 } end
        local notes = {}
        for _, n in ipairs(active_notifications(3)) do notes[#notes + 1] = sky_note(ctx, n) end
        return shell.column {
            gap = 12, align = "end",
            exit = { opacity = 0, translate = { 0, -10 } }, animate = "gentle",
            notes,
        }
    end,
}
sync_toasts()

-- ---- status: a line of text in the top-right sky -----------------------

local control_output

local function toggle_control(output)
    if shell.is_open("horizon-control") then
        shell.close("horizon-control")
    else
        control_output = output
        shell.open("horizon-control", { output = output })
    end
end

shell.popup {
    name = "horizon-status",
    layer = "top",
    anchor = { "top", "right" },
    margin = { top = 26, right = 40 },
    -- Windows open and maximize below the status line instead of under it,
    -- where its text would cover their title bars and close buttons.
    exclusive = 30,
    persistent = true,
    outputs = "all",
    view = function(ctx)
        local open = shell.is_open("horizon-control") and control_output == ctx.output
        local wifi, ssid = sys.wifi()
        local volume, muted = sys.volume()
        local battery = sys.battery()
        local tint = open and c.accent or c.text
        local function item(icon, value)
            return shell.row {
                gap = 7, align = "center",
                shell.icon(icon, { size = 15, stroke = 1.9, color = tint, animate = "smooth" }),
                value and text(value, { font = sans("SemiBold", 9.5), color = tint, animate = "smooth" }) or false,
            }
        end
        return shell.row {
            id = "status", gap = 22, padding = { 8, 12 }, radius = 12, align = "center",
            hover = { background = a(c.text, 0.08) }, animate = "smooth",
            enter = { opacity = 0 },
            on_click = function(button, info)
                if button == "left" then toggle_control(info.output) end
            end,
            wifi and item("wifi", ssid ~= "" and ssid or "Wi-Fi") or false,
            item((muted or volume == 0) and "mute" or "volume", volume and tostring(volume) or nil),
            battery and item("battery", battery .. "%") or false,
            item("sliders"),
        }
    end,
}

-- ---- control center: a slab of glass ------------------------------------

local TRACK = 316

local function slider(id, name, value, set)
    local v = value or 0
    local x = 8 + (TRACK - 16) * v / 100
    local sent = v
    return shell.column {
        gap = 8, align = "stretch", opacity = value and 1 or 0.45,
        shell.row {
            justify = "space-between",
            text(name, { font = sans("Medium", 9.5), color = c.text_dim }),
            text(value and tostring(value) or "—", { font = sans("SemiBold", 9.5), color = c.text_dim }),
        },
        shell.stack {
            id = id, width = TRACK, height = 18,
            on_drag = value and function(info)
                local next_value = math.max(0, math.min(100, (info.px - 8) / (TRACK - 16) * 100))
                if info.done or math.abs(next_value - sent) >= 2 then
                    sent = next_value
                    set(next_value)
                end
            end or nil,
            on_scroll = value and function(dy) set(v - dy / 2) end or nil,
            shell.box { left = 0, right = 0, top = 8, height = 2, radius = 1, background = a(c.text, 0.22) },
            shell.box {
                key = id .. ":fill", left = 0, top = 8, width = x, height = 2, radius = 1, animate = "snappy",
                background = shell.gradient("horizontal", c.accent_2, c.accent, c.sun),
            },
            shell.row {
                key = id .. ":knob", left = x - 9, top = 0, width = 18, height = 18, justify = "center",
                animate = "snappy",
                shell.pip { radius = 6, lit = true, color = c.sun },
            },
        },
    }
end

local function tile(id, title, detail, on, action, available)
    if available == false then return false end
    return shell.column {
        id = id, grow = 1, align = "start", gap = 3, padding = { 12, 14 }, radius = 14,
        background = on and a(c.accent, 0.24) or a(c.surface, 0.28),
        border = a(c.text, on and 0.45 or 0.16), animate = "smooth",
        hover = { background = on and a(c.accent, 0.34) or a(c.text, 0.10) },
        on_click = action,
        text(title, { font = sans("Bold", 10), color = on and c.text or a(c.text, 0.85) }),
        text(detail, { font = sans("Medium", 8.5), color = c.text_dim, max_width = 130 }),
    }
end

local function now_playing()
    local m = sys.media()
    if not m then
        return shell.row {
            gap = 12, align = "center", padding = { 14, 0, 0, 0 },
            shell.icon("music", { size = 18, color = c.text_dim, stroke = 1.8 }),
            text("Nothing playing", { font = SERIF_ITALIC .. " 15", color = c.text_dim }),
        }
    end
    return shell.row {
        gap = 14, align = "center", padding = { 14, 0, 0, 0 },
        shell.box {
            width = 48, height = 48, radius = 12,
            background = shell.gradient("diagonal", c.accent, c.sky_mid),
        },
        shell.column {
            grow = 1, shrink = 1, align = "start", gap = 2,
            text(m.title ~= "" and m.title or "Untitled", { font = serif(18) }),
            m.artist ~= "" and text(m.artist, { font = sans("Medium", 8.5), color = c.text_dim }) or false,
        },
        shell.row {
            id = "play", width = 38, height = 38, radius = 19, justify = "center",
            border = a(c.text, 0.5), background = a(c.text, 0.10), animate = "smooth",
            hover = { background = a(c.text, 0.2) },
            on_click = function()
                remember("playing", not m.playing)
                shell.spawn("playerctl play-pause")
                shell.redraw()
            end,
            shell.icon(m.playing and "pause" or "play", { size = 14, color = c.text, stroke = 1.8 }),
        },
    }
end

-- What the sky said lately: the last few notifications.
local function earlier()
    local list = shell.notifications()
    if #list == 0 then return false end
    local rows = {}
    for i = 1, math.min(3, #list) do
        local n = list[i]
        rows[#rows + 1] = shell.row {
            gap = 10, align = "center", opacity = n.active and 1 or 0.6,
            text(n.summary ~= "" and n.summary or n.app, { font = serif(16), max_width = 220 }),
            shell.spacer(),
            text(ago(os.time() - n.time), { font = sans("Medium", 8.5), color = c.text_dim }),
        }
    end
    return shell.column {
        gap = 8, align = "stretch",
        shell.box { height = 1, background = a(c.text, 0.16) },
        shell.row {
            align = "center", padding = { 6, 0, 0, 0 },
            label("Earlier"),
            shell.spacer(),
            shell.row {
                id = "c:clear", radius = 8, padding = { 3, 9 }, animate = "smooth",
                hover = { background = a(c.text, 0.12) },
                on_click = function() shell.clear_notifications() end,
                label("Clear", { color = c.text }),
            },
        },
        rows,
    }
end

shell.popup {
    name = "horizon-control",
    anchor = { "top", "right" },
    margin = { top = 72, right = 48 },
    view = function()
        local volume, muted = sys.volume()
        local brightness = sys.brightness()
        local wifi, ssid, has_wifi = sys.wifi()
        local bluetooth, has_bluetooth = sys.bluetooth()
        local now = hour()
        local hours = {}
        for _, h in ipairs(HOURS) do
            local on = h.id == now
            hours[#hours + 1] = shell.row {
                id = "hour:" .. h.id, grow = 1, height = 34, radius = 10, justify = "center",
                background = on and shell.gradient("vertical", c.horizon, c.accent) or nil,
                hover = not on and { background = a(c.text, 0.08) } or nil,
                animate = "smooth",
                on_click = function() go_to_hour(h.id) end,
                text(h.label, {
                    font = sans(on and "ExtraBold" or "SemiBold", 8.5),
                    color = on and c.surface or c.text_dim,
                }),
            }
        end
        return shell.column {
            width = 360, radius = 24, padding = 22, gap = 20, align = "stretch",
            background = shell.gradient("diagonal", a(c.text, 0.20), a(c.surface, 0.42), a(c.surface, 0.58)),
            border = a(c.text, 0.38),
            animate = "smooth",
            enter = { opacity = 0, translate = { 30, 0 }, scale = 0.97 },
            exit = { opacity = 0, translate = { 30, 0 }, scale = 0.97 },
            shell.row {
                align = "end",
                text("Tonight", { font = serif(30) }),
                shell.spacer(),
                label("Super+T"),
            },
            shell.row {
                gap = 6, padding = 4, radius = 14, align = "stretch",
                background = a(c.surface, 0.35),
                hours,
            },
            slider("c:volume", muted and "Volume · muted" or "Volume", volume, function(v)
                set_volume(v)
                shell.redraw()
            end),
            slider("c:light", "Brightness", brightness, function(v)
                set_brightness(v)
                shell.redraw()
            end),
            shell.column {
                gap = 10, align = "stretch",
                shell.row {
                    gap = 10, align = "stretch",
                    tile("t:wifi", "Wi-Fi", wifi and (ssid ~= "" and ssid or "On") or "Off", wifi, function()
                        remember("wifi", not wifi)
                        shell.spawn("nmcli radio wifi " .. (wifi and "off" or "on"))
                        shell.redraw()
                    end, has_wifi),
                    tile("t:bt", "Bluetooth", bluetooth and "On" or "Off", bluetooth, function()
                        remember("bluetooth", not bluetooth)
                        shell.spawn("bluetoothctl power " .. (bluetooth and "off" or "on"))
                        shell.redraw()
                    end, has_bluetooth),
                },
                shell.row {
                    gap = 10, align = "stretch",
                    tile("t:mute", "Sound", muted and "Muted" or "On", not muted, function()
                        remember("mute", not muted)
                        shell.spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")
                        shell.redraw()
                    end),
                    tile("t:shot", "Capture", "Save the screen", false, function()
                        shell.close("horizon-control")
                        -- The compositor saves the frame itself once the panel is gone.
                        shell.screenshot(350)
                    end),
                },
            },
            shell.column {
                align = "stretch",
                shell.box { height = 1, background = a(c.text, 0.16) },
                now_playing(),
            },
            earlier(),
        }
    end,
}

-- ---- launcher: name a star ----------------------------------------------

local sky = { query = "", selected = 1 }

local function matches()
    if sky.query == "" then return {} end
    return shell.apps(sky.query, 6)
end

local function launch(app)
    shell.close("horizon-launcher")
    shell.launch(app)
end

shell.on("launcher", function()
    sky.query, sky.selected = "", 1
    shell.close("horizon-control")
    shell.toggle("horizon-launcher")
end)

-- Every application gets a fixed place in the sky: a cell of a loose grid
-- above the horizon, nudged by a hash of its name.
local function place(index, count, name, w, h, hy)
    local cols = 8
    local rows = math.max(1, math.ceil(count / cols))
    local col, row = (index - 1) % cols, (index - 1) // cols
    local cell_w = w * 0.88 / cols
    local cell_h = (hy - h * 0.2 - h * 0.08) / rows
    local hsh = hash(name)
    local jx = ((hsh % 1000) / 1000 - 0.5) * 0.7
    local jy = (((hsh // 1000) % 1000) / 1000 - 0.5) * 0.6
    return w * 0.06 + (col + 0.5 + jx) * cell_w, h * 0.08 + (row + 0.5 + jy) * cell_h
end

local function star_key(app)
    return app.name .. "|" .. app.exec
end

local function star(app, x, y, rank, selected, dim)
    local radius = selected and 6 or (rank and 4 or 1.8)
    local name_props = selected and { font = serif(28) }
        or rank and { font = serif(19) }
        or { font = sans("Medium", 8.5), color = a(c.text_dim, dim and 0.35 or 0.6) }
    name_props.animate = "smooth"
    local width = rank and 360 or 180
    return shell.column {
        key = "star:" .. star_key(app), left = x - width / 2, top = y - radius * 3.2, width = width,
        align = "center", gap = 4, animate = "smooth",
        enter = { opacity = 0, scale = 0.3 }, exit = { opacity = 0 },
        on_click = function() launch(app) end,
        shell.row {
            width = radius * 6.4, height = radius * 6.4, justify = "center",
            shell.pip {
                key = "star:" .. star_key(app) .. ":light", radius = radius, lit = true, animate = "smooth",
                color = (selected or rank) and c.sun or a(c.text, dim and 0.45 or 0.75),
            },
        },
        text(app.name, name_props),
    }
end

-- A thin straight line between two points: a rotated hairline box.
local function line(key, x1, y1, x2, y2, color)
    local dx, dy = x2 - x1, y2 - y1
    local length = math.sqrt(dx * dx + dy * dy)
    return shell.box {
        key = key, left = (x1 + x2) / 2 - length / 2, top = (y1 + y2) / 2 - 0.6,
        width = length, height = 1.2, rotate = math.deg(math.atan(dy, dx)),
        background = color, animate = "smooth", enter = { opacity = 0 }, exit = { opacity = 0 },
    }
end

shell.popup {
    name = "horizon-launcher",
    fill = true,
    keyboard = "exclusive",
    text_input = true,
    view = function(ctx)
        local w, h = ctx.width, ctx.height
        local hy = math.floor(h * HORIZON)
        local all = shell.apps("", 40)
        local found = matches()
        if sky.selected > #found then sky.selected = math.max(#found, 1) end
        local rank, where = {}, {}
        for i, app in ipairs(found) do rank[star_key(app)] = i end
        for i, app in ipairs(all) do
            local x, y = place(i, #all, star_key(app), w, h, hy)
            where[star_key(app)] = { x, y }
        end
        -- A match outside the first 40 still gets a place.
        for i, app in ipairs(found) do
            if not where[star_key(app)] then
                where[star_key(app)] = { place(#all + i, #all + #found, star_key(app), w, h, hy) }
                all[#all + 1] = app
            end
        end

        local children = {
            shell.box {
                key = "veil", left = 0, right = 0, top = 0, bottom = 0,
                background = shell.gradient("vertical", a(c.sky_top, 0.55), a(c.sky_top, 0.25), a(c.sky_top, 0.55)),
                enter = { opacity = 0 }, exit = { opacity = 0 }, animate = "smooth",
                on_click = function() shell.close("horizon-launcher") end,
            },
        }
        for i = 2, #found do
            local p, q = where[star_key(found[i - 1])], where[star_key(found[i])]
            children[#children + 1] = line("link:" .. star_key(found[i - 1]) .. ">" .. star_key(found[i]),
                p[1], p[2], q[1], q[2], a(c.sun, 0.55))
        end
        if found[sky.selected] then
            local p = where[star_key(found[sky.selected])]
            children[#children + 1] = line("thread", p[1], p[2], w / 2, hy - 64, a(c.sun, 0.18))
        end
        for _, app in ipairs(all) do
            local p = where[star_key(app)]
            local r = rank[star_key(app)]
            children[#children + 1] = star(app, p[1], p[2], r, r == sky.selected, sky.query ~= "" and not r)
        end

        local chosen = found[sky.selected]
        children[#children + 1] = shell.column {
            key = "prompt", top = hy - 92, align = "center", gap = 10,
            enter = { opacity = 0, translate = { 0, 16 } }, exit = { opacity = 0 }, animate = "gentle",
            label("Name a star"),
            shell.row {
                gap = 4, align = "center", height = 64,
                text(sky.query, { font = serif(54) }),
                ctx.preedit and text(ctx.preedit, { font = SERIF_ITALIC .. " 54", color = c.accent }) or false,
                shell.box { width = 2, height = 50, background = c.accent, caret = true },
            },
        }
        local hints = {}
        local function hint(key_name, what)
            hints[#hints + 1] = shell.row {
                gap = 8, align = "center",
                text(key_name, { font = sans("ExtraBold", 9) }),
                text(what, { font = sans("Medium", 9), color = c.text_dim }),
            }
        end
        hint("↵", chosen and ("launch " .. chosen.name) or "launch")
        hint("Tab", "next star")
        hint("Esc", "back to the sea")
        children[#children + 1] = shell.row {
            key = "hints", top = hy + 28, gap = 34,
            enter = { opacity = 0 }, exit = { opacity = 0 },
            hints,
        }
        children.justify = "center"
        return shell.stack(children)
    end,
    on_key = function(key, typed)
        local found = matches()
        if key == "Escape" then
            shell.close("horizon-launcher")
        elseif key == "Return" or key == "KP_Enter" then
            if found[sky.selected] then launch(found[sky.selected]) end
        elseif key == "Tab" or key == "Right" or key == "Down" then
            if #found > 0 then sky.selected = sky.selected % #found + 1 end
        elseif key == "ISO_Left_Tab" or key == "Left" or key == "Up" then
            if #found > 0 then sky.selected = (sky.selected - 2) % #found + 1 end
        elseif key == "BackSpace" then
            local cut = utf8.offset(sky.query, -1)
            if cut then sky.query = sky.query:sub(1, cut - 1) end
            sky.selected = 1
        elseif typed ~= "" and #sky.query + #typed < 64 then
            sky.query = sky.query .. typed
            sky.selected = 1
        end
        shell.redraw()
    end,
}

-- ---- shortcuts (Super+Shift+/) -----------------------------------------
-- Another slab of the control center's glass, standing in the middle of the
-- sky.

require("shortcut_overlay").install {
    name = "horizon-shortcuts",
    palette = c,
    title = "Shortcuts",
    title_font = serif(30),
    font = sans("Medium", 9.5),
    bold = sans("Bold", 9.5),
    cap = function(p)
        return {
            radius = 8, font = sans("Bold", 8.5), color = p.text,
            background = a(p.surface, 0.35), border = a(p.text, 0.30),
        }
    end,
    frame = function(body)
        return shell.column {
            radius = 24, padding = 26, align = "stretch",
            background = shell.gradient("diagonal", a(c.text, 0.20), a(c.surface, 0.42), a(c.surface, 0.58)),
            border = a(c.text, 0.38),
            animate = "smooth",
            enter = { opacity = 0, translate = { 0, 24 }, scale = 0.97 },
            exit = { opacity = 0, translate = { 0, 24 }, scale = 0.97 },
            body,
        }
    end,
}
