-- Objects: shady-shell as chunky, tactile cards.
--
-- Everything here is a physical-looking object with a thick ink outline and
-- a hard offset shadow: a paper-sunset wallpaper, a desk clock made of two
-- number tiles, a floating dock at the bottom, a control center that leans
-- like a remote on the table, and a launcher that fans the applications out
-- like a hand of cards. The API is documented in docs/SHELL_LUA_API.md.

shell.fonts("fonts")

local INK, CREAM = "#241530", "#fff7ec"
local ORANGE, LILAC, MINT, BUTTER, SKY = "#ff7a3d", "#b9a3ff", "#8fe3c0", "#ffe08a", "#9fd3ff"
local PAPER = { "#ffd9bf", "#ffb99b", "#f59a8c" }
local CARDS = { LILAC, BUTTER, MINT, SKY, "#ffb3c7" }
local a = shell.alpha

local function font(weight, size)
    return "Bricolage Grotesque " .. weight .. " " .. size
end

-- The house style: ink outline and a hard shadow `depth` px down-right.
local function chunky(props, depth)
    props.border = props.border or INK
    props.border_width = props.border_width or 3
    if depth and depth > 0 then props.shadow = { depth, depth, INK } end
    return props
end

-- Lifts a little towards the pointer, its shadow growing to match.
local function lift(props, depth)
    props.animate = props.animate or "snappy"
    props.hover = { translate = { -2, -2 }, shadow = { depth + 2, depth + 2, INK } }
    return chunky(props, depth)
end

-- Text is ink unless it says otherwise.
local function text(s, props)
    props = props or {}
    props.color = props.color or INK
    return shell.text(s, props)
end

