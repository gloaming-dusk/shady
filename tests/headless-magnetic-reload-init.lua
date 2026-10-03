local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local mapped = 0
local reloaded = false

shady.on("window.mapped", function(window)
    if window.app_id ~= "magnetic-reload-a" and
            window.app_id ~= "magnetic-reload-b" then return end
    mapped = mapped + 1
    if mapped < 2 or reloaded then return end
    reloaded = true
    shady.automation.after(260, function()
        assert(shady.reload_plugin("magnetic-windows"),
            "failed to reload magnetic-windows")
        shady.automation.after(500, function()
            shady.log("magnetic-reload: PASS")
            shady.quit()
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=magnetic-reload-a SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Magnetic Reload A", probe))
shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=magnetic-reload-b SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Magnetic Reload B", probe))
