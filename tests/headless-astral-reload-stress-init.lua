dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/astral-loom/init.lua")

local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status_path = assert(os.getenv("SHADY_ASTRAL_RELOAD_STATUS"))
local reloads = tonumber(os.getenv("SHADY_ASTRAL_RELOAD_ROUNDS") or "8")

local mapped = 0
local windows = {}
local originals = {}
local destroyed = 0
local closing = false
local step = 0

local function mark(line)
    local f = assert(io.open(status_path, "a"))
    f:write(line, "\n")
    f:close()
end

local function module_active(name)
    for _, module in ipairs(shady.modules()) do
        if module.name == name then return module.active end
    end
    return false
end

local function assert_live()
    for i = 1, 4 do
        local w = assert(windows[i], "missing reload-stress window")
        assert(tostring(w) ~= "Window<dead>", "reload-stress window died")
        assert(w.mapped and w.z ~= nil, "reload-stress window lost spatial state")
    end
    assert(module_active("astral-loom"), "Astral Loom module is inactive")
end

local run_step

local function after_reload(current)
    shady.automation.after(220, function()
        assert_live()

        if current == 2 then
            assert(shady.workspace("reload-alt"))
            assert(windows[1].visible, "hidden workspace window did not recover after reload")
            assert(shady.workspace("main"))
        elseif current == 3 then
            assert(windows[2].maximized, "maximize state was lost across reload")
            assert(windows[2]:maximize(false), "failed to clear maximize after reload")
        elseif current == 4 then
            assert(windows[3].fullscreen, "fullscreen state was lost across reload")
            assert(windows[3]:set_fullscreen(false), "failed to clear fullscreen after reload")
        elseif current == 6 then
            assert(shady.automation.key("Super+b"), "failed to resume Astral after reload")
        elseif current == 7 then
            shady.fold_all()
            assert(shady.automation.key("w"), "FPS capture did not recover after reload")
        end

        shady.automation.after(80, run_step)
    end)
end

run_step = function()
    step = step + 1
    if step > reloads then
        assert(shady.workspace("main"))
        for i = 1, 4 do
            if windows[i].workspace ~= "main" then
                assert(windows[i]:move_to_workspace("main"))
            end
        end

        -- Deactivate Astral and verify the original per-window geometry survived
        -- all V2 migration cycles.
        assert(shady.automation.key("Super+j"), "failed to restore Astral layout")
        shady.automation.after(1900, function()
            for i = 1, 4 do
                local w = windows[i]
                local o = originals[i]
                assert(math.abs(w.x - o.x) <= 1,
                    "reload stress lost original X for window " .. i)
                assert(math.abs(w.y - o.y) <= 1,
                    "reload stress lost original Y for window " .. i)
                assert(math.abs(w.z - o.z) < .001,
                    "reload stress lost original Z for window " .. i)
            end
            mark("STATE_MIGRATION=PASS")
            closing = true
            for i = 1, 4 do
                assert(windows[i]:close(), "failed to close reload-stress window")
            end
        end)
        return
    end

    assert_live()
    local focus_target = nil
    for offset = 0, 3 do
        local candidate = windows[((step - 1 + offset) % 4) + 1]
        if candidate.visible then
            focus_target = candidate
            break
        end
    end
    assert(focus_target and focus_target:focus(), "focus failed before reload")

    if step == 2 then
        assert(windows[1]:move_to_workspace("reload-alt"),
            "failed to hide window before reload")
        assert(not windows[1].visible, "workspace-moved window stayed visible")
    elseif step == 3 then
        assert(windows[2]:maximize(true), "failed to maximize before reload")
    elseif step == 4 then
        assert(windows[3]:set_fullscreen(true), "failed to fullscreen before reload")
    elseif step == 5 then
        assert(shady.automation.key("Super+Shift+j"),
            "failed to toggle helix before reload")
    elseif step == 6 then
        assert(shady.automation.key("Super+b"),
            "failed to pause Astral before reload")
    elseif step == 7 then
        shady.expand_all()
        assert(not shady.automation.key("w"),
            "FPS input remained captured after expand_all")
    elseif step == 8 then
        for _ = 1, 6 do
            assert(shady.automation.key("Super+Tab"), "focus cycle failed before reload")
        end
    end

    assert(shady.reload_plugin("astral-loom"),
        "Astral Loom reload request failed at step " .. step)
    after_reload(step)
end

shady.on("window.mapped", function(w)
    local index = w.app_id and w.app_id:match("^astral%-reload%-(%d+)$")
    if not index then return end
    index = tonumber(index)
    windows[index] = w
    originals[index] = {x = w.x, y = w.y, z = w.z}
    mapped = mapped + 1
    if mapped == 4 then
        shady.automation.after(600, run_step)
    end
end)

shady.on("window.destroyed", function(_w)
    if not closing then return end
    destroyed = destroyed + 1
    if destroyed == 4 then
        for i = 1, 4 do
            assert(tostring(windows[i]) == "Window<dead>",
                "reload-stress stale handle remained live")
        end
        mark(string.format("RELOADS=PASS(%d)", reloads))
        mark("WORKSPACE_RELOAD=PASS")
        mark("MAXIMIZE_RELOAD=PASS")
        mark("FULLSCREEN_RELOAD=PASS")
        mark("FPS_RELOAD=PASS")
        mark("DESTROY_AFTER_RELOAD=PASS")
        shady.log(string.format("astral-reload-stress: PASS reloads=%d", reloads))
        shady.quit()
    end
end)

for i = 1, 4 do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=astral-reload-%d SHADY_AUTOMATION_PROBE_COLOR=0xff%02x5060 %q",
        i, 35 + i * 28, probe))
end
