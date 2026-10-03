local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local screenshot = assert(os.getenv("SHADY_AUTOMATION_SCREENSHOT"))

local last_focused = nil
local spawned_b = false
local finished = false

shady.on("window.focused", function(window)
    last_focused = window.app_id
end)

shady.on("window.mapped", function(window)
    if window.app_id == "shady-alt-tab-a" and not spawned_b then
        spawned_b = true
        shady.spawn(string.format(
            "SHADY_AUTOMATION_PROBE_APP_ID=shady-alt-tab-b " ..
            "SHADY_AUTOMATION_PROBE_COLOR=0xff304f80 %q",
            probe
        ))
        return
    end

    if finished or window.app_id ~= "shady-alt-tab-b" then return end
    finished = true

    shady.automation.after(100, function()
        assert(last_focused == "shady-alt-tab-b",
            "second mapped window did not receive focus")

        assert(shady.automation.key("Alt+Tab"),
            "Alt+Tab was not handled by compositor")
        assert(last_focused == "shady-alt-tab-a",
            "first Alt+Tab did not focus previous window")

        local pid, err = shady.automation.screenshot(screenshot, function(ok, path)
            assert(ok, "Alt+Tab screenshot failed")
            assert(path == screenshot)

            assert(shady.automation.key("Alt+Tab"),
                "second Alt+Tab was not handled by compositor")
            assert(last_focused == "shady-alt-tab-b",
                "second Alt+Tab did not cycle back")

            shady.log("alt-tab-test: PASS")
            shady.quit()
        end)
        assert(pid ~= nil, err or "failed to launch Alt+Tab screenshot")
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=shady-alt-tab-a " ..
    "SHADY_AUTOMATION_PROBE_COLOR=0xff803030 %q",
    probe
))
