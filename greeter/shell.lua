-- Shady greeter: the login card shady-shell shows over Afterglow's sky while
-- greetd waits for someone to log in. Authentication goes through the
-- greetd shell plugin (src/greeter/greetd.c); see docs/GREETER.md.
--
-- Keys: type the password and press Enter. Up/Down pick the user, Tab the
-- session, Escape clears what was typed.

-- Afterglow's live palette, with theme.sh's colours until it arrives
-- (as in examples/rice/afterglow/shell.lua).
local base = shell.theme
local live = {}
for _, name in ipairs({ "accent", "accent_2", "accent_deep", "surface", "text", "text_dim" }) do
    live[name] = true
end
local t = setmetatable({}, {
    __index = function(_, name)
        if live[name] then return shell.compositor_value("afterglow." .. name, base[name]) end
        return base[name]
    end,
})

local WHITE, BLACK = "#ffffff", "#000000"
local CARD = 380
local a, mix = shell.alpha, shell.mix

local ok, err = shell.plugin("greetd")
if not ok then shell.log("greeter: " .. tostring(err)) end

-- ---- people and sessions ---------------------------------------------

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
    for _, row in ipairs(rows(shell.value("greetd.users", ""))) do
        list[#list + 1] = { name = row[1], display = row[2] }
    end
    return list
end

local function sessions()
    local list = {}
    for _, row in ipairs(rows(shell.value("greetd.sessions", ""))) do
        list[#list + 1] = { name = row[1], id = row[2] }
    end
    return list
end

local function index_of(list, field, value, fallback)
    for i, item in ipairs(list) do
        if item[field] == value then return i end
    end
    return fallback
end

local login = { user = nil, session = nil, typed = "" }

-- Start from the last login, else the first account and the Shady session.
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

local function state()
    return shell.value("greetd.state", "idle")
end

local function submit()
    local person, kind = selection()
    if not person then return end
    if state() == "prompt" and shell.value("greetd.prompt", "") ~= "" and login.started then
        -- A further question from PAM (a code, a new password, ...).
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

-- ---- drawing ------------------------------------------------------------

local function sheen(colour, alpha, lift)
    return shell.gradient("vertical", a(mix(colour, WHITE, lift), alpha), a(colour, alpha))
end

local function arrow(id, glyph, ctx, on_click, visible)
    if not visible then return shell.spacer { width = 28 } end
    return shell.row {
        id = id, width = 28, height = 28, radius = 14, justify = "center",
        background = ctx.hovered == id and a(WHITE, 0.08) or nil,
        on_click = on_click,
        shell.text(glyph, { font = "Sans 13", color = a(t.text, ctx.hovered == id and 1 or 0.6) }),
    }
end

local function avatar(person)
    local initial = person and (person.display:match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper() or "?"
    return shell.row {
        width = 72, height = 72, radius = 36, justify = "center",
        background = shell.gradient("diagonal", t.accent, t.accent_2),
        border = a(WHITE, 0.3),
        shell.text(initial, { font = "Sans SemiBold 26", color = "#1a0e14" }),
    }
end

local function password_field(current)
    local busy = current == "busy" or current == "done"
    local secret = shell.value("greetd.secret", "1") == "1"
    local prompt = shell.value("greetd.prompt", "")
    if current ~= "prompt" or prompt == "" or prompt:lower():match("^password:?%s*$") then
        prompt = "Password"
    end
    local content
    if busy then
        content = shell.text(current == "done" and "Starting…" or "Checking…",
            { font = "Sans 10.5", color = a(t.text_dim, 0.9) })
    elseif login.typed == "" then
        content = shell.row {
            gap = 2,
            shell.box { width = 1.5, height = 20, background = a(t.accent, 0.9) },
            shell.text(prompt, { font = "Sans 10.5", color = a(t.text_dim, 0.7) }),
        }
    else
        local shown = secret and string.rep("●", utf8.len(login.typed) or #login.typed) or login.typed
        content = shell.row {
            gap = 2, shrink = 1,
            shell.text(shown, { font = secret and "Sans 8" or "Sans 10.5", letter_spacing = secret and 2.5 or 0.25 }),
            shell.box { width = 1.5, height = 20, background = a(t.accent, 0.9) },
        }
    end
    return shell.row {
        height = 46, radius = 13, padding = { 0, 16 }, gap = 10,
        background = a(BLACK, 0.32), border = a(t.accent, busy and 0.2 or 0.5),
        content,
        shell.spacer(),
        (not busy and login.typed ~= "") and shell.text("↵", { font = "Sans SemiBold 11", color = t.accent }) or false,
    }
end

local function message_line()
    local text = shell.value("greetd.message", "")
    local error = shell.value("greetd.message_kind", "info") == "error"
    return shell.row {
        height = 20, justify = "center",
        shell.text(text, { font = "Sans 9", color = error and t.danger or t.text_dim }),
    }
end

local function power(ctx, id, label, command)
    return shell.row {
        id = id, height = 30, radius = 15, padding = { 0, 14 },
        background = ctx.hovered == id and a(WHITE, 0.10) or a(WHITE, 0.04),
        border = a(t.text, ctx.hovered == id and 0.22 or 0.10),
        on_click = function() shell.spawn(command) end,
        shell.text(label, { font = "Sans 9", color = a(t.text, ctx.hovered == id and 1 or 0.72) }),
    }
end

shell.popup {
    name = "login",
    persistent = true,
    keyboard = "exclusive",
    layer = "overlay",
    view = function(ctx)
        local person, kind, people, kinds = selection()
        local current = state()
        return shell.column {
            align = "center", gap = 0, padding = 24,
            shell.text(shell.date("%H:%M"), { font = "Sans Light 64", color = a(t.text, 0.96), letter_spacing = 1 }),
            shell.text(shell.date("%A, %e %B"), { font = "Sans 12", color = a(t.text, 0.75) }),
            shell.box { height = 30 },
            shell.column {
                width = CARD, radius = 24, padding = { 26, 26, 20, 26 }, gap = 0, align = "stretch",
                background = sheen(t.surface, 0.62, 0.05), border = a(t.text, 0.10),
                shell.row { justify = "center", avatar(person) },
                shell.box { height = 12 },
                shell.row {
                    justify = "center", gap = 6,
                    arrow("user:prev", "‹", ctx, function() cycle("user", people, -1); login.typed = ""; shell.redraw() end, #people > 1),
                    shell.text(person and person.display or "No accounts", { font = "Sans SemiBold 14", shrink = 1 }),
                    arrow("user:next", "›", ctx, function() cycle("user", people, 1); login.typed = ""; shell.redraw() end, #people > 1),
                },
                (person and person.display ~= person.name) and shell.row {
                    justify = "center",
                    shell.text(person.name, { font = "Sans 9", color = t.text_dim }),
                } or false,
                shell.box { height = 18 },
                password_field(current),
                shell.box { height = 8 },
                message_line(),
                shell.box { height = 10 },
                shell.row {
                    id = "session", height = 34, radius = 10, padding = { 0, 6, 0, 12 }, gap = 6,
                    background = ctx.hovered == "session" and a(WHITE, 0.07) or a(WHITE, 0.03),
                    on_click = function() cycle("session", kinds, 1); shell.redraw() end,
                    shell.text("Session", { font = "Sans 9", color = t.text_dim }),
                    shell.spacer(),
                    shell.text(kind and kind.name or "none", { font = "Sans SemiBold 9.5", shrink = 1 }),
                    shell.text("⇅", { font = "Sans 9", color = a(t.accent, 0.8) }),
                },
            },
            shell.box { height = 16 },
            shell.text("↵ log in     ↑ ↓ user     Tab session     Esc clear",
                { font = "Sans 8.5", color = a(t.text, 0.55) }),
            shell.box { height = 14 },
            shell.row {
                gap = 10,
                power(ctx, "power:restart", "Restart", "systemctl reboot"),
                power(ctx, "power:off", "Shut down", "systemctl poweroff"),
            },
        }
    end,
    on_key = function(key, text)
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
        elseif text ~= "" and #login.typed + #text <= 256 then
            login.typed = login.typed .. text
        end
        shell.redraw()
    end,
}
