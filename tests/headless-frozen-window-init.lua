-- Super+Z without a focused window does nothing. With a live probe window:
-- freeze it, thaw it, freeze and thaw the workspace, then hot-reload and
-- unload while frozen. Shaders are compiled at plugin init, so a GLSL error
-- fails the load before any of this runs. Set SHADY_FROZEN_SCREENSHOT to
-- save the fully frozen window.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local screenshot = os.getenv("SHADY_FROZEN_SCREENSHOT")
local after = shady.automation.after
local started = false

local function finish()
    assert(shady.automation.key("Super+Shift+z")) -- freeze the workspace
    after(600, function()
        assert(shady.automation.key("Super+Shift+z")) -- thaw it
        after(600, function()
            assert(shady.automation.key("Super+Shift+z")) -- freeze, then reload mid-freeze
            after(300, function()
                assert(shady.plugins.reload("frozen-window"))
                after(200, function()
                    assert(shady.plugins.unload("frozen-window"))
                    shady.log("frozen-window-test: PASS")
                    shady.quit()
                end)
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if started or window.app_id ~= "frozen-probe" then return end
    started = true
    assert(window:focus())
    after(200, function()
        assert(shady.automation.key("Super+z")) -- freeze
        after(1900, function()
            local function thaw()
                assert(shady.automation.key("Super+z"))
                after(1600, finish)
            end
            if screenshot and screenshot ~= "" then
                shady.automation.screenshot(screenshot, function(ok)
                    assert(ok, "frozen-window screenshot failed")
                    thaw()
                end)
            else
                thaw()
            end
        end)
    end)
end)

after(200, function()
    assert(shady.automation.key("Super+z")) -- nothing focused yet
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=frozen-probe " ..
        "SHADY_AUTOMATION_PROBE_PATTERN=1 " ..
        "SHADY_AUTOMATION_PROBE_COLOR=0xff245d78 %q", probe))
end)
