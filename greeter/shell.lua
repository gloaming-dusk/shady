-- The login screen: Tide before anyone is logged in. The same night sea and
-- glass islands along the top edge as the desktop, and the clock, avatar and
-- password field exactly where the lock screen has them, so that logging in
-- only fills in the windows. Authentication goes through the greetd shell
-- plugin (greeter/src/greetd.c); see docs/GREETER.md.
--
-- Keys: type the password and press Enter. Up/Down pick the person, Tab the
-- session, Escape clears what was typed.

local HERE = (debug.getinfo(1, "S").source:match("^@(.*)/[^/]*$")) or "."
local TIDE = HERE .. "/../rices/tide"
shell.fonts(TIDE .. "/fonts")

local a = shell.alpha
local S = dofile(TIDE .. "/style.lua")()
local P, SANS, font, text, glass, island = S.P, S.SANS, S.font, S.text, S.glass, S.island
local hint = os.getenv("SHADY_GREETER_HINT") or ""

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

local function choose_user(index)
    login.user, login.typed, login.started = index, "", false
    shell.action("greetd.cancel")
    shell.redraw()
end

-- A failed attempt shakes the field.
local last_message = ""
shell.on("value", function(k, v)
    if k == "greetd.message" and v and v ~= last_message then
        last_message = v
        if shell.value("greetd.message_kind", "info") == "error" then login.shake = login.shake + 1 end
    end
end)

-- The night sea on every screen, drawn once.
shell.popup {
    name = "tide-sea",
    layer = "background",
    fill = true,
    outputs = "all",
    shader = shell.shader(TIDE .. "/shaders/tide.frag"),
    uniforms = S.SEA_UNIFORMS,
    view = function(ctx) return shell.box { width = ctx.width, height = ctx.height } end,
}

