-- Print's picker with the real Tide runtime and shell, by mouse and by keyboard:
-- a dragged region, a clicked window, the whole screen, and the ways out.
-- tests/headless-tide-screenshot.sh checks the pictures against what is logged.
-- SHOT_TEST_MODE: "all" takes them every way; "failure" and "scale" one each.
dofile(assert(os.getenv("SHADY_ROOT")) .. "/rices/tide/init.lua")
local auto = shady.automation
local after = auto.after
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))

local function spawn(name)
    shady.spawn(string.format("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=%s %q", name, probe))
end
local function win(name)
    for _, w in ipairs(shady.windows()) do if w.app_id == name then return w end end
end
local function wtype(args) shady.spawn("wtype " .. args .. " -s 300") end

-- The toolbar, on the 1280x720 output, centred and a little wider or narrower
-- with the mode: these points are inside the buttons in every mode.
local REGION, WINDOW, SCREEN, TAKE, CLOSE = 514, 594, 672, 739, 785
local function toolbar(x)
    return function() auto.move_pointer(x, 625); auto.click("left") end
end

-- Each step, then this long before the next. A shot is named by the second it
-- was taken in, so they are kept apart.
local steps = {
    -- A region, with the mouse only. It opens in that mode.
    { 800, function() auto.key("Print") end },
    { 400, function() auto.drag(100, 150, 500, 450, "left", 8) end },
    { 1600, toolbar(TAKE) },
    -- A window: the toolbar's button changes the mode, a click takes it.
    { 800, function() auto.key("Print") end },
    { 500, toolbar(WINDOW) },
    { 500, function()
        local w = win("shot-a")
        shady.log(string.format("shot-test: WINDOW %d,%d %dx%d", w.x, w.y, w.geometry_width, w.geometry_height))
        auto.move_pointer(w.x + 40, w.y + 40)
    end },
    { 1600, function() auto.click("left") end },
    -- The screen, likewise.
    { 800, function() auto.key("Print") end },
    { 500, toolbar(SCREEN) },
    { 1600, toolbar(TAKE) },
    -- The keyboard does it all too.
    { 800, function() auto.key("Print") end },
    { 600, function() wtype("r") end },
    -- Outside the region it remembers: a new one.
    { 400, function() auto.drag(800, 300, 1000, 400, "left", 8) end },
    { 1600, function() wtype("-k Return") end },
    -- Inside it: the same region, moved.
    { 800, function() auto.key("Print") end },
    { 400, function() auto.drag(850, 350, 900, 400, "left", 8) end },
    { 1600, function() wtype("-k Return") end },
    -- Three ways out that take nothing: the close button, Escape, Print again.
    -- A bare click outside the region clears it, so Enter takes nothing either.
    { 800, function() auto.key("Print") end },
    { 500, toolbar(CLOSE) },
    { 500, function() auto.key("Print") end },
    { 600, function() wtype("-k Escape") end },
    { 500, function() auto.key("Print") end },
    { 500, function() auto.key("Print") end },
    { 500, function() auto.key("Print") end },
    { 400, function() auto.move_pointer(300, 300); auto.click("left") end },
    { 600, function() wtype("-k Return") end },
    { 400, function() wtype("-k Escape") end },
    -- And straight away, with no picker.
    { 1600, function() auto.key("Ctrl+Print") end },
    { 1600, function() auto.key("Alt+Print") end },
}
if os.getenv("SHOT_TEST_MODE") == "failure" then
    steps = { { 7000, function() auto.key("Ctrl+Print") end } }
elseif os.getenv("SHOT_TEST_MODE") == "scale" then
    steps = {
        { 800, function() auto.key("Print") end },
        { 400, function() auto.drag(50, 80, 450, 380, "left", 8) end },
        { 1600, function() wtype("-k Return") end },
    }
end
-- What the screen shows by now, the notification included.
local screen = os.getenv("SHOT_TEST_SCREEN") or ""
steps[#steps + 1] = { 800, function()
    if screen ~= "" then auto.screenshot(screen) end
end }
steps[#steps + 1] = { 800, function() shady.log("shot-test: DONE"); shady.quit() end }
local function run(i)
    if not steps[i] then return end
    steps[i][2]()
    after(steps[i][1], function() run(i + 1) end)
end

shady.on("window.mapped", function(window)
    if window.app_id == "shot-a" then spawn("shot-b")
    elseif window.app_id == "shot-b" then after(800, function() run(1) end) end
end)
-- Long enough for the shell to be up under a sanitizer.
after(3000, function() spawn("shot-a") end)
after(50000, function() shady.log("shot-test: timed out"); shady.quit() end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
