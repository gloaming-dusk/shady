-- Driver for capture.sh: the real Horizon runtime layer and shell, a
-- terminal over the sea, then either stills (one per hour, a notification, the
-- launcher and the control center) or the hero loop (hour changes timed to
-- the website's chips, with a notification and the control center).
local root = assert(os.getenv("SHADY_ROOT"))
dofile(root .. "/rices/horizon/init.lua")

local mode = assert(os.getenv("CAPTURE_MODE"))
local out = assert(os.getenv("CAPTURE_OUT"))
local status = assert(os.getenv("CAPTURE_STATUS"))
local auto = shady.automation
local after = auto.after

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end
local function wait_for(token, fn)
    local function poll()
        local f = io.open(status, "r")
        local text = f and f:read("a") or ""
        if f then f:close() end
        if text:find(token, 1, true) then fn() else after(20, poll) end
    end
    poll()
end

-- Out of the pictures: the bottom-right corner.
local function park() auto.move_pointer(1279, 719) end

-- The terminal opens in the middle of the sky; drag it by its title bar
-- (on screen, where the spatial view draws it) down onto the sea, clear of
-- the clock, the horizon, the toasts and the control center.
shady.on("window.mapped", function(window)
    if window.app_id ~= "capture-shell" then return end
    after(1200, function()
        auto.drag(450, 157, 340, 355, "left", 16)
        after(200, park)
        -- Let the shell, the glass and the window settle.
        after(2500, function() mark("READY") end)
    end)
end)

shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
after(750, function()
    shady.spawn(string.format("foot --config=%q --app-id=capture-shell --title=Shell --window-size-pixels=400x150 sh -c %q",
        os.getenv("CAPTURE_FOOT_INI"), "printf 'hello horizon\\n'; sleep 3600"))
end)

local function notify(summary, body, timeout)
    shady.spawn(string.format(
        "busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications "
        .. "org.freedesktop.Notifications Notify susssasa{sv}i Calendar 0 '' %q %q 0 0 %d",
        summary, body, timeout))
end

-- The status line's sliders icon, top right of the 1280x720 output.
local function toggle_control()
    auto.move_pointer(1220, 43)
    after(120, function()
        auto.click("left")
        after(120, park)
    end)
end

local function screenshot(name, k)
    auto.screenshot(out .. "/" .. name .. ".png", function(ok)
        assert(ok, "screenshot failed: " .. name)
        k()
    end)
end

-- Run steps one after another; each gets a `next` continuation.
local function sequence(steps)
    local function run(i)
        if steps[i] then steps[i](function() run(i + 1) end) end
    end
    run(1)
end
local function wait(ms) return function(k) after(ms, k) end end
local function shoot(name) return function(k) screenshot(name, k) end end
local function key(shortcut) return function(k) assert(auto.key(shortcut)); k() end end
-- Super+T cross-fades the hour over 2.4 s.
local function next_hour() return function(k) assert(auto.key("Super+t")); after(3200, k) end end

if mode == "stills" then
    wait_for("READY", function()
        sequence {
            shoot("hour-golden-hour"),
            next_hour(),
            shoot("hour-afterglow"),
            function(k) notify("Dinner on the terrace", "Seven o'clock, by the water", 4000); k() end,
            wait(1500),
            shoot("notification"),
            key("Super+d"),
            wait(900),
            function(k) assert(auto.type_text("e")); k() end,
            wait(1500),
            shoot("launcher"),
            -- Bindings only: automation keys never reach clients.
            key("Super+d"),
            wait(800),
            function(k) toggle_control(); k() end,
            wait(1500),
            shoot("control"),
            function(k) toggle_control(); k() end,
            wait(1200),
            next_hour(),
            shoot("hour-blue-hour"),
            next_hour(),
            shoot("hour-night"),
            function() mark("DONE"); shady.quit() end,
        }
    end)
else
    -- The website's hour chips switch at 2.3, 6.0, 10.2 and 14.3 s, the
    -- midpoints of the cross-fades, so press 1.2 s earlier.
    wait_for("RECORDING", function()
        for _, t in ipairs({ 1100, 4800, 9000, 13100 }) do
            after(t, function() assert(auto.key("Super+t")) end)
        end
        after(2000, function() notify("Dinner on the terrace", "Seven o'clock, by the water", 3500) end)
        after(6600, toggle_control)
        after(11600, toggle_control)
        after(17600, function()
            mark("DONE")
            after(500, function() shady.quit() end)
        end)
    end)
end
after(90000, function() shady.quit() end)
