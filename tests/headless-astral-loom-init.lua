dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/astral-loom/init.lua")
local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local screenshot = assert(os.getenv("SHADY_ASTRAL_SCREENSHOT"))
local originals, count, started = {}, 0, false
local closed = false
shady.on("window.destroyed", function(w)
    closed = true
end)
local function find(id)
    for _, w in ipairs(shady.windows()) do if w.app_id == id then return w end end
end
local function after(ms, fn) shady.automation.after(ms, fn) end
local function key(chord) assert(shady.automation.key(chord), chord .. " was not handled") end

local function screenshot_retry(attempts, callback)
    local pid, err = shady.automation.screenshot(screenshot, function(ok)
        if ok then
            callback()
            return
        end
        assert(attempts > 1, "screenshot failed after retries")
        after(200, function()
            screenshot_retry(attempts - 1, callback)
        end)
    end)
    assert(pid, err)
end
shady.on("window.mapped", function(w)
    if not w.app_id:match("^astral%-probe%-") then return end
    originals[w.app_id] = {x=w.x, y=w.y, z=w.z}
    count = count + 1
    if count ~= 4 or started then return end
    started = true
    after(700, function()
        local a = assert(find("astral-probe-1"))
        assert(a:focus())
        key("Super+Shift+j")
        after(800, function()
            local b = assert(find("astral-probe-2"))
            assert(math.abs(a.z-b.z) > .12, "layout has no spatial separation")
            assert(math.abs(a.x-b.x) > 10 or math.abs(a.y-b.y) > 10, "layout collapsed")
            key("Super+b")
            local z = b.z
            after(200, function()
                assert(math.abs(b.z-z) < .1, "pause produced a large jump")
                screenshot_retry(3, function()
                    key("Super+j")
                    after(1800, function()
                        for id, original in pairs(originals) do
                            local window = assert(find(id))
                            assert(math.abs(window.x-original.x) <= 1, "X restoration failed")
                            assert(math.abs(window.y-original.y) <= 1, "Y restoration failed")
                            assert(math.abs(window.z-original.z) < .001, "Z restoration failed")
                        end
                        key("Super+j")
                        assert(shady.reload_plugin("astral-loom"), "reload failed")
                        after(250, function()
                            key("Super+j")
                            after(1800, function()
                                for id, original in pairs(originals) do
                                    local window = assert(find(id))
                                    assert(math.abs(window.x-original.x) <= 1, "reload lost original X")
                                    assert(math.abs(window.y-original.y) <= 1, "reload lost original Y")
                                    assert(math.abs(window.z-original.z) < .001, "reload lost original Z")
                                end
                                assert(find("astral-probe-4"):close())
                                after(1200, function()
                                    assert(closed and not find("astral-probe-4"), "window was not destroyed")
                                    shady.log("astral-loom-test: PASS depth, helix, restore, screenshot, reload, close")
                                    shady.quit()
                                end)
                            end)
                        end)
                    end)
                end)
            end)
        end)
    end)
end)
local colors = {"0xff1a6680", "0xff593a86", "0xff237368", "0xff9b4b46"}
for i=1,4 do
    shady.spawn(string.format("SHADY_AUTOMATION_PROBE_APP_ID=astral-probe-%d SHADY_AUTOMATION_PROBE_COLOR=%s %q", i, colors[i], probe))
end
