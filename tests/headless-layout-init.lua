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

        -- Three fingers turn the strip a column at a time: fingers to the
        -- right bring the column on the left. A touch is not a swipe.
        local swipe = shady.automation.swipe
        check(not swipe(4, 200, 0), "another number of fingers was taken")
        check(swipe(3, 20, 0) and focused() == "lay-c", "a touch changed the focus")
        check(swipe(3, 120, 0) and focused() == "lay-b", "a short swipe right")
        check(swipe(3, -120, 0) and focused() == "lay-c", "a short swipe left")
        check(c.x + half == W - G, "a swipe left the strip out of line")
        check(swipe(3, -300, 0) and focused() == "lay-c", "a swipe past the last column")
        check(swipe(3, 2 * W, 0) and focused() == "lay-a", "a long swipe goes further")
        check(swipe(3, 300, 0) and focused() == "lay-a", "a swipe past the first column")
        check(swipe(3, -120, 0) and focused() == "lay-b", "from the first column")
        check(swipe(3, -120, 0) and focused() == "lay-c", "back to the last column")

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
        -- Two columns fit on the screen, so the strip cannot move; the swipe
        -- still goes to the other one.
        check(swipe(3, 120, 0) and focused() == "lay-b", "swipe between columns that fit")
        check(swipe(3, -120, 0) and focused() == "lay-a", "and back")
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
