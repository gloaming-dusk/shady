-- Afterglow rice: sky hook renders, Super+T cycles the hour both ways while
-- the transition keeps frames flowing, and a closing window sinks cleanly.
dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/afterglow/init.lua")
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local mapped, started, destroyed = 0, false, false

local function after(ms, fn) shady.automation.after(ms, fn) end
local function key(chord) assert(shady.automation.key(chord), chord .. " was not handled") end
local function find(id)
    for _, w in ipairs(shady.windows()) do if w.app_id == id then return w end end
end

shady.on("window.destroyed", function()
    destroyed = true
end)

shady.on("window.mapped", function(w)
    if not w.app_id:match("^afterglow%-probe%-") then return end
    mapped = mapped + 1
    if mapped ~= 2 or started then return end
    started = true
    after(300, function()
        key("Super+t")
        after(2800, function()
            key("Super+Shift+t")
            after(2800, function()
                assert(find("afterglow-probe-2"):close(), "close request failed")
                after(1500, function()
                    assert(destroyed and not find("afterglow-probe-2"),
                        "closing window did not finish sinking")
                    shady.log("afterglow-test: PASS sky, hour cycle, sink close")
                    shady.quit()
                end)
            end)
        end)
    end)
end)

for i = 1, 2 do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=afterglow-probe-%d SHADY_AUTOMATION_PROBE_COLOR=0xff3a2440 %q",
        i, probe))
end
