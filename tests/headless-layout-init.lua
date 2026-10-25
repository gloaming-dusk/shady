-- The scrolling layout: columns open beside the focused one, the strip
-- scrolls to keep the focus on screen, and windows move between columns.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local W = shady.outputs()[1].width
local G = 10

local mapped = {}
local focused_before_close
local function spawn(name)
    shady.spawn(string.format("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=%s %q", name, probe))
end
local function win(name)
    for _, w in ipairs(shady.windows()) do if w.app_id == name then return w end end
end
local function check(cond, message)
    if not cond then
        for _, w in ipairs(shady.windows()) do
            shady.log(string.format("  %s x=%d y=%d ws=%s tiled=%s", w.app_id, w.x, w.y,
                w.workspace, tostring(w.tiled)))
        end
        error("layout-test: " .. message)
    end
end
local function focused() return shady.focused_window().app_id end

local steps = {}
local function run()
    local ok, err = pcall(function()
        local half = math.floor((W - G) * 0.5 + 0.5) - G
        local a, b, c = win("lay-a"), win("lay-b"), win("lay-c")
        check(a.tiled and b.tiled and c.tiled, "windows are not tiled")
        -- a | b | c, each half the screen; c focused at the right edge.
        check(focused() == "lay-c", "newest window is not focused")
        check(b.x - a.x == half + G, "columns are not a half plus a gap apart")
        check(c.x + half == W - G, "focused last column is not at the right edge")

        assert(shady.layout.focus("left"))
        check(focused() == "lay-b", "focus left")
        assert(shady.layout.focus("first"))
        check(focused() == "lay-a", "focus first")
        check(win("lay-a").x == G, "first column is not at the left edge")

        -- Move a to the end: b | c | a.
        assert(shady.layout.move("last"))
        check(win("lay-a").x > win("lay-c").x, "move last")
        -- Put a under c: b | c/a.
        assert(shady.layout.consume_or_expel("left"))
        a, c = win("lay-a"), win("lay-c")
        check(a.x == c.x and a.y > c.y, "consume into the left column")
        check(shady.layout.state().columns == 2, "two columns after consume")
        -- And out again: b | c | a.
        assert(shady.layout.consume_or_expel("right"))
        check(shady.layout.state().columns == 3, "three columns after expel")

        -- Width: 50% -> full width, then a preset.
        assert(shady.layout.full_width())
        check(win("lay-a").x == G, "full width column starts at the gap")
        assert(shady.layout.cycle_width())
        assert(shady.layout.set_width("-10%"))

        -- Moving to another workspace takes the window out of the strip.
        assert(win("lay-a"):move_to_workspace("2"))
        check(shady.layout.state().columns == 2, "window left the strip")
        assert(shady.workspace("2"))
        check(shady.layout.state().columns == 1, "window joined workspace 2")
        assert(shady.layout.workspace(-1))
        check(shady.current_workspace() == "1", "workspace step back")

        -- Floating and back.
        local b2 = win("lay-b")
        b2:focus()
        assert(b2:toggle_floating())
        check(not win("lay-b").tiled, "toggle floating")
        assert(win("lay-b"):toggle_floating())
        check(win("lay-b").tiled, "toggle tiled")

        -- Closing a column that does not have the focus keeps the focus.
        assert(shady.layout.focus("first"))
        local first = focused()
        local last
        for _, w in ipairs(shady.windows()) do
            if w.tiled and w.workspace == "1" and w.app_id ~= first then last = w end
        end
        assert(last, "no second tiled window")
        focused_before_close = first
        last:close()
    end)
    if not ok then
        shady.log(tostring(err))
        shady.quit()
    end
end

local function after_close()
    local first = focused_before_close
    local ok, err = pcall(function()
        check(focused() == first, "closing another column moved the focus")
        check(shady.layout.state().active == 1, "closing another column moved the active column")
    end)
    if ok then shady.log("layout-test: PASS") else shady.log(tostring(err)) end
    shady.quit()
end

shady.on("window.unmapped", function(window)
    if window.app_id ~= "lay-a" then
        shady.automation.after(100, after_close)
    end
end)

shady.on("window.mapped", function(window)
    mapped[window.app_id] = true
    if window.app_id == "lay-a" then spawn("lay-b")
    elseif window.app_id == "lay-b" then spawn("lay-c")
    elseif window.app_id == "lay-c" then shady.automation.after(200, run) end
end)
assert(shady.workspace("1"))
spawn("lay-a")
