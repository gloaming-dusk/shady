local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_FOCUS_DEPTH_STATUS"))
local mapped = {}
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function find_window(app_id)
    for _, window in ipairs(shady.windows()) do
        if window.app_id == app_id then return window end
    end
    return nil
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "focus-depth-a" and window.app_id ~= "focus-depth-b" then
        return
    end
    mapped[window.app_id] = true

    if window.app_id == "focus-depth-a" and not mapped["focus-depth-b"] then
        shady.automation.after(80, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=focus-depth-b " ..
                "SHADY_AUTOMATION_PROBE_COLOR=0xff603050 %q", probe))
        end)
        return
    end

    if started or not mapped["focus-depth-a"] or not mapped["focus-depth-b"] then
        return
    end
    started = true

    shady.automation.after(700, function()
        local a = assert(find_window("focus-depth-a"))
        local b = assert(find_window("focus-depth-b"))
        assert(a.z ~= nil and b.z ~= nil, "spatial z unavailable")
        assert(b.z > a.z + 0.12,
            string.format("focused depth ordering wrong: a=%.3f b=%.3f", a.z, b.z))
        mark("INITIAL_DEPTH=PASS")

        assert(shady.automation.key("Super+Tab"), "cycle binding was not handled")
        shady.automation.after(700, function()
            a = assert(find_window("focus-depth-a"))
            b = assert(find_window("focus-depth-b"))
            assert(a.z > b.z + 0.12,
                string.format("focus depth did not reverse: a=%.3f b=%.3f", a.z, b.z))
            mark("FOCUS_SWAP=PASS")
            shady.log("focus-depth-test: PASS")
            shady.quit()
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=focus-depth-a " ..
    "SHADY_AUTOMATION_PROBE_COLOR=0xff305070 %q", probe))