local function trim(s)
    return ((s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function hash(s)
    local h = 0
    for i = 1, #s do h = (h * 31 + s:byte(i)) % 997 end
    return h
end

local function initial(s)
    return ((s or ""):match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper()
end

-- ---- system state -------------------------------------------------------
-- Polled through ordinary commands; each is optional and the UI shows a
-- dash or hides a control when its tool is missing. Values set from the UI
-- are shown right away, before the next poll confirms them.

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

function sys.brightness()
    local out = shell.poll("brightnessctl -m 2>/dev/null", 3000)
    return live("brightness", tonumber(out:match(",(%d+)%%,") or ""))
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

local function set_brightness(v)
    v = math.max(1, math.min(100, math.floor(v + 0.5)))
    remember("brightness", v)
    shell.spawn(string.format("brightnessctl -q set %d%%", v))
end

-- ---- wallpaper ----------------------------------------------------------

shell.popup {
    name = "objects-wallpaper",
    layer = "background",
    fill = true,
    persistent = true,
    view = function(ctx)
        local w, h = ctx.width, ctx.height
        local sun = math.floor(h * 0.47)
        local function hill(color, size, left, top)
            return shell.box {
                left = left, top = top, width = size, height = size, radius = size / 2,
                background = color, border = INK, border_width = 3,
            }
        end
        return shell.stack {
            background = shell.gradient("vertical", PAPER[1], PAPER[2], PAPER[3]),
            shell.box {
                left = math.floor(w * 0.6), top = math.floor(h * 0.42), width = sun, height = sun,
                radius = sun / 2, background = BUTTER, border = INK, border_width = 3,
            },
            hill(LILAC, w * 1.5, -w * 0.35, h * 0.72),
            hill(MINT, w * 1.3, w * 0.38, h * 0.82),
        }
    end,
}

-- ---- desk clock ---------------------------------------------------------

local function tile(digits, background, color)
    return shell.row(chunky({
        width = 116, height = 132, radius = 18, justify = "center", background = background,
        text(digits, {
            key = "digits:" .. digits, font = font("ExtraBold", 60), color = color,
            letter_spacing = -1, ellipsize = false, enter = { scale = 0.4, opacity = 0, rotate = -12 }, animate = "bouncy",
        }),
    }, 6))
end

shell.popup {
    name = "objects-clock",
    layer = "bottom",
    anchor = { "top", "left" },
    margin = { top = 40, left = 44 },
    persistent = true,
    view = function()
        return shell.column {
            padding = { 4, 14, 16, 4 }, gap = 16, align = "start",
            enter = { translate = { -60, 0 }, opacity = 0 }, animate = "gentle",
            shell.row {
                gap = 12, align = "center",
                tile(shell.date("%H"), CREAM, INK),
                shell.column {
                    gap = 14,
                    shell.box { width = 12, height = 12, radius = 6, background = INK },
                    shell.box { width = 12, height = 12, radius = 6, background = INK },
                },
                tile(shell.date("%M"), ORANGE, CREAM),
            },
            shell.row {
                key = "date", rotate = -3, radius = 10, padding = { 6, 14 }, background = INK,
                animate = "bouncy", enter = { rotate = -20, opacity = 0, scale = 0.6 },
                text(shell.date("%a · %d %b"):upper(), { font = font("Bold", 15), color = BUTTER }),
            },
        }
    end,
}

-- ---- launcher: a fanned hand of cards -----------------------------------

-- The best match is dealt to the middle of the hand, the next ones
-- alternately to its right and left; `slot` is the chosen card's place,
-- 0 in the middle, negative to the left.
local deck = { query = "", slot = 0 }

local function deck_apps()
    return shell.apps(deck.query, 7)
end

local function slot_of(rank)
    return rank % 2 == 0 and rank // 2 or -(rank // 2)
end

local function rank_of(slot)
    return slot > 0 and slot * 2 or -slot * 2 + 1
end

-- Move the choice by `step` places, staying on cards that exist.
local function shift(step, count)
    local next_slot = deck.slot + step
    if rank_of(next_slot) <= count then deck.slot = next_slot end
end

local function launch(app)
    shell.close("launcher")
    shell.launch(app)
end

local function toggle_launcher()
    deck.query, deck.slot = "", 0
    shell.close("control")
    shell.close("objects-menu")
    shell.toggle("launcher")
end

shell.on("launcher", toggle_launcher)

local function card(app, index, selected, distance, x, y, angle)
    local w, h = selected and 190 or 150, selected and 264 or 210
    local color = selected and ORANGE or CARDS[hash(app.name) % #CARDS + 1]
    local ink = selected and CREAM or INK
    return shell.column(chunky({
        key = "card:" .. app.name,
        left = x - w / 2, top = y - h / 2, width = w, height = h, radius = selected and 22 or 18,
        rotate = angle, padding = selected and 18 or 14, justify = "space-between", align = "stretch",
        background = color, opacity = math.max(0.45, 1 - distance * 0.14),
        animate = "bouncy", enter = { opacity = 0, translate = { 0, 320 }, rotate = angle * 2 },
        exit = { opacity = 0, translate = { 0, 320 } },
        on_click = function()
            if selected then launch(app) else
                deck.slot = slot_of(index)
                shell.redraw()
            end
        end,
        shell.row {
            align = "start",
            text(initial(app.name), { font = font("ExtraBold", selected and 48 or 32), color = ink }),
            shell.spacer(),
            selected and shell.row {
                radius = 8, padding = { 3, 8 }, background = INK,
                text("↵", { font = font("Bold", 12), color = BUTTER }),
            } or false,
        },
        shell.column {
            align = "start", gap = 2,
            text(app.name, {
                font = font(selected and "ExtraBold" or "Bold", selected and 17 or 13), color = ink,
                max_width = w - 30,
            }),
            selected and text(app.exec, {
                font = font("Medium", 10), color = a(CREAM, 0.85), max_width = w - 36,
            }) or false,
        },
    }, selected and 8 or 5))
end

shell.popup {
    name = "launcher",
    fill = true,
    keyboard = "exclusive",
    view = function(ctx)
        local apps = deck_apps()
        if rank_of(deck.slot) > #apps then deck.slot = 0 end
        local w, h = ctx.width, ctx.height
        local cx, cy, radius = w / 2, h * 0.52, math.min(1100, w * 0.85)
        local children = {
            shell.box {
                key = "dim", left = 0, right = 0, top = 0, bottom = 0, background = a(INK, 0.55),
                enter = { opacity = 0 }, exit = { opacity = 0 },
                on_click = function() shell.close("launcher") end,
            },
            shell.row(chunky({
                key = "sticker", top = math.floor(h * 0.12), rotate = -3, radius = 22,
                padding = { 12, 24 }, gap = 14, background = BUTTER, align = "center",
                animate = "bouncy", enter = { translate = { 0, -200 }, rotate = -15 },
                exit = { translate = { 0, -200 }, opacity = 0 },
                shell.icon("search", { size = 26, color = INK, stroke = 2.8 }),
                deck.query ~= "" and shell.row {
                    gap = 3, align = "center",
                    text(deck.query, { font = font("ExtraBold", 34) }),
                    shell.box { width = 4, height = 34, radius = 2, background = ORANGE },
                } or text("Pick a card", { font = font("Bold", 26), color = a(INK, 0.6) }),
            }, 6)),
        }
        if #apps == 0 then
            children[#children + 1] = shell.row(chunky({
                key = "empty", top = math.floor(cy - 40), radius = 18, padding = { 16, 22 },
                background = CREAM, enter = { scale = 0.6, opacity = 0 }, animate = "bouncy",
                text("No card matches “" .. deck.query .. "”", { font = font("Bold", 18) }),
            }, 6))
        end
        -- Cards far from the chosen one first, so it lands on top; it rises
        -- out of the hand and straightens a little.
        local order = {}
        for i = 1, #apps do order[i] = i end
        table.sort(order, function(p, q)
            return math.abs(slot_of(p) - deck.slot) > math.abs(slot_of(q) - deck.slot)
        end)
        for _, i in ipairs(order) do
            local slot = slot_of(i)
            local d = math.abs(slot - deck.slot)
            local angle = slot * 9
            local r = math.rad(angle)
            local x = cx + radius * math.sin(r)
            local y = cy + radius * (1 - math.cos(r)) + (d == 0 and -56 or 0)
            children[#children + 1] = card(apps[i], i, d == 0, d, x, y, d == 0 and angle / 2 or angle)
        end
        local hints = {}
        for _, hint in ipairs({ "← → shuffle", "↵ play the card", "Esc fold" }) do
            hints[#hints + 1] = shell.row(chunky({
                radius = 12, padding = { 7, 14 }, background = CREAM,
                text(hint, { font = font("Bold", 14) }),
            }))
        end
        children[#children + 1] = shell.row {
            key = "hints", bottom = 130, gap = 12,
            enter = { opacity = 0, translate = { 0, 40 } }, exit = { opacity = 0 },
            hints,
        }
        children.justify = "center"
        return shell.stack(children)
    end,
    on_key = function(key, text)
        local apps = deck_apps()
        if key == "Escape" then
            shell.close("launcher")
        elseif key == "Return" or key == "KP_Enter" then
            local app = apps[rank_of(deck.slot)]
            if app then launch(app) end
        elseif key == "Left" or key == "Up" or key == "ISO_Left_Tab" then
            shift(-1, #apps)
        elseif key == "Right" or key == "Down" or key == "Tab" then
            shift(1, #apps)
        elseif key == "BackSpace" then
            local cut = utf8.offset(deck.query, -1)
            if cut then deck.query = deck.query:sub(1, cut - 1) end
            deck.slot = 0
        elseif text ~= "" and #deck.query + #text < 64 then
            deck.query = deck.query .. text
            deck.slot = 0
        end
        shell.redraw()
    end,
}

-- ---- control center -----------------------------------------------------

local TRACK = 206 -- slider track width

local function round_toggle(id, icon, label, on, color, action, available)
    if available == false then return false end
    local ink = on and color == ORANGE and CREAM or INK
    return shell.column {
        gap = 7, align = "center",
        shell.row {
            id = id, width = 64, height = 64, radius = 32, justify = "center",
            background = on and color or CREAM, border = INK, border_width = 3,
            shadow = on and { 0, 4, INK } or false, translate = on and { 0, -2 } or { 0, 0 },
            animate = "bouncy", hover = { scale = 1.06 },
            on_click = action,
            shell.icon(icon, { size = 26, color = ink, stroke = 2.4, animate = "snappy" }),
        },
        text(label, { font = font("Bold", 12) }),
    }
end

local function slider(id, icon, value, color, set)
    local v = value or 0
    local knob = 15 + (TRACK - 30) * v / 100
    local sent = v
    local function from(info, done)
        local next_value = math.floor((info.px - 15) / (TRACK - 30) * 100 + 0.5)
        next_value = math.max(0, math.min(100, next_value))
        if done or math.abs(next_value - sent) >= 2 then
            sent = next_value
            set(next_value)
        end
    end
    return shell.row {
        gap = 12, align = "center", opacity = value and 1 or 0.4,
        shell.icon(icon, { size = 22, color = INK, stroke = 2.4 }),
        shell.stack {
            id = id, width = TRACK, height = 30,
            on_drag = value and function(info) from(info, info.done) end or nil,
            on_scroll = value and function(dy) set(v - dy / 2) end or nil,
            shell.box { left = 0, right = 0, top = 4, height = 22, radius = 11, background = CREAM,
                border = INK, border_width = 3 },
            shell.box { key = id .. ":fill", left = 0, top = 4, width = math.max(22, knob + 4), height = 22,
                radius = 11, background = color, border = INK, border_width = 3, animate = "snappy" },
            shell.box { key = id .. ":knob", left = knob - 15, top = 0, width = 30, height = 30, radius = 15,
                background = BUTTER, border = INK, border_width = 3, animate = "snappy" },
        },
        text(value and tostring(value) or "—", { font = font("Bold", 14), width = 30, text_align = 1 }),
    }
end

local function media_card()
    local m = sys.media()
    if not m then
        return shell.row {
            radius = 20, padding = { 14, 16 }, gap = 12, background = INK, align = "center",
            shell.icon("music", { size = 22, color = a(CREAM, 0.7), stroke = 2.2 }),
            text("Nothing playing", { font = font("Bold", 14), color = a(CREAM, 0.75) }),
        }
    end
    local function button(icon, command, big)
        local size = big and 46 or 34
        return shell.row {
            width = size, height = size, radius = size / 2, justify = "center",
            background = big and BUTTER or a(CREAM, 0.12), border = big and CREAM or nil, border_width = 3,
            animate = "snappy", hover = { scale = 1.08 },
            on_click = function()
                if command == "play-pause" then remember("playing", not m.playing) end
                shell.spawn("playerctl " .. command)
                shell.redraw()
            end,
            shell.icon(icon, { size = big and 18 or 16, color = big and INK or CREAM, stroke = 2.2 }),
        }
    end
    return shell.row {
        radius = 20, padding = 12, gap = 10, background = INK, align = "center",
        shell.row {
            width = 52, height = 52, radius = 14, justify = "center", background = ORANGE,
            border = CREAM, border_width = 3,
            text(initial(m.title), { font = font("ExtraBold", 22), color = CREAM }),
        },
        shell.column {
            grow = 1, shrink = 1, align = "start", gap = 2,
            text(m.title ~= "" and m.title or "Untitled", { font = font("ExtraBold", 15), color = CREAM }),
            m.artist ~= "" and text(m.artist, { font = font("Medium", 12), color = a(CREAM, 0.75) }) or false,
        },
        button("previous", "previous"),
        button(m.playing and "pause" or "play", "play-pause", true),
        button("next", "next"),
    }
end

local control_output

local function toggle_control(output)
    shell.close("objects-menu")
    if shell.is_open("control") then
        shell.close("control")
    else
        control_output = output
        shell.open("control", { output = output })
    end
end

shell.popup {
    name = "control",
    anchor = { "bottom", "right" },
    margin = { bottom = 104, right = 18 },
    view = function()
        local volume, muted = sys.volume()
        local brightness = sys.brightness()
        local wifi, ssid, has_wifi = sys.wifi()
        local bluetooth, has_bluetooth = sys.bluetooth()
        local battery = sys.battery()
        return shell.row {
            padding = { 12, 24, 24, 12 },
            shell.column(chunky({
                width = 380, radius = 30, padding = { 16, 20, 20, 20 }, gap = 20, align = "stretch",
                background = CREAM, rotate = 2,
                animate = "bouncy",
                enter = { translate = { 30, 80 }, rotate = 10, opacity = 0, scale = 0.9 },
                exit = { translate = { 20, 60 }, rotate = 8, opacity = 0, scale = 0.92 },
                shell.row { justify = "center", shell.box { width = 60, height = 8, radius = 4, background = INK } },
                shell.row {
                    justify = "space-between", align = "start",
                    round_toggle("c:wifi", "wifi", ssid ~= "" and ssid or "Wi-Fi", wifi, ORANGE, function()
                        remember("wifi", not wifi)
                        shell.spawn("nmcli radio wifi " .. (wifi and "off" or "on"))
                        shell.redraw()
                    end, has_wifi),
                    round_toggle("c:bt", "bluetooth", "Bluetooth", bluetooth, LILAC, function()
                        remember("bluetooth", not bluetooth)
                        shell.spawn("bluetoothctl power " .. (bluetooth and "off" or "on"))
                        shell.redraw()
                    end, has_bluetooth),
                    round_toggle("c:mute", muted and "mute" or "volume", muted and "Muted" or "Sound",
                        muted, MINT, function()
                            remember("mute", not muted)
                            shell.spawn("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle")
                            shell.redraw()
                        end),
                    round_toggle("c:shot", "capture", "Capture", false, BUTTER, function()
                        shell.close("control")
                        shell.spawn("mkdir -p ~/Pictures && grim ~/Pictures/shady-$(date +%Y%m%d-%H%M%S).png")
                    end),
                },
                shell.column {
                    gap = 12, align = "start",
                    slider("c:volume", muted and "mute" or "volume", volume, ORANGE, function(v)
                        set_volume(v)
                        shell.redraw()
                    end),
                    slider("c:light", "sun", brightness, MINT, function(v)
                        set_brightness(v)
                        shell.redraw()
                    end),
                },
                media_card(),
                shell.row {
                    gap = 10, align = "center",
                    battery and shell.row(chunky({
                        radius = 12, padding = { 6, 12 }, gap = 8, background = battery <= 20 and ORANGE or MINT,
                        shell.icon("battery", { size = 20, color = INK, stroke = 2.2 }),
                        text(battery .. "%", { font = font("Bold", 13) }),
                    })) or false,
                    shell.spacer(),
                    shell.row(lift({
                        id = "c:quit", radius = 12, padding = { 6, 12 }, gap = 8, background = CREAM,
                        on_click = function() shell.quit() end,
                        shell.icon("power", { size = 18, color = INK, stroke = 2.4 }),
                        text("Quit", { font = font("Bold", 13) }),
                    }, 3)),
                },
            }, 8)),
        }
    end,
}

-- ---- window menu --------------------------------------------------------

local function find_window(id)
    for _, w in ipairs(shell.windows()) do
        if w.id == id then return w end
    end
end

local menu_window

shell.on("change", function()
    if shell.is_open("objects-menu") and not find_window(menu_window) then shell.close("objects-menu") end
end)

shell.popup {
    name = "objects-menu",
    anchor = { "bottom" },
    margin = { bottom = 104 },
    view = function(ctx, args)
        local w = find_window(args.window) or { id = args.window, workspace = "", title = "" }
        local function item(id, label, fn, danger)
            local hovered = ctx.hovered == id
            return shell.row {
                id = id, height = 38, radius = 12, padding = { 0, 14 }, gap = 10,
                background = hovered and (danger and ORANGE or BUTTER) or nil,
                border = hovered and INK or nil, border_width = 3, animate = "snappy",
                on_click = function()
                    fn(w.id)
                    shell.close("objects-menu")
                end,
                text(label, { font = font("Bold", 14), color = INK }),
            }
        end
        local rows = {
            text(w.title ~= "" and w.title or "Window", {
                font = font("ExtraBold", 15), max_width = 220, color = INK,
            }),
            item("m:focus", "Focus", shell.focus),
            item("m:max", w.maximized and "Restore" or "Maximize", shell.maximize),
            item("m:full", w.fullscreen and "Leave fullscreen" or "Fullscreen", shell.fullscreen),
        }
        for _, name in ipairs(shell.workspaces()) do
            if name ~= w.workspace then
                rows[#rows + 1] = item("m:ws:" .. name, "Move to " .. name,
                    function(id) shell.move(id, name) end)
            end
        end
        rows[#rows + 1] = item("m:close", "Close", shell.close_window, true)
        return shell.row {
            padding = { 4, 14, 14, 4 },
            shell.column(chunky({
                width = 250, radius = 22, padding = 12, gap = 4, align = "stretch", background = CREAM,
                animate = "bouncy", enter = { translate = { 0, 40 }, opacity = 0, scale = 0.9 },
                exit = { translate = { 0, 30 }, opacity = 0, scale = 0.94 },
                rows,
            }, 6)),
        }
    end,
}

-- ---- dock ---------------------------------------------------------------

local function divider()
    return shell.box { width = 3, height = 36, radius = 2, background = INK }
end

local function workspace_buttons()
    local active = shell.active_workspace()
    local buttons = {}
    for i, name in ipairs(shell.workspaces()) do
        local on = name == active
        buttons[#buttons + 1] = shell.row(lift({
            id = "ws:" .. name, height = 44, min_width = 44, radius = 14, justify = "center",
            padding = { 0, on and 16 or 0 }, background = on and ORANGE or CREAM, animate = "bouncy",
            on_click = function(button) if button == "left" then shell.workspace(name) end end,
            text(on and name or tostring(i), {
                key = "ws:" .. name .. (on and ":on" or ":off"),
                font = font("ExtraBold", 17), color = on and CREAM or INK,
                enter = { opacity = 0, scale = 0.5 }, animate = "bouncy",
            }),
        }, 0))
    end
    return shell.row { gap = 8, align = "center", buttons }
end

local function tasks()
    local active = shell.active_workspace()
    local avatars, focused = {}, nil
    for _, w in ipairs(shell.windows()) do
        if w.workspace == "" or active == "" or w.workspace == active then
            local name = w.app_id ~= "" and w.app_id or w.title
            if w.focused then focused = w end
            avatars[#avatars + 1] = shell.row {
                id = "task:" .. w.id, width = 50, height = 50, radius = 25, justify = "center",
                background = CARDS[hash(name) % #CARDS + 1], border = INK, border_width = 3,
                translate = w.focused and { 0, -8 } or { 0, 0 },
                shadow = w.focused and { 0, 5, INK } or false,
                animate = "bouncy", enter = { scale = 0, opacity = 0 },
                hover = { translate = { 0, w.focused and -10 or -4 } },
                on_click = function(button, info)
                    if button == "left" then
                        shell.close("objects-menu")
                        shell.focus(w.id)
                    elseif button == "middle" then
                        shell.close_window(w.id)
                    elseif button == "right" then
                        shell.close("control")
                        menu_window = w.id
                        shell.open("objects-menu", { output = info.output, window = w.id })
                    end
                end,
                text(initial(name), { font = font("ExtraBold", 20), color = INK }),
            }
        end
    end
    if #avatars == 0 then
        return text("Nothing open", { font = font("Bold", 14), color = a(INK, 0.55) })
    end
    return shell.row {
        gap = 10, align = "end", height = 50,
        avatars,
        focused and text(focused.title ~= "" and focused.title or focused.app_id, {
            key = "title", font = font("Bold", 14), max_width = 220,
            enter = { opacity = 0, translate = { -10, 0 } }, animate = "smooth",
        }) or false,
    }
end

local function status(ctx)
    local open = shell.is_open("control") and control_output == ctx.output
    local wifi = sys.wifi()
    local volume, muted = sys.volume()
    local battery = sys.battery()
    return shell.row(lift({
        id = "status", height = 44, radius = 14, padding = { 0, 14 }, gap = 10,
        background = open and ORANGE or INK, animate = "snappy",
        on_click = function(button, info)
            if button == "left" then toggle_control(info.output) end
        end,
        wifi and shell.icon("wifi", { size = 18, color = CREAM, stroke = 2.6 }) or false,
        shell.icon((muted or volume == 0) and "mute" or "volume", { size = 18, color = CREAM, stroke = 2.4 }),
        text(shell.date("%H:%M"), { font = font("ExtraBold", 15), color = CREAM }),
        battery and text(battery .. "%", { font = font("Bold", 13), color = a(CREAM, 0.8) }) or false,
    }, 0))
end

shell.popup {
    name = "dock",
    anchor = { "bottom" },
    margin = { bottom = 14 },
    layer = "top",
    persistent = true,
    exclusive = 84,
    view = function(ctx)
        return shell.row {
            padding = { 14, 12, 12, 6 },
            shell.row(chunky({
                radius = 26, padding = { 10, 12 }, gap = 14, background = CREAM, align = "center",
                animate = "gentle", enter = { translate = { 0, 120 } },
                shell.row(lift({
                    id = "deck", width = 44, height = 44, radius = 14, justify = "center", background = BUTTER,
                    on_click = function() toggle_launcher() end,
                    shell.icon("grid", { size = 22, color = INK, stroke = 2.6 }),
                }, 0)),
                workspace_buttons(),
                divider(),
                tasks(),
                divider(),
                status(ctx),
            }, 6)),
        }
    end,
}
