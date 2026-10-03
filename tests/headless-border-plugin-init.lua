local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_BORDER_STATUS"))
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "border-probe" then return end
    started = true
    shady.automation.after(250, function()
        assert(shady.automation.key("Super+b"), "border override key not handled")
        mark("OVERRIDE_KEY=PASS")
        shady.automation.after(250, function()
            assert(shady.automation.key("Super+b"), "border reset key not handled")
            mark("RESET_KEY=PASS")
            shady.automation.after(250, function()
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=border-probe SHADY_AUTOMATION_PROBE_PATTERN=1 %q",
    probe))
