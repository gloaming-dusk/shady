local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false
local destroyed = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "close-burn-probe" then return end
    started = true
    shady.automation.after(180, function()
        assert(window:close(), "failed to start close")
    end)
end)

shady.on("window.destroyed", function(window)
    if destroyed then return end
    destroyed = true
    shady.log("close-burn: PASS")
    shady.automation.after(80, function()
        shady.quit()
    end)
end)

shady.automation.after(4000, function()
    assert(destroyed, "close-burn: window did not close")
    shady.quit()
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=close-burn-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Close Burn", probe))
