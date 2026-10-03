local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "representation-mesh-reload-probe" then return end
    started = true
    shady.automation.after(160, function()
        assert(shady.automation.key("Super+f"))
        shady.automation.after(220, function()
            assert(shady.reload_plugin("fps-folded-paper"),
                "failed to reload fps-folded-paper")
            shady.automation.after(220, function()
                shady.log("representation-mesh-reload: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=representation-mesh-reload-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Representation Mesh Reload", probe))
