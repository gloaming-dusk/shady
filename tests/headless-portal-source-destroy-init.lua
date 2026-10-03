local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local windows = {}
local started = false

local function maybe_start()
    if started or #windows < 3 then return end
    started = true

    shady.automation.after(180, function()
        assert(windows[1]:focus())
        assert(shady.automation.key("Super+p"))
        shady.automation.after(320, function()
            assert(windows[2]:close())
            shady.automation.after(420, function()
                if windows[3].mapped then assert(windows[3]:close()) end
                shady.automation.after(420, function()
                    shady.log("portal-source-destroy: PASS")
                    shady.quit()
                end)
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "portal-destroy-a" and
            window.app_id ~= "portal-destroy-b" and
            window.app_id ~= "portal-destroy-c" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

for _, id in ipairs({"portal-destroy-a", "portal-destroy-b", "portal-destroy-c"}) do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_TITLE=%q %q",
        id, id, probe))
end
