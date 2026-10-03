local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status_path = assert(os.getenv("SHADY_SPATIAL_DAILY_STATUS"))
local mapped = {}
local started = false
local destroyed_b = false

local function mark(line)
    local f = assert(io.open(status_path, "a"))
    f:write(line, "\n")
    f:close()
end

local function find(app_id)
    for _, w in ipairs(shady.windows()) do
        if w.app_id == app_id then return w end
    end
end

shady.on("window.destroyed", function(w)
    if started then destroyed_b = true end
end)

shady.on("window.mapped", function(w)
    if w.app_id ~= "spatial-daily-a" and w.app_id ~= "spatial-daily-b" then return end
    mapped[w.app_id] = true
    if started or not mapped["spatial-daily-a"] or not mapped["spatial-daily-b"] then return end
    started = true

    shady.automation.after(120, function()
        local a = assert(find("spatial-daily-a"))
        local b = assert(find("spatial-daily-b"))
        assert(a.mapped and b.mapped, "probe windows are not mapped")
        assert(a.z ~= nil and b.z ~= nil, "spatial z state is unavailable")
        mark("LIFECYCLE=PASS")

        assert(a:focus(), "focus failed")
        shady.automation.after(80, function()
            local focused = assert(shady.focused_window(), "no focused window")
            assert(focused.app_id == "spatial-daily-a", "focus did not move to probe A")
            mark("FOCUS=PASS")

            assert(a:maximize(true), "maximize request failed")
            shady.automation.after(80, function()
                assert(a.maximized, "maximize state did not stick")
                assert(a:maximize(false), "unmaximize request failed")
                shady.automation.after(80, function()
                    assert(not a.maximized, "maximize state did not clear")
                    mark("MAXIMIZE=PASS")

                    assert(a:set_fullscreen(true), "fullscreen request failed")
                    shady.automation.after(80, function()
                        assert(a.fullscreen, "fullscreen state did not stick")
                        assert(a:set_fullscreen(false), "fullscreen exit failed")
                        shady.automation.after(80, function()
                            assert(not a.fullscreen, "fullscreen state did not clear")
                            mark("FULLSCREEN=PASS")

                            assert(b:close(), "close request failed")
                            shady.automation.after(220, function()
                                assert(not find("spatial-daily-b"), "closed window is still live")
                                assert(destroyed_b, "destroy event was not observed")
                                mark("CLOSE=PASS")
                                shady.log("spatial-daily-smoke: PASS lifecycle, focus, maximize, fullscreen, close")
                                shady.quit()
                            end)
                        end)
                    end)
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=spatial-daily-a SHADY_AUTOMATION_PROBE_COLOR=0xff305070 %q",
    probe))
shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=spatial-daily-b SHADY_AUTOMATION_PROBE_COLOR=0xff704030 %q",
    probe))
