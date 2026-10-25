-- The Tide greeter's login card, over the same night sea as Tide's lock
-- screen. Authentication goes through the greetd shell plugin
-- (greeter/src/greetd.c); see docs/GREETER.md.
--
-- Keys: type the password and press Enter. Up/Down pick the person, Tab the
-- session, Escape clears what was typed.

local HERE = (debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$")) or "."
local ROOT = HERE .. "/../.."
shell.fonts(ROOT .. "/rices/horizon/fonts")

local a, mix = shell.alpha, shell.mix
local t = shell.theme
local SANS = "Manrope"
local function font(weight, size) return SANS .. " " .. weight .. " " .. size end
local P = { text = t.text, dim = t.text_dim, accent = t.accent, accent_2 = t.accent_2,
    surface = t.surface, danger = t.danger, base = "#11111B" }
local hint = os.getenv("SHADY_GREETER_HINT") or ""

local function text(s, props)
    props = props or {}
    props.color = props.color or P.text
    props.font = props.font or font("Medium", 10)
    return shell.text(s, props)
end
local function glass(alpha)
    return shell.gradient("vertical", a(mix(P.surface, "#FFFFFF", 0.04), alpha), a(P.surface, alpha))
end

local ok, err = shell.plugin("greetd")
if not ok then shell.log("greeter: " .. tostring(err)) end

local function rows(value)
    local list = {}
    for line in (value or ""):gmatch("[^\n]+") do
        local first, second = line:match("^([^\t]*)\t(.*)$")
        list[#list + 1] = { first or line, second or line }
    end
    return list
end
local function users()
    local list = {}
    for _, r in ipairs(rows(shell.value("greetd.users", ""))) do list[#list + 1] = { name = r[1], display = r[2] } end
    return list
end
local function sessions()
    local list = {}
    for _, r in ipairs(rows(shell.value("greetd.sessions", ""))) do list[#list + 1] = { name = r[1], id = r[2] } end
    return list
end
local function index_of(list, field, value, fallback)
    for i, item in ipairs(list) do if item[field] == value then return i end end
    return fallback
end

local login = { user = nil, session = nil, typed = "", started = false, shake = 0 }

local function selection()
    local people, kinds = users(), sessions()
    if not login.user then
        login.user = index_of(people, "name", shell.value("greetd.last_user", ""), 1)
    end
    if not login.session then
        login.session = index_of(kinds, "id", shell.value("greetd.last_session", ""),
            index_of(kinds, "id", "shady", 1))
    end
    login.user = math.max(1, math.min(login.user, math.max(#people, 1)))
    login.session = math.max(1, math.min(login.session, math.max(#kinds, 1)))
    return people[login.user], kinds[login.session], people, kinds
end

local function state() return shell.value("greetd.state", "idle") end

local function submit()
    local person, kind = selection()
    if not person then return end
    if state() == "prompt" and shell.value("greetd.prompt", "") ~= "" and login.started then
        shell.action("greetd.respond", login.typed)
    else
        if kind then shell.action("greetd.session", kind.id) end
        shell.action("greetd.login", person.name)
        shell.action("greetd.respond", login.typed)
        login.started = true
    end
    login.typed = ""
end

local function cycle(field, list, step)
    if #list > 0 then login[field] = (login[field] - 1 + step) % #list + 1 end
end

-- A failed attempt shakes the field.
local last_message = ""
shell.on("value", function(k, v)
    if k == "greetd.message" and v and v ~= last_message then
        last_message = v
        if shell.value("greetd.message_kind", "info") == "error" then login.shake = login.shake + 1 end
    end
end)

-- The night sea, drawn once.
shell.popup {
    name = "tide-sea",
    layer = "background",
    fill = true,
    outputs = "all",
    shader = shell.shader(ROOT .. "/rices/tide/shaders/tide.frag"),
    uniforms = { accent = P.accent, accent_2 = P.accent_2, base = "#0F0F1A" },
    view = function(ctx) return shell.box { width = ctx.width, height = ctx.height } end,
}

local function initial(name)
    return ((name or "?"):match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper()
end

local function pill(ctx, id, label, on_click, danger)
    local hovered = ctx.hovered == id
    return shell.row {
        id = id, height = 32, radius = 16, padding = { 0, 14 }, gap = 6, animate = "snappy",
        background = hovered and (danger and a(P.danger, 0.22) or a(P.text, 0.12)) or a(P.text, 0.05),
        border = a(P.text, hovered and 0.18 or 0.08),
        on_click = on_click,
        text(label, { font = font("SemiBold", 9), color = a(P.text, hovered and 1 or 0.75) }),
    }
end

shell.popup {
    name = "tide-login",
    persistent = true,
    keyboard = "exclusive",
    layer = "overlay",
    view = function(ctx)
        local person, kind, people, kinds = selection()
        local current = state()
        local busy = current == "busy" or current == "done"
        local secret = shell.value("greetd.secret", "1") == "1"
        local prompt = shell.value("greetd.prompt", "")
        if current ~= "prompt" or prompt == "" or prompt:lower():match("^password:?%s*$") then prompt = "Password" end

        local inside
        if busy then
            inside = text(current == "done" and "Starting your session…" or "Checking…",
                { font = font("Medium", 10.5), color = P.dim })
        elseif login.typed == "" then
            inside = shell.row { gap = 10, align = "center",
                shell.icon("lock", { size = 15, color = P.dim }),
                text(prompt, { font = font("Medium", 10.5), color = a(P.dim, 0.85) }) }
        elseif secret then
            local dots = {}
            for i = 1, math.min(utf8.len(login.typed) or #login.typed, 24) do
                dots[#dots + 1] = shell.box { key = "dot:" .. i, width = 8, height = 8, radius = 4,
                    background = P.text, enter = { opacity = 0, scale = 0.2 }, animate = "snappy" }
            end
            inside = shell.row { gap = 8, align = "center", dots }
        else
            inside = text(login.typed, { font = font("SemiBold", 10.5) })
        end
        local error = shell.value("greetd.message_kind", "info") == "error"
        local message = shell.value("greetd.message", "")

        local people_row = {}
        if #people > 1 then
            for i, p in ipairs(people) do
                local id = "user:" .. i
                local on = i == login.user
                people_row[#people_row + 1] = shell.row {
                    id = id, width = 30, height = 30, radius = 15, justify = "center", animate = "snappy",
                    background = on and shell.gradient("diagonal", P.accent, P.accent_2) or a(P.text, ctx.hovered == id and 0.14 or 0.07),
                    on_click = function()
                        login.user, login.typed, login.started = i, "", false
                        shell.action("greetd.cancel")
                        shell.redraw()
                    end,
                    text(initial(p.display), { font = font("ExtraBold", 9), color = on and P.base or P.text }),
                }
            end
        end

        return shell.column {
            align = "center", gap = 0, padding = 24,
            text(shell.date("%H:%M"), { font = SANS .. " ExtraLight 88", letter_spacing = -2 }),
            text(shell.date("%A, %d %B"), { font = font("Medium", 12.5), color = a(P.text, 0.8) }),
            shell.box { height = 40 },
            shell.column {
                width = 360, radius = 28, padding = { 28, 28, 22, 28 }, gap = 0, align = "center",
                background = glass(0.62), border = a(P.text, 0.10),
                shell.row { width = 76, height = 76, radius = 38, justify = "center",
                    background = shell.gradient("diagonal", P.accent, P.accent_2),
                    text(initial(person and person.display), { font = font("ExtraBold", 26), color = P.base }) },
                shell.box { height = 12 },
                text(person and person.display or "No accounts", { font = font("Bold", 14) }),
                (person and person.display ~= person.name) and
                    text(person.name, { font = font("Medium", 9), color = P.dim }) or false,
                #people_row > 0 and shell.box { height = 10 } or false,
                #people_row > 0 and shell.row { gap = 6, people_row } or false,
                shell.box { height = 18 },
                shell.row {
                    key = "field:" .. login.shake, width = 300, height = 48, radius = 24, padding = { 0, 20 },
                    align = "center", justify = (login.typed ~= "" and secret) and "center" or "start",
                    background = a(P.base, 0.55), border = error and a(P.danger, 0.7) or a(P.accent, busy and 0.2 or 0.45),
                    enter = login.shake > 0 and { translate = { 14, 0 } } or nil, animate = "bouncy",
                    inside,
                },
                shell.box { height = 10 },
                text(message ~= "" and message or " ", { font = font("Medium", 9), color = error and P.danger or P.dim,
                    max_width = 300, wrap = true, max_lines = 2, text_align = 0.5 }),
                shell.box { height = 8 },
                shell.row {
                    id = "session", height = 32, radius = 16, padding = { 0, 14 }, gap = 8, align = "center",
                    background = a(P.text, ctx.hovered == "session" and 0.10 or 0.05),
                    on_click = function() cycle("session", kinds, 1); shell.redraw() end,
                    shell.icon("grid", { size = 12, color = P.accent }),
                    text(kind and kind.name or "No sessions", { font = font("SemiBold", 9) }),
                    text("Tab", { font = font("Bold", 7.5), color = a(P.dim, 0.8) }),
                },
            },
            hint ~= "" and shell.box { height = 14 } or false,
            hint ~= "" and shell.row { radius = 12, padding = { 8, 14 },
                background = a(P.base, 0.45), border = a(P.accent, 0.35),
                text(hint, { font = font("Medium", 9.5) }) } or false,
            shell.box { height = 22 },
            shell.row { gap = 10,
                pill(ctx, "power:suspend", "Suspend", function() shell.spawn("systemctl suspend") end),
                pill(ctx, "power:restart", "Restart", function() shell.spawn("systemctl reboot") end),
                pill(ctx, "power:off", "Shut down", function() shell.spawn("systemctl poweroff") end, true),
            },
        }
    end,
    on_key = function(key, typed)
        local current = state()
        if current == "busy" or current == "done" then return end
        local _, _, people, kinds = selection()
        if key == "Return" or key == "KP_Enter" then
            submit()
        elseif key == "Escape" then
            login.typed, login.started = "", false
            shell.action("greetd.cancel")
        elseif key == "BackSpace" then
            local cut = utf8.offset(login.typed, -1)
            if cut then login.typed = login.typed:sub(1, cut - 1) end
        elseif key == "Up" or key == "Down" then
            cycle("user", people, key == "Down" and 1 or -1)
            login.typed, login.started = "", false
            shell.action("greetd.cancel")
        elseif key == "Tab" then
            cycle("session", kinds, 1)
        elseif key == "ISO_Left_Tab" then
            cycle("session", kinds, -1)
        elseif typed ~= "" and #login.typed + #typed <= 256 then
            login.typed = login.typed .. typed
        end
        shell.redraw()
    end,
}
