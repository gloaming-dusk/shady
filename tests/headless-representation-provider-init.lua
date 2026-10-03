local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-provider-probe" then return end
    started = true
    shady.automation.after(180, function()
        assert(shady.automation.key("Super+f"))
        shady.automation.after(180, function()
            -- Neon Transit binds debug-ray to Super+Shift+d. Enabling debug
            -- makes the renderer resolve the authoritative collision provider.
            assert(shady.automation.key("Super+Shift+d"))
            shady.automation.after(240, function()
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-provider-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation Provider", probe))
