local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local windows = {}
local started = false

local function maybe_start()
    if started or #windows < 3 then return end
    started = true

    shady.automation.after(180, function()
        assert(windows[1]:focus())
        assert(shady.automation.key("Super+p"))
        shady.automation.after(360, function()
            assert(shady.reload_plugin("window-portal"),
                "failed to reload window-portal")
            shady.automation.after(320, function()
                assert(shady.automation.key("Super+p"))
                shady.automation.after(360, function()
                    assert(shady.automation.key("Super+Shift+p"))
                    shady.log("portal-reload: PASS")
                    shady.quit()
                end)
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "portal-reload-a" and
            window.app_id ~= "portal-reload-b" and
            window.app_id ~= "portal-reload-c" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

for _, id in ipairs({"portal-reload-a", "portal-reload-b", "portal-reload-c"}) do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_TITLE=%q %q",
        id, id, probe))
end
