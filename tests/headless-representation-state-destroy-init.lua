local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false
local destroyed = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-state-probe" then return end
    started = true
    shady.automation.after(180, function()
        assert(shady.automation.key("Super+f"))
        shady.automation.after(220, function()
            assert(window:close())
        end)
    end)
end)

shady.on("window.destroyed", function(window)
    if destroyed then return end
    destroyed = true
    shady.log("representation-state-destroy: PASS")
    shady.automation.after(100, function()
        shady.quit()
    end)
end)

shady.automation.after(4000, function()
    assert(destroyed, "representation state window did not destroy")
    shady.quit()
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-state-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation State", probe))
