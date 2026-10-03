local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_CLOSE_STATUS"))
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function find_probe()
    for _, window in ipairs(shady.windows()) do
        if window.app_id == "close-effect-probe" then return window end
    end
end

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "close-effect-probe" then return end
    started = true
    shady.automation.after(260, function()
        assert(window:close(), "Lua window:close() failed")
        mark("CLOSE_API=PASS")
        shady.automation.after(120, function()
            assert(find_probe(), "slide-fade close was not delayed")
            mark("MID_ALIVE=PASS")
            shady.automation.after(700, function()
                assert(not find_probe(), "window did not close after slide-fade duration")
                mark("CLOSED=PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=close-effect-probe " ..
    "SHADY_AUTOMATION_PROBE_TITLE=%q SHADY_AUTOMATION_PROBE_PATTERN=1 %q",
    "Close Effect API", probe))
