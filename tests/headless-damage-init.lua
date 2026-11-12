-- A wallpaper, two windows and a frosted-glass bar change bits of themselves
-- on their own clocks while the focus moves between the windows; then, once
-- everything is still, the frame is saved. headless-damage.sh runs this with
-- damage tracking and with SHADY_DAMAGE=0 and wants the same pixels.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local path = assert(os.getenv("SHOT_PATH"))
local A = shady.automation

local function spawn(env)
    shady.spawn(string.format("%s %q", env, probe))
end

shady.layer_effect("damage-bar", { blur = 12 })

local mapped = 0
shady.on("window.mapped", function()
    mapped = mapped + 1
    if mapped ~= 2 then return end
    -- The focus moves while the clients tick: a frame of everything, then
    -- partial ones again.
    A.after(700, function() assert(shady.layout.focus("left")) end)
    A.after(1400, function() assert(shady.layout.focus("right")) end)
    -- Ticks end by ~2.2 s; leave the layout time to settle.
    A.after(3600, function()
        assert(shady.screenshot(path) == path)
        A.after(800, function()
            shady.log("damage-test: DONE")
            shady.quit()
        end)
    end)
end)

spawn("SHADY_AUTOMATION_PROBE_LAYER=background SHADY_AUTOMATION_PROBE_APP_ID=damage-wall " ..
    "SHADY_AUTOMATION_PROBE_COLOR=0xff4db3ff SHADY_AUTOMATION_PROBE_TICK_MS=55 SHADY_AUTOMATION_PROBE_TICKS=36")
spawn("SHADY_AUTOMATION_PROBE_LAYER=top SHADY_AUTOMATION_PROBE_APP_ID=damage-bar " ..
    "SHADY_AUTOMATION_PROBE_COLOR=0x80ffffff SHADY_AUTOMATION_PROBE_TICK_MS=130 SHADY_AUTOMATION_PROBE_TICKS=12")
spawn("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=damage-a " ..
    "SHADY_AUTOMATION_PROBE_PATTERN=1 SHADY_AUTOMATION_PROBE_TICK_MS=40 SHADY_AUTOMATION_PROBE_TICKS=40")
A.after(200, function()
    spawn("SHADY_AUTOMATION_PROBE_RESIZABLE=1 SHADY_AUTOMATION_PROBE_APP_ID=damage-b " ..
        "SHADY_AUTOMATION_PROBE_COLOR=0xff2a2440 SHADY_AUTOMATION_PROBE_TICK_MS=70 SHADY_AUTOMATION_PROBE_TICKS=25")
end)
