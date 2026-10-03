local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_PORTAL_STATUS"))
local windows = {}
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function maybe_start()
    if started or #windows < 3 then return end
    started = true

    shady.automation.after(180, function()
        assert(windows[1]:focus())
        assert(shady.automation.key("Super+p"))
        mark("OPEN=PASS")
        shady.automation.after(420, function()
            assert(shady.automation.key("Super+p"))
            mark("CYCLE=PASS")
            shady.automation.after(420, function()
                assert(shady.automation.key("Super+Shift+p"))
                mark("CLOSE=PASS")
                shady.automation.after(220, function()
                    shady.quit()
                end)
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "portal-a" and
            window.app_id ~= "portal-b" and
            window.app_id ~= "portal-c" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

local specs = {
    {"portal-a", "Portal A", "0xff6b3040"},
    {"portal-b", "Portal B", "0xff2060a0"},
    {"portal-c", "Portal C", "0xff40a060"},
}

for _, spec in ipairs(specs) do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_TITLE=%q SHADY_AUTOMATION_PROBE_COLOR=%s %q",
        spec[1], spec[2], spec[3], probe))
end
