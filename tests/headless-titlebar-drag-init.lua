local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "titlebar-drag-probe" then return end
    started = true

    shady.automation.after(180, function()
        local x0, y0 = window.x, window.y
        local tx = x0 + math.min(80, math.max(20, window.width * 0.25))
        local ty = y0 - 14
        shady.automation.drag(tx, ty, tx + 96, ty + 52, "left", 8)

        shady.automation.after(80, function()
            local dx, dy = window.x - x0, window.y - y0
            assert(math.abs(dx - 96) <= 2, "titlebar drag x mismatch: " .. dx)
            assert(math.abs(dy - 52) <= 2, "titlebar drag y mismatch: " .. dy)
            shady.log("titlebar-drag: PASS dx=" .. dx .. " dy=" .. dy)
            shady.quit()
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=titlebar-drag-probe SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Titlebar Drag", probe))
