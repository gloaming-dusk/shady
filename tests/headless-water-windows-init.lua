local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_WATER_STATUS"))
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "water-probe" then return end
    started = true
    assert(window:maximize(), "failed to maximize water probe")

    shady.automation.after(500, function()
        mark("WATER_ON_A")
        shady.automation.after(650, function()
            mark("WATER_ON_B")
            shady.automation.after(350, function()
                assert(shady.automation.key("Super+w"),
                    "water plugin did not handle Super+W")
                -- Let the final toggle-triggered frame land before comparing
                -- disabled captures. After that, demand-driven rendering
                -- should leave the image stable.
                shady.automation.after(250, function()
                    mark("WATER_OFF_A")
                    shady.automation.after(650, function()
                        mark("WATER_OFF_B")
                        shady.automation.after(300, function()
                            mark("DONE")
                            shady.quit()
                        end)
                    end)
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=water-probe " ..
    "SHADY_AUTOMATION_PROBE_PATTERN=1 " ..
    "SHADY_AUTOMATION_PROBE_COLOR=0xff245d78 %q", probe))
