-- Rewind: a layout change is recorded once it settles, Super+BackSpace steps
-- back to it, Return restores it, and the restore can itself be rewound.
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local started = false
local SETTLE = 1500

local function find_window()
    for _, window in ipairs(shady.windows()) do
        if window.app_id == "rewind-a" then return window end
    end
    return nil
end

local function expect_value(key, wanted)
    local got = shady.value(key)
    assert(got == wanted, string.format("%s: wanted %s, got %s", key, tostring(wanted), tostring(got)))
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "rewind-a" or started then return end
    started = true

    shady.automation.after(SETTLE, function()
        local a = assert(find_window())
        local home_x, home_y = a.x, a.y
        expect_value("rewind.frames", "1")
        assert(a:maximize(true), "maximize failed")

        shady.automation.after(SETTLE, function()
            expect_value("rewind.frames", "2")
            assert(shady.automation.key("Super+BackSpace"), "Super+BackSpace was not handled")
            expect_value("rewind.active", "1")
            expect_value("rewind.position", "1")
            assert(shady.automation.key("Return"), "Return was not handled")
            expect_value("rewind.active", nil)

            shady.automation.after(SETTLE, function()
                a = assert(find_window())
                assert(not a.maximized, "rewind did not unmaximize the window")
                assert(math.abs(a.x - home_x) <= 3 and math.abs(a.y - home_y) <= 3,
                    string.format("window not back home: %d,%d vs %d,%d", a.x, a.y, home_x, home_y))
                expect_value("rewind.frames", "3")

                -- The restore is history too: one step back is maximized again.
                assert(shady.automation.key("Super+BackSpace"))
                expect_value("rewind.position", "2")
                assert(shady.automation.key("Return"))
                shady.automation.after(SETTLE, function()
                    a = assert(find_window())
                    assert(a.maximized, "rewinding the rewind did not maximize")

                    -- Escape leaves the layout as it is.
                    assert(shady.automation.key("Super+BackSpace"))
                    assert(shady.automation.key("Escape"))
                    expect_value("rewind.active", nil)
                    shady.automation.after(400, function()
                        assert(assert(find_window()).maximized, "Escape changed the layout")
                        shady.log("rewind-test: PASS")
                        shady.quit()
                    end)
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=rewind-a SHADY_AUTOMATION_PROBE_COLOR=0xff507090 %q", probe))
