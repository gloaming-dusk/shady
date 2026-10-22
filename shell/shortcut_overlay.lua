-- The shortcut overlay: every key combination the session answers to, grouped
-- by where it comes from (Shady itself, the rice's bindings, each plugin).
-- The compositor opens and closes it with Super+Shift+/ (`bind.show_shortcuts`)
-- and sends the list along; see docs/SHELL_LUA_API.md.
--
--     local overlay = require("shortcut_overlay")
--     overlay.install()                        -- the default look
--     overlay.install { accent = "#ff9a5c", frame = function(body) ... end }
--
-- Options (all optional):
--   name        popup name, default "shortcuts"
--   columns     the fewest columns the groups flow into, default 2; more are
--               used when the list would not fit the screen's height
--   backdrop    colour laid over the screen behind the card
--   title       heading, default "Keyboard shortcuts"
--   font, bold, title_font   text fonts
--   palette     where colours come from, read on every repaint so a live
--               theme (Afterglow's sky) is followed: `text`, `text_dim`,
--               `accent`, `surface`. Default shell.theme.
--   cap         function(palette) -> style table for a key cap
--               ({background=, border=, radius=, color=, font=})
--   frame       function(body, ctx) -> widget: the card around the list. The
--               default is a dark rounded card in the palette's colours.
--   enter, exit animation styles of the card
--   order       group names to show first, in this order; default
--               { "Shady", "Desktop" }: the compositor, then the rice's bindings

local M = {}

local a = shell.alpha

-- Key names as they are printed on keyboards.
local PRETTY = {
    slash = "/", backslash = "\\", question = "?", equal = "=", minus = "−", plus = "+",
    comma = ",", period = ".", semicolon = ";", apostrophe = "'", grave = "`",
    bracketleft = "[", bracketright = "]",
    Return = "Enter", KP_Enter = "Enter", Escape = "Esc", BackSpace = "Backspace",
    space = "Space", Prior = "PgUp", Next = "PgDn",
    Left = "←", Right = "→", Up = "↑", Down = "↓", Arrows = "← → ↑ ↓",
    Print = "PrtSc",
}

local function pretty(key)
    if PRETTY[key] then return PRETTY[key] end
    -- "Left/Right/Up/Down": several keys sharing one cap.
    if key:find("/", 2, true) then
        local parts = {}
        for part in key:gmatch("[^/]+") do parts[#parts + 1] = PRETTY[part] or part end
        return table.concat(parts, " ")
    end
    return key
end

-- "Super+Shift+slash Alt+Left" -> { {"Super", "Shift", "/"}, {"Alt", "←"} }
local function combos(keys)
    local out = {}
    for combo in keys:gmatch("%S+") do
        local caps = {}
        for key in combo:gmatch("[^+]+") do caps[#caps + 1] = pretty(key) end
        out[#out + 1] = caps
    end
    return out
end

-- Plugin names read as titles: "spatial-overview" -> "Spatial overview".
local function title(group)
    if group:find("%u") then return group end
    group = group:gsub("[-_]", " ")
    return group:sub(1, 1):upper() .. group:sub(2)
end

local function group_list(list, order)
    local groups, by_name = {}, {}
    local function group(name)
        if not by_name[name] then
            by_name[name] = { name = name, items = {} }
            groups[#groups + 1] = by_name[name]
        end
        return by_name[name]
    end
    for _, name in ipairs(order or {}) do group(name) end
    -- A binding both the config and the rice's Lua describe is shown once.
    local seen = {}
    for _, s in ipairs(list) do
        local id = s.keys .. "\0" .. s.description
        if not seen[id] then
            seen[id] = true
            local g = group(s.group)
            g.items[#g.items + 1] = s
        end
    end
    local shown = {}
    for _, g in ipairs(groups) do
        if #g.items > 0 then shown[#shown + 1] = g end
    end
    return shown
end

-- Sizes the layout is estimated with, in logical pixels.
local COLUMN, COLUMN_GAP, ROW, HEADER, GROUP_GAP, CHROME = 380, 36, 28, 26, 20, 150

-- Deal whole groups into columns, filling the shortest one each time.
-- Returns the columns and the tallest one's height.
local function flow(groups, columns)
    local cols, heights = {}, {}
    for i = 1, columns do cols[i], heights[i] = {}, 0 end
    for _, g in ipairs(groups) do
        local shortest = 1
        for i = 2, columns do
            if heights[i] < heights[shortest] then shortest = i end
        end
        table.insert(cols[shortest], g)
        heights[shortest] = heights[shortest] + (heights[shortest] > 0 and GROUP_GAP or 0) +
            HEADER + #g.items * ROW
    end
    local tallest = 0
    for i = 1, columns do tallest = math.max(tallest, heights[i]) end
    return cols, tallest
end

-- The fewest columns (from `min`) that fit the height, within the width.
local function layout(groups, min, width, height)
    local most = math.max(1, math.min(#groups, (width - 60 + COLUMN_GAP) // (COLUMN + COLUMN_GAP)))
    local cols
    for n = math.min(min, most), most do
        local tallest
        cols, tallest = flow(groups, n)
        if tallest + CHROME <= height then break end
    end
    return cols
end

function M.install(opts)
    opts = opts or {}
    local name = opts.name or "shortcuts"
    local columns = opts.columns or 2
    local font = opts.font or "Sans 9.5"
    local bold = opts.bold or "Sans SemiBold 9.5"
    -- Set at the start of every repaint.
    local p, cap_style
    local enter = opts.enter or { opacity = 0, scale = 0.97, translate = { 0, 8 } }
    local exit = opts.exit or { opacity = 0, scale = 0.98 }

    local function cap(label)
        local props = {
            height = 22, min_width = 22, padding = { 0, 7 }, justify = "center", radius = 6,
            background = a(p.text, 0.07), border = a(p.text, 0.16),
        }
        for k, v in pairs(cap_style) do
            if k ~= "font" and k ~= "color" then props[k] = v end
        end
        props[1] = shell.text(label, { font = cap_style.font or bold, color = cap_style.color or p.text })
        return shell.row(props)
    end

    local function keys_widget(keys)
        local row = { gap = 10 }
        for _, caps in ipairs(combos(keys)) do
            local combo = { gap = 3 }
            for i, label in ipairs(caps) do
                if i > 1 then combo[#combo + 1] = shell.text("+", { font = font, color = a(p.text_dim, 0.7) }) end
                combo[#combo + 1] = cap(label)
            end
            row[#row + 1] = shell.row(combo)
        end
        return shell.row(row)
    end

    local function group_widget(g)
        local rows = {
            align = "stretch", gap = 6,
            shell.text(title(g.name), { font = bold, color = p.accent }),
        }
        for _, s in ipairs(g.items) do
            rows[#rows + 1] = shell.row {
                gap = 18, justify = "space-between",
                shell.text(s.description, { font = font, color = p.text, shrink = 1 }),
                keys_widget(s.keys),
            }
        end
        return shell.column(rows)
    end

    local function default_frame(body)
        return shell.column {
            radius = 18, padding = { 22, 26 }, align = "stretch",
            background = a(p.surface, 0.96), border = a(p.text, 0.10),
            body,
        }
    end
    local frame = opts.frame or default_frame

    shell.popup {
        name = name,
        fill = true,
        keyboard = "exclusive",
        view = function(ctx)
            p = opts.palette or shell.theme
            cap_style = opts.cap and opts.cap(p) or {}
            local groups = group_list(shell.shortcuts(), opts.order or { "Shady", "Desktop" })
            local body
            if #groups == 0 then
                body = shell.text("No shortcuts are known yet.", { font = font, color = p.text_dim })
            else
                local cols = { gap = COLUMN_GAP, align = "start" }
                for _, col in ipairs(layout(groups, columns, ctx.width, ctx.height)) do
                    local c = { align = "stretch", gap = GROUP_GAP, width = COLUMN }
                    for _, g in ipairs(col) do c[#c + 1] = group_widget(g) end
                    cols[#cols + 1] = shell.column(c)
                end
                body = shell.row(cols)
            end
            local card = frame(shell.column {
                align = "stretch", gap = 18,
                shell.row {
                    justify = "space-between",
                    shell.text(opts.title or "Keyboard shortcuts",
                        { font = opts.title_font or "Sans SemiBold 13", color = p.text }),
                    shell.text("Esc  close", { font = font, color = p.text_dim }),
                },
                body,
            }, ctx)
            card.animate = card.animate or "snappy"
            card.enter, card.exit = card.enter or enter, card.exit or exit
            return shell.stack {
                width = ctx.width, height = ctx.height, justify = "center", align = "center",
                shell.box {
                    key = "backdrop", left = 0, top = 0, right = 0, bottom = 0,
                    background = opts.backdrop or a("#000000", 0.35),
                    enter = { opacity = 0 }, exit = { opacity = 0 },
                    on_click = function() shell.close(name) end,
                },
                card,
            }
        end,
        on_key = function(key)
            if key == "Escape" or key == "Return" then shell.close(name) end
        end,
    }

    -- The compositor sends the list, then this toggles the popup.
    shell.on("shortcuts", function() shell.toggle(name) end)
end

return M
