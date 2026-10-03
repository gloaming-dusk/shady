local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-collision-probe" then return end
    started = true
    shady.automation.after(180, function()
        assert(shady.automation.key("F2"))
        shady.automation.after(120, function()
            assert(shady.automation.key("F4"))
            shady.automation.after(500, function()
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-collision-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation Collision", probe))
