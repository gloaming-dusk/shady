-- shady-shell's default UI: a bar on every output, Quick Settings, the
-- task context menu and the application launcher.
--
-- To customise, copy this file to ~/.config/shady/shell.lua (or point
-- SHADY_SHELL_CONFIG at your own file). The shell reloads it whenever it
-- changes. The API is documented in docs/SHELL_LUA_API.md.

-- Options a config can set before dofile()-ing this file; see
-- shell/examples/aurora.lua.
local options = shell.options or {}
local t = shell.theme
local WHITE, BLACK = "#ffffff", "#000000"
local BAR = 38
local BOLD, MEDIUM = "Sans SemiBold 9.5", "Sans Medium 9.5"
local a, mix = shell.alpha, shell.mix

-- Popups grow out of a slightly smaller, lifted, transparent card and go
-- back into it when they close.
local POP = { opacity = 0, scale = 0.96, translate = { 0, -6 } }

-- A rounded fill that is a little lighter at the top.
local function sheen(base, alpha, lift)
    return shell.gradient("vertical", a(mix(base, WHITE, lift), alpha), a(base, alpha))
end

-- The brand tile: accent gradient with a dark monogram.
local function badge(size, radius, letter)
    return shell.row {
        width = size, height = size, radius = radius,
        justify = "center", align = "center",
        background = shell.gradient("diagonal", t.accent, t.accent_2),
        border = a(WHITE, 0.25),
        shell.text("S", { font = "Sans SemiBold " .. letter, color = "#050f1a" }),
    }
end

-- Glass card used by every popup, with an accent kiss along its top edge.
local function panel(radius, props)
    props.radius = radius
    props.align = props.align or "stretch"
    props.animate, props.enter, props.exit = "snappy", POP, POP
    props.background = sheen(t.surface, options.panel_opacity or 0.975, 0.045)
    props.border = a(t.text, 0.08)
    table.insert(props, 1, shell.row {
        height = 1, padding = { 0, radius },
        shell.spacer { height = 1, background = shell.gradient("horizontal",
            a(t.accent, 0), a(t.accent, 0.4), a(t.accent, 0)) },
    })
    return shell.column(props)
end

-- A menu row: highlight when hovered, optional pip, danger colouring.
local function menu_row(ctx, id, height, inset, label, opts)
    opts = opts or {}
    local hovered = ctx.hovered == id
    local tint = opts.danger and mix(t.surface, t.danger, 0.28) or t.accent_deep
    local line = opts.danger and t.danger or t.accent
    local text_color = opts.danger and t.danger or t.text
    return shell.row {
        id = id, height = height, padding = { inset, inset * 2 + 1, inset, inset * 2 + 1 },
        align = "stretch", on_click = opts.on_click,
        shell.row {
            grow = 1, radius = 8, padding = { 0, 9 }, gap = 8, animate = "snappy",
            background = hovered and sheen(tint, 0.85, 0.06) or nil,
            border = hovered and a(line, 0.32) or nil,
            opts.pip ~= nil and shell.pip { radius = 2.6, lit = opts.pip } or false,
            shell.text(label, {
                animate = "snappy",
                font = (hovered or opts.strong) and "Sans SemiBold 9" or "Sans 9",
                color = a(text_color, (hovered or opts.strong) and 1.0 or (opts.danger and 0.85 or 0.78)),
            }),
        },
    }
end

local function divider()
    return shell.row { height = 1, padding = { 0, 12 }, shell.spacer { background = a(WHITE, 0.07) } }
end

-- ---- launcher ----------------------------------------------------------

local launcher = { search = "", selected = 1 }

local function matches()
    return shell.apps(launcher.search, 8)
end

local function launch(app)
    shell.close("launcher")
    shell.launch(app)
end

local function toggle_launcher()
    launcher.search, launcher.selected = "", 1
    shell.toggle("launcher")
end

shell.on("launcher", toggle_launcher)

