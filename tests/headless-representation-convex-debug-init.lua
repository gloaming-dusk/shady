local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-convex-debug-probe" then return end
    started = true
    shady.automation.after(160, function()
        assert(shady.automation.key("F2"))
        shady.automation.after(180, function()
            assert(shady.automation.key("F5"))
            shady.automation.after(240, function()
                shady.log("representation-convex-debug: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-convex-debug-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation Convex Debug", probe))
