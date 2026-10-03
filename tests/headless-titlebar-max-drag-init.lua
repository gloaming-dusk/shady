local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "titlebar-max-drag-probe" then return end
    started = true
    shady.automation.after(120, function()
        assert(window:maximize(true))
        shady.automation.after(180, function()
            assert(window.maximized, "window did not maximize")
            local tx = window.x + math.min(120, math.max(30, window.width * 0.2))
            local ty = window.y - 14
            shady.automation.drag(tx, ty, tx + 110, ty + 64, "left", 8)
            shady.automation.after(160, function()
                assert(not window.maximized, "titlebar drag did not restore maximized window")
                assert(window.x ~= 0 or window.y ~= 0, "restored window did not move")
                shady.log("titlebar-max-drag: PASS x=" .. window.x .. " y=" .. window.y)
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=titlebar-max-drag-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Titlebar Max Drag", probe))
