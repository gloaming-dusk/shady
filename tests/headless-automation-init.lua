local screenshot = assert(os.getenv("SHADY_AUTOMATION_SCREENSHOT"))
local shortcut_hit = false
local hold_key_hit = false
local finished = false

shady.bind("Ctrl+F12", function()
    shortcut_hit = true
    shady.log("headless-automation: shortcut PASS")
end)

shady.bind("F10", function()
    hold_key_hit = true
end)

shady.on("window.mapped", function(window)
    if finished or window.app_id ~= "shady-automation-probe" then return end
    finished = true

    shady.automation.after(100, function()
        assert(shady.automation.key("Ctrl+F12"))
        assert(shortcut_hit, "automation shortcut did not fire")

        assert(shady.automation.key_down("F10"))
        assert(hold_key_hit, "key_down did not reach Lua keybinding")
        shady.automation.key_up("F10")

        local cx = window.x + window.width * 0.5
        local cy = window.y + window.height * 0.5

        shady.automation.move_pointer(cx, cy)
        shady.automation.click("left")

        shady.automation.after(80, function()
            shady.automation.drag(
                window.x + 16, window.y + 16,
                window.x + 120, window.y + 60,
                "left", 6
            )

            shady.automation.move_pointer(cx, cy)
            shady.automation.scroll(1, 0)

            local type_pid, type_err = shady.automation.type_text("K")
            assert(type_pid ~= nil, type_err or "type_text failed")

            shady.automation.after(350, function()
                local pid, err = shady.automation.screenshot(screenshot, function(ok, path)
                    assert(ok, "screenshot callback reported failure")
                    assert(path == screenshot, "screenshot callback path mismatch")
                    local file = assert(io.open(path, "rb"))
                    local size = assert(file:seek("end"))
                    file:close()
                    assert(size > 100, "screenshot file is empty")
                    shady.log("headless-automation: screenshot callback PASS bytes=" .. size)
                    shady.log("headless-automation: PASS")
                    shady.quit()
                end)
                assert(pid ~= nil, err or "failed to launch screenshot")
            end)
        end)
    end)
end)

local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
shady.spawn(string.format("%q", probe))
