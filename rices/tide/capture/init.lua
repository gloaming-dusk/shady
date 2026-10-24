-- Driver for capture.sh: Tide's runtime layer and shell with three
-- terminals in the strip, then the stills.
local root = assert(os.getenv("SHADY_ROOT"))
dofile(root .. "/rices/tide/init.lua")

local out = assert(os.getenv("CAPTURE_OUT"))
local auto = shady.automation
local after = auto.after
local L = shady.layout

local ini = os.getenv("CAPTURE_FOOT_INI")
local function term(id, title, script)
    shady.spawn(string.format("foot --config=%q --app-id=%s --title=%q sh -c %q",
        ini, id, title, script .. "; sleep 3600"))
end

local function screenshot(name, k)
    auto.screenshot(out .. "/" .. name .. ".png", function(ok)
        assert(ok, "screenshot failed: " .. name)
        k()
    end)
end
local function sequence(steps)
    local function run(i) if steps[i] then steps[i](function() run(i + 1) end) end end
    run(1)
end
local function wait(ms) return function(k) after(ms, k) end end
local function shoot(name) return function(k) screenshot(name, k) end end
local function act(fn) return function(k) fn(); k() end end
local serial = 100
local function tide(action) serial = serial + 1; shady.publish("tide.do", action .. " " .. serial) end
local function notify(summary, body)
    shady.spawn(string.format(
        "busctl --user call org.freedesktop.Notifications /org/freedesktop/Notifications "
        .. "org.freedesktop.Notifications Notify susssasa{sv}i Calendar 0 '' %q %q 0 0 6000",
        summary, body))
end

auto.move_pointer(1279, 719)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
after(700, function() term("notes", "notes", "printf '\\n  tide\\n\\n  a strip of columns that scrolls sideways\\n  ' ; cat /dev/null") end)
after(1200, function() term("build", "build", "printf '\\n  $ ninja -C build\\n  [312/312] Linking target shady\\n\\n  $ ./tests/headless-layout.sh\\n  layout: PASS\\n'") end)
after(1700, function() term("logs", "logs", "printf '\\n  focus  ->  build\\n  column ->  2 of 3\\n'") end)
after(2200, function() term("top", "top", "printf '\\n  under the third column\\n'") end)

after(4200, function()
    sequence {
        act(function() L.consume_or_expel("left") end),
        act(function() L.focus("left") end),
        wait(900),
        act(function() notify("Standup in 10 minutes", "Room 3 · bring the layout demo") end),
        wait(1400),
        shoot("preview"),
        act(function() tide("control") end),
        wait(1200),
        shoot("control"),
        act(function() tide("control") end),
        wait(500),
        act(function() shady.toggle_launcher() end),
        wait(500),
        act(function() auto.type_text("te") end),
        wait(900),
        shoot("launcher"),
        act(function() shady.toggle_launcher() end),
        wait(500),
        act(function() L.overview(true) end),
        wait(1800),
        shoot("overview"),
        act(function() L.overview(false) end),
        wait(1200),
        act(function() tide("lock") end),
        wait(1500),
        shoot("lock"),
        function() shady.quit() end,
    }
end)
after(60000, function() shady.quit() end)