local function initial(name)
    return ((name or "?"):match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper()
end

local function avatar(size, name)
    return shell.row { width = size, height = size, radius = size / 2, justify = "center",
        background = shell.gradient("diagonal", P.accent, P.accent_2),
        text(initial(name), { font = font("ExtraBold", math.floor(size * 0.34)), color = P.base }) }
end

-- The left island: which session to start.
local function session_island(ctx, kind)
    local hovered = ctx.hovered == "session"
    return island {
        id = "session", gap = 8, on_click = function()
            cycle("session", sessions(), 1)
            shell.redraw()
        end,
        shell.icon("grid", { size = 13, color = P.accent }),
        text(kind and kind.name or "No sessions", { font = font("SemiBold", 9),
            color = a(P.text, hovered and 1 or 0.85) }),
        text("Tab", { font = font("Bold", 7.5), color = a(P.dim, 0.8) }),
    }
end

-- The right island: leaving without logging in.
local function power_button(ctx, id, icon, label, command, danger)
    local hovered = ctx.hovered == id
    return shell.row {
        id = id, height = 24, radius = 12, padding = { 0, 8 }, gap = 5, align = "center", animate = "snappy",
        background = hovered and (danger and a(P.danger, 0.22) or a(P.text, 0.10)) or nil,
        on_click = function() shell.spawn(command) end,
        shell.icon(icon, { size = 12, color = hovered and danger and P.danger or P.dim }),
        text(label, { font = font("SemiBold", 8.5), color = a(P.text, hovered and 1 or 0.72) }),
    }
end

local function power_island(ctx)
    return island { padding = { 0, 4 }, gap = 2,
        power_button(ctx, "power:suspend", "moon", "Suspend", "systemctl suspend"),
        power_button(ctx, "power:restart", "sliders", "Restart", "systemctl reboot"),
        power_button(ctx, "power:off", "power", "Shut down", "systemctl poweroff", true),
    }
end

local function people_row(ctx, people)
    if #people < 2 then return false end
    local list = {}
    for i, p in ipairs(people) do
        local id = "user:" .. i
        local on = i == login.user
        list[#list + 1] = shell.row {
            id = id, width = 30, height = 30, radius = 15, justify = "center", animate = "snappy",
            background = on and shell.gradient("diagonal", P.accent, P.accent_2)
                or a(P.text, ctx.hovered == id and 0.14 or 0.07),
            on_click = function() choose_user(i) end,
            text(initial(p.display), { font = font("ExtraBold", 9), color = on and P.base or P.text }),
        }
    end
    return shell.row { gap = 6, list }
end

local function field_contents(current, secret)
    if current == "busy" or current == "done" then
        return text(current == "done" and "Starting your session…" or "Checking…",
            { font = font("Medium", 10.5), color = P.dim })
    end
    if login.typed == "" then
        local prompt = shell.value("greetd.prompt", "")
        if current ~= "prompt" or prompt == "" or prompt:lower():match("^password:?%s*$") then prompt = "Password" end
        return shell.row { gap = 10, align = "center",
            shell.icon("lock", { size = 15, color = P.dim }),
            text(prompt, { font = font("Medium", 10.5), color = a(P.dim, 0.85) }) }
    end
    if not secret then return text(login.typed, { font = font("SemiBold", 10.5) }) end
    local dots = {}
    for i = 1, math.min(utf8.len(login.typed) or #login.typed, 24) do
        dots[#dots + 1] = shell.box { key = "dot:" .. i, width = 8, height = 8, radius = 4,
            background = P.text, enter = { opacity = 0, scale = 0.2 }, animate = "snappy" }
    end
    return shell.row { gap = 8, align = "center", dots }
end

shell.popup {
    name = "tide-login",
    persistent = true,
    fill = true,
    keyboard = "exclusive",
    layer = "overlay",
    view = function(ctx)
        local person, kind, people = selection()
        local current = state()
        local secret = shell.value("greetd.secret", "1") == "1"
        local failed = shell.value("greetd.message_kind", "info") == "error"
        local message = shell.value("greetd.message", "")
        local centered = login.typed ~= "" and secret or current == "busy" or current == "done"

        local field = shell.row {
            key = "field:" .. login.shake, width = 300, height = 48, radius = 24, padding = { 0, 20 },
            align = "center", justify = centered and "center" or "start",
            background = glass(0.72),
            border = failed and a(P.danger, 0.7) or a(P.text, 0.14),
            enter = login.shake > 0 and { translate = { 14, 0 } } or nil, animate = "bouncy",
            field_contents(current, secret),
        }

        return shell.stack {
            width = ctx.width, height = ctx.height, justify = "center",
            shell.box { left = 0, right = 0, top = 0, bottom = 0,
                background = shell.gradient("vertical", a(P.base, 0.35), a(P.base, 0.15), a(P.base, 0.55)) },

            shell.row { left = 8, top = 6, session_island(ctx, kind) },
            shell.row { right = 8, top = 6, power_island(ctx) },

            shell.column {
                top = math.floor(ctx.height * 0.17), align = "center", gap = 0,
                text(shell.date("%H:%M"), { font = SANS .. " ExtraLight 96", letter_spacing = -2 }),
                text(shell.date("%A, %d %B"), { font = font("Medium", 13), color = a(P.text, 0.8) }),
            },

            shell.column {
                bottom = math.floor(ctx.height * 0.22), align = "center", gap = 14,
                people_row(ctx, people),
                avatar(64, person and person.display),
                text(person and person.display or "No accounts", { font = font("Bold", 12) }),
                field,
                text(message ~= "" and message or " ", { font = font("Medium", 9.5),
                    color = failed and P.danger or P.dim, max_width = 300, wrap = true,
                    max_lines = 2, text_align = 0.5 }),
            },

            hint ~= "" and shell.row { bottom = 28, radius = 12, padding = { 8, 14 },
                background = a(P.base, 0.45), border = a(P.accent, 0.35),
                text(hint, { font = font("Medium", 9.5) }) } or false,
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
