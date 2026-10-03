local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-reload-probe" then return end
    started = true
    shady.automation.after(160, function()
        assert(shady.automation.key("Super+f"))
        shady.automation.after(180, function()
            assert(shady.reload_plugin("fps-squash"),
                "failed to reload fps-squash provider")
            shady.automation.after(180, function()
                -- Force another render after the old DSO has been closed.
                assert(shady.automation.key("Super+Shift+d"))
                shady.automation.after(180, function()
                    shady.log("representation-reload: PASS")
                    shady.quit()
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-reload-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation Reload", probe))