shell.popup {
    name = "launcher",
    width = 680, height = 580,
    keyboard = "exclusive",
    view = function(ctx)
        local apps = matches()
        if launcher.selected > #apps then launcher.selected = math.max(#apps, 1) end
        local results = {}
        for i, app in ipairs(apps) do
            local id = "app:" .. i
            local selected = i == launcher.selected
            local lit = selected or ctx.hovered == id
            local initial = (app.name:match("^[%z\1-\127\194-\244][\128-\191]*") or "?"):upper()
            results[#results + 1] = shell.row {
                id = id, height = 46, radius = 11, padding = { 0, 12 }, gap = 12,
                animate = "snappy",
                background = lit and sheen(t.accent_deep, selected and 0.85 or 0.55, 0.06) or nil,
                border = lit and a(t.accent, selected and 0.32 or 0.16) or nil,
                on_click = function() launch(app) end,
                shell.row {
                    width = 34, height = 34, radius = 9, justify = "center",
                    background = sheen(selected and mix(t.accent_deep, t.accent, 0.18)
                        or mix(t.surface, t.text, 0.07), 1.0, 0.10),
                    border = a(selected and t.accent or t.text, selected and 0.45 or 0.10),
                    shell.text(initial, { font = "Sans SemiBold 11", color = a(t.text, selected and 1 or 0.75) }),
                },
                shell.column {
                    grow = 1, shrink = 1, align = "start", gap = 2,
                    shell.text(app.name, { font = selected and "Sans SemiBold 10" or "Sans 10",
                        color = a(t.text, selected and 1 or 0.84) }),
                    shell.text(app.exec, { font = "Sans 8", color = a(t.text_dim, selected and 0.9 or 0.65) }),
                },
                selected and shell.text("↵", { font = "Sans SemiBold 10", color = a(t.accent, 0.85) }) or false,
            }
        end
        if #apps == 0 then
            results[1] = shell.column {
                align = "start", padding = { 13, 16 }, gap = 4,
                shell.text("No applications found", { font = "Sans SemiBold 10" }),
                shell.text("Try another name or executable", { font = "Sans 9", color = t.text_dim }),
            }
        end

        local query = launcher.search ~= "" and shell.row {
            gap = 2,
            shell.text(launcher.search, { font = "Sans SemiBold 10" }),
            shell.box { width = 1.5, height = 20, background = a(t.accent, 0.9) },
        } or shell.text("Search applications…", { font = "Sans 10", color = a(t.text_dim, 0.75) })

        return panel(18, {
            width = 680, height = 580,
            shell.column {
                grow = 1, align = "stretch", padding = { 17, 22, 0, 22 },
                shell.row {
                    height = 34, gap = 12,
                    badge(34, 10, 12),
                    shell.column {
                        align = "start", gap = 2,
                        shell.text("Applications", { font = "Sans SemiBold 13" }),
                        shell.text("Launch something", { font = "Sans 9", color = t.text_dim }),
                    },
                    shell.spacer(),
                    shell.row {
                        height = 20, radius = 5, padding = { 0, 6 },
                        background = a(WHITE, 0.05), border = a(t.text, 0.14),
                        shell.text("Esc", { font = "Sans SemiBold 8", color = t.text_dim }),
                    },
                },
                shell.box { height = 16 },
                shell.row {
                    height = 48, radius = 12, padding = { 0, 16 }, gap = 12,
                    background = a(BLACK, 0.30), border = a(t.accent, 0.42),
                    shell.text("⌕", { font = "Sans 13", color = t.accent }),
                    query,
                },
                shell.box { height = 9 },
                shell.column { align = "stretch", gap = 4, results },
                shell.spacer(),
                divider(),
                shell.row {
                    height = 43, padding = { 0, 2 },
                    shell.text("↑ ↓  navigate     ↵  launch     Esc  close",
                        { font = "Sans 8", color = t.text_dim }),
                },
            },
        })
    end,
    on_key = function(key, text)
        local apps = matches()
        if key == "Escape" then
            shell.close("launcher")
        elseif key == "Return" or key == "KP_Enter" then
            if apps[launcher.selected] then launch(apps[launcher.selected]) end
        elseif key == "Up" then
            launcher.selected = math.max(1, launcher.selected - 1)
        elseif key == "Down" then
            launcher.selected = math.min(math.max(#apps, 1), launcher.selected + 1)
        elseif key == "BackSpace" then
            local cut = utf8.offset(launcher.search, -1)
            if cut then launcher.search = launcher.search:sub(1, cut - 1) end
            launcher.selected = 1
        elseif text ~= "" and #launcher.search + #text < 128 then
            launcher.search = launcher.search .. text
            launcher.selected = 1
        end
        shell.redraw()
    end,
}

-- ---- quick settings ----------------------------------------------------

local quick_output

local function toggle_quick(output)
    shell.close("menu")
    if shell.is_open("quick") then
        shell.close("quick")
    else
        quick_output = output
        shell.open("quick", { output = output })
    end
end

shell.popup {
    name = "quick",
    anchor = { "top", "right" },
    margin = { top = BAR + 4, right = 8 },
    width = 250,
    view = function(ctx)
        local rows = {
            menu_row(ctx, "q:launcher", 38, 3, "Launcher", { on_click = function()
                shell.close("quick")
                toggle_launcher()
            end }),
            menu_row(ctx, "q:next", 38, 3, "Next window", { on_click = function()
                shell.cycle()
                shell.close("quick")
            end }),
        }
        local active = shell.active_workspace()
        for _, name in ipairs(shell.workspaces()) do
            rows[#rows + 1] = menu_row(ctx, "q:ws:" .. name, 38, 3, "Workspace  " .. name, {
                pip = name == active, strong = name == active,
                on_click = function()
                    shell.workspace(name)
                    shell.close("quick")
                end,
            })
        end
        rows[#rows + 1] = divider()
        rows[#rows + 1] = menu_row(ctx, "q:quit", 37, 3, "Quit Shady", { danger = true,
            on_click = function() shell.quit() end })
        rows[#rows + 1] = shell.box { height = 6 }
        return panel(13, {
            width = 250,
            shell.row {
                height = 32, padding = { 0, 16 },
                shell.text("Quick settings", { font = "Sans SemiBold 10", color = a(t.text, 0.96) }),
            },
            divider(),
            rows,
        })
    end,
}

-- ---- task context menu -------------------------------------------------

local function find_window(id)
    for _, w in ipairs(shell.windows()) do
        if w.id == id then return w end
    end
end

local menu_window

-- Close the menu once its window is gone.
shell.on("change", function()
    if shell.is_open("menu") and not find_window(menu_window) then shell.close("menu") end
end)

shell.popup {
    name = "menu",
    anchor = { "top", "left" },
    margin = { top = BAR + 4 },
    width = 230,
    view = function(ctx, args)
        local w = find_window(args.window) or { id = args.window, workspace = "" }
        local function act(fn)
            return function()
                fn(w.id)
                shell.close("menu")
            end
        end
        local rows = {
            menu_row(ctx, "m:focus", 34, 3, "Focus", { on_click = act(shell.focus) }),
            menu_row(ctx, "m:max", 34, 3, w.maximized and "Restore" or "Maximize",
                { on_click = act(shell.maximize) }),
            menu_row(ctx, "m:full", 34, 3, w.fullscreen and "Exit fullscreen" or "Fullscreen",
                { on_click = act(shell.fullscreen) }),
        }
        for _, name in ipairs(shell.workspaces()) do
            rows[#rows + 1] = menu_row(ctx, "m:ws:" .. name, 34, 3, "Move to " .. name, {
                pip = w.workspace == name,
                on_click = act(function(id) shell.move(id, name) end),
            })
        end
        rows[#rows + 1] = menu_row(ctx, "m:close", 34, 3, "Close",
            { danger = true, on_click = act(shell.close_window) })
        return panel(11, { width = 230, rows })
    end,
}

local function open_menu(window, info)
    shell.close("quick")
    menu_window = window.id
    shell.open("menu", { output = info.output, margin = { left = math.floor(info.x) },
        window = window.id })
end

-- ---- notifications -----------------------------------------------------

-- Serve org.freedesktop.Notifications unless another daemon already does.
shell.notification_server()

local function active_notifications(limit)
    local list = {}
    for _, n in ipairs(shell.notifications()) do
        if n.active and #list < limit then list[#list + 1] = n end
    end
    return list
end

-- The toast popup is open exactly while something is on screen.
shell.on("notifications", function()
    local any = #active_notifications(1) > 0
    if any and not shell.is_open("toasts") then
        shell.open("toasts")
    elseif not any and shell.is_open("toasts") then
        shell.close("toasts")
    end
end)

-- A click runs the "default" action if there is one, else dismisses.
local function activate(n)
    for _, action in ipairs(n.actions) do
        if action.key == "default" then
            shell.invoke_notification(n.id, "default")
            return
        end
    end
    shell.dismiss_notification(n.id)
end

local function toast(ctx, n)
    local id = "toast:" .. n.id
    local critical = n.urgency == "critical"
    local buttons = {}
    for _, action in ipairs(n.actions) do
        if action.key ~= "default" and action.label ~= "" then
            local bid = id .. ":" .. action.key
            buttons[#buttons + 1] = shell.row {
                id = bid, height = 24, radius = 7, padding = { 0, 10 }, animate = "snappy",
                background = ctx.hovered == bid and t.accent_deep or a(WHITE, 0.06),
                border = a(t.accent, ctx.hovered == bid and 0.4 or 0.15),
                on_click = function() shell.invoke_notification(n.id, action.key) end,
                shell.text(action.label, { font = "Sans SemiBold 8.5" }),
            }
        end
    end
    local card = panel(12, {
        key = id, width = 340, padding = { 0, 0, 12, 0 },
        shell.row {
            id = id, padding = { 10, 12, 0, 14 }, gap = 11, align = "start",
            on_click = function() activate(n) end,
            badge(30, 9, 11),
            shell.column {
                grow = 1, shrink = 1, align = "start", gap = 3,
                shell.text(n.app ~= "" and n.app or "Notification",
                    { font = "Sans 8", color = a(t.text_dim, 0.9) }),
                shell.text(n.summary, { font = "Sans SemiBold 10", max_width = 250 }),
                n.body ~= "" and shell.text(n.body,
                    { font = "Sans 9", color = a(t.text, 0.78), max_width = 250 }) or false,
            },
            shell.row {
                id = id .. ":close", width = 20, height = 20, radius = 6, justify = "center",
                background = ctx.hovered == id .. ":close" and a(WHITE, 0.1) or nil,
                on_click = function() shell.dismiss_notification(n.id) end,
                shell.text("×", { font = "Sans 11", color = t.text_dim }),
            },
        },
        #buttons > 0 and shell.row { padding = { 8, 14, 0, 55 }, gap = 6, buttons } or false,
    })
    card.enter, card.exit = { opacity = 0, translate = { 24, 0 } }, nil
    if critical then card.border = a(t.danger, 0.6) end
    return card
end

shell.popup {
    name = "toasts",
    anchor = { "top", "right" },
    margin = { top = BAR + 6, right = 8 },
    view = function(ctx)
        local cards = {}
        for _, n in ipairs(active_notifications(4)) do cards[#cards + 1] = toast(ctx, n) end
        return shell.column {
            gap = 8, align = "stretch", padding = { 0, 0, 4, 0 },
            exit = { opacity = 0, translate = { 24, 0 } }, animate = "snappy",
            cards,
        }
    end,
}

-- ---- bar ---------------------------------------------------------------

local function workspaces(ctx)
    local active = shell.active_workspace()
    local pills = {}
    for _, name in ipairs(shell.workspaces()) do
        local id = "ws:" .. name
        local on = name == active
        local hovered = ctx.hovered == id
        pills[#pills + 1] = shell.row {
            id = id, height = 22, radius = 7, animate = "bouncy",
            padding = on and { 0, 5, 0, 8 } or { 0, 11 }, gap = 4.6,
            background = on and sheen(t.accent_deep, 0.92, 0.08) or (hovered and a(WHITE, 0.06) or nil),
            border = on and a(t.accent, 0.35) or nil,
            on_click = function(button)
                if button == "left" then shell.workspace(name) end
            end,
            on and shell.box { key = id .. ":dot", width = 4.4, height = 4.4, radius = 2.2,
                background = t.accent, enter = { scale = 0, opacity = 0 } } or false,
            shell.text(name, { key = id .. ":name", animate = "snappy", font = on and BOLD or MEDIUM,
                color = a(t.text, on and 0.98 or (hovered and 0.85 or 0.55)) }),
        }
    end
    if #pills == 0 then return false end
    return shell.row {
        height = 28, radius = 10, padding = 3, gap = 5,
        background = a(BLACK, 0.22), border = a(WHITE, 0.06),
        pills,
    }
end

local function tasks(ctx)
    local active = shell.active_workspace()
    local chips = {}
    for _, w in ipairs(shell.windows()) do
        if w.workspace == "" or active == "" or w.workspace == active then
            local id = "task:" .. w.id
            local hovered = ctx.hovered == id
            local label = w.title ~= "" and w.title or (w.app_id ~= "" and w.app_id or "Window")
            chips[#chips + 1] = shell.row {
                id = id, height = 26, min_width = 92, max_width = 200, radius = 9,
                animate = "smooth", enter = { opacity = 0, scale = 0.9 },
                padding = { 0, 13, 0, 10 }, gap = 7,
                background = w.focused and sheen(t.accent_deep, 0.90, 0.08)
                    or a(WHITE, hovered and 0.06 or 0.025),
                border = w.focused and a(t.accent, 0.38) or a(WHITE, hovered and 0.12 or 0.06),
                shader = w.focused and options.focus_shader or nil,
                uniforms = options.focus_uniforms,
                on_click = function(button, info)
                    if button == "left" then
                        shell.close("menu")
                        shell.focus(w.id)
                    elseif button == "right" then
                        open_menu(w, info)
                    end
                end,
                shell.pip { radius = 3, lit = w.focused },
                shell.text(label, { font = w.focused and BOLD or MEDIUM,
                    color = a(t.text, w.focused and 0.97 or (hovered and 0.82 or 0.60)) }),
            }
        end
    end
    return shell.row { grow = 1, clip = true, gap = 6, chips }
end

local function clock(ctx)
    local open = shell.is_open("quick") and quick_output == ctx.output
    return shell.row {
        id = "clock", height = 26, radius = 9, padding = { 0, 10, 0, 12 }, gap = 10,
        animate = "snappy",
        background = open and sheen(t.accent_deep, 0.92, 0.08) or a(WHITE, 0.03),
        border = open and a(t.accent, 0.38) or a(WHITE, 0.07),
        on_click = function(button, info)
            if button == "left" then toggle_quick(info.output) end
        end,
        shell.text(shell.date("%a %d %b"), { font = MEDIUM, color = a(t.text_dim, 0.95) }),
        shell.text(shell.date("%H:%M"), { font = BOLD, color = a(t.text, 0.98) }),
    }
end

shell.bar {
    name = "shady-shell",
    edge = "top",
    size = BAR,
    shader = options.bar_shader,
    uniforms = options.bar_uniforms,
    view = function(ctx)
        local surface = t.surface
        return shell.column {
            align = "stretch",
            -- Slides down from above the screen when the bar appears.
            enter = { translate = { 0, -BAR }, opacity = 0 }, animate = "gentle",
            background = shell.gradient("vertical",
                a(mix(surface, WHITE, 0.025), options.bar_opacity or 0.94),
                a(mix(surface, BLACK, 0.35), options.bar_opacity or 0.94)),
            shell.box { height = 1, background = a(WHITE, 0.045) },
            shell.row {
                grow = 1, padding = { 0, 12, 0, 8 },
                badge(26, 8, 11),
                shell.box { width = 8 },
                shell.text("Shady", { font = BOLD, color = a(t.text, 0.92), width = 49 }),
                workspaces(ctx),
                shell.box { width = 2 },
                tasks(ctx),
                options.bar_extra and options.bar_extra(ctx) or false,
                options.bar_extra and shell.box { width = 8 } or false,
                clock(ctx),
            },
            shell.box { height = 1, background = shell.gradient("horizontal",
                a(t.accent, 0.06), a(t.accent, 0.42), a(t.accent, 0.06)) },
        }
    end,
}
