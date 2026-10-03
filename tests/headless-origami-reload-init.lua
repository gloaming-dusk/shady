local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "origami-reload-probe" then return end
    started = true
    shady.automation.after(160, function()
        assert(shady.automation.key("F2"))
        shady.automation.after(220, function()
            assert(shady.reload_plugin("fps-origami"),
                "failed to reload fps-origami")
            shady.automation.after(220, function()
                shady.log("origami-reload: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=origami-reload-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Origami Reload", probe))
