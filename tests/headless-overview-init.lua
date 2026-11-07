-- The overview of the scrolling layout: a tap of Super opens it, and a strip
-- too long to show whole scrolls in it without moving the focus.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local COLUMNS = 8
local L = shady.layout
local after = shady.automation.after

local function focused() return shady.focused_window().app_id end
local function win(name)
    for _, w in ipairs(shady.windows()) do if w.app_id == name then return w end end
end
local function check(cond, message)
    if not cond then error("overview-test: " .. message) end
end
local function overview() return L.state().overview end
local function tap()
    shady.automation.key_down("Super_L")
    shady.automation.key_up("Super_L")
end

local tapped = 0
shady.bind("Super", function()
    tapped = tapped + 1
    L.overview()
end, "Overview")

-- Each step runs after the last has settled.
local steps = {}
local function step(ms, fn) steps[#steps + 1] = { ms, fn } end
local function run(i)
    local s = steps[i]
    if not s then
        shady.log("overview-test: PASS")
        shady.quit()
        return
    end
    after(s[1], function()
        local ok, err = pcall(s[2])
        if not ok then
            shady.log(tostring(err))
            shady.quit()
            return
        end
        run(i + 1)
    end)
end

local before_x, before_focus
step(200, function()
    check(not overview(), "starts open")
    -- A tap opens and a second closes.
    tap()
    check(overview() and tapped == 1, "a tap did not open the overview")
    tap()
    check(not overview() and tapped == 2, "a second tap did not close it")

    -- Super used for something else is no tap.
    shady.automation.key_down("Super_L")
    shady.automation.key_down("Super+x")
    shady.automation.key_up("Super+x")
    shady.automation.key_up("Super_L")
    check(tapped == 2, "Super with a key counted as a tap")
    shady.automation.key_down("Super_L")
    shady.automation.scroll(15, 0)
    shady.automation.key_up("Super_L")
    check(tapped == 2, "Super with the wheel counted as a tap")
    shady.automation.key_down("Shift_L")
    shady.automation.key_down("Shift+Super_L")
    shady.automation.key_up("Shift+Super_L")
    shady.automation.key_up("Shift_L")
    check(tapped == 2, "Shift+Super counted as a tap")

    -- Holding it and letting go is no tap either.
    shady.automation.key_down("Super_L")
end)
step(700, function()
    shady.automation.key_up("Super_L")
    check(tapped == 2, "a long press counted as a tap")
    -- The wheel above may have changed workspace.
    assert(shady.workspace("1"))
    assert(L.focus("first"))
    check(focused() == "ov-1", "focus first")
    tap()
    check(overview(), "tap to open for the scrolling")
end)
step(500, function()
    before_x, before_focus = win("ov-1").x, focused()
    -- Sideways: the strip moves, the focus stays, no window's size changes.
    shady.automation.scroll(0, 45)
end)
step(500, function()
    check(win("ov-1").x < before_x, "a sideways scroll did not move the strip")
    check(focused() == before_focus, "scrolling moved the focus")
    before_x = win("ov-1").x
    -- It stops at the end of the strip.
    for _ = 1, 40 do shady.automation.scroll(0, 45) end
end)
step(500, function()
    local at_end = win("ov-1").x
    check(at_end < before_x, "further scrolling did not move the strip")
    shady.automation.scroll(0, 45)
    before_x = at_end
end)
step(300, function()
    check(win("ov-1").x == before_x, "the strip scrolled past its end")
    check(focused() == before_focus, "a scroll chose a window")
    -- Keys: arrows choose, other keys go nowhere.
    check(shady.automation.key_down("Right"), "the overview did not take Right")
    shady.automation.key_up("Right")
    check(focused() == "ov-2", "Right did not move the focus")
    check(shady.automation.key_down("a"), "the overview let a letter through")
    shady.automation.key_up("a")
    check(overview(), "a letter closed the overview")
    -- Escape leaves, and the focused column is on screen again.
    check(shady.automation.key_down("Escape"), "the overview did not take Escape")
    shady.automation.key_up("Escape")
    check(not overview(), "Escape did not close the overview")
end)
step(500, function()
    local w, screen = win("ov-2"), shady.outputs()[1].width
    check(w.x >= 0 and w.x + w.width <= screen, "the focused column is not on screen after leaving")
    -- Outside the overview a letter is the window's again.
    check(not shady.automation.key_down("a"), "a letter was taken outside the overview")
    shady.automation.key_up("a")
    -- A click picks the window under it, and leaves.
    tap()
end)
step(500, function()
    local o = shady.outputs()[1]
    shady.automation.move_pointer(o.width / 2, o.height / 2)
    shady.automation.click("left")
end)
step(500, function()
    check(not overview(), "a click did not close the overview")
    -- The bind is a tap's, not a key press's: nothing fires on the way down.
    local n = tapped
    shady.automation.key_down("Super_L")
    check(tapped == n, "Super fired on the press")
    shady.automation.key_up("Super_L")
    check(tapped == n + 1 and overview(), "the last tap")
    tap()
end)

local mapped = 0
shady.on("window.mapped", function()
    mapped = mapped + 1
    if mapped < COLUMNS then
        shady.spawn(string.format("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=ov-%d %q",
            mapped + 1, probe))
    else
        run(1)
    end
end)
assert(shady.workspace("1"))
shady.spawn(string.format("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=ov-1 %q", probe))
