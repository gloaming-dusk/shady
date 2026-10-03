dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/astral-loom/init.lua")

local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local lock_probe = assert(os.getenv("SHADY_SESSION_LOCK_PROBE"))
local lock_status = assert(os.getenv("SHADY_SESSION_LOCK_STATUS"))
local output_helper = assert(os.getenv("SHADY_ASTRAL_OUTPUT_HELPER"))
local output_status = assert(os.getenv("SHADY_ASTRAL_OUTPUT_STATUS"))
local status_path = assert(os.getenv("SHADY_ASTRAL_DAILY_STATUS"))

local mapped = {}
local started = false

local function mark(line)
    local f = assert(io.open(status_path, "a"))
    f:write(line, "\n")
    f:close()
end

local function contains(path, marker)
    local f = io.open(path, "r")
    if not f then return false end
    local body = f:read("*a")
    f:close()
    return body:find(marker, 1, true) ~= nil
end

local function wait_for(path, marker, attempts, callback)
    if contains(path, marker) then
        callback()
        return
    end
    if contains(path, "=FAIL") then
        error("helper failed while waiting for " .. marker)
    end
    assert(attempts > 0, "timed out waiting for " .. marker)
    shady.automation.after(50, function()
        wait_for(path, marker, attempts - 1, callback)
    end)
end

local function find(id)
    for _, w in ipairs(shady.windows()) do
        if w.app_id == id then return w end
    end
end

local function assert_windows_alive()
    for i = 1, 3 do
        local w = assert(find("astral-daily-" .. i), "probe window disappeared")
        assert(w.mapped and w.visible, "probe window is not mapped/visible")
        assert(w.z ~= nil, "spatial state disappeared")
    end
end

local function module_active(name)
    for _, module in ipairs(shady.modules()) do
        if module.name == name then return module.active end
    end
    return false
end

local function run_output_test()
    assert(shady.spawn(string.format("%q", output_helper)), "failed to spawn output helper")
    wait_for(output_status, "OUTPUT_DISABLE=PASS", 30, function()
        shady.automation.after(350, function()
            assert(#shady.outputs() == 2, "output objects unexpectedly disappeared")
            assert_windows_alive()
            mark("OUTPUT_RECOVERY=PASS")
            shady.log("astral-loom-daily: PASS states, reload, lock, output recovery")
            shady.quit()
        end)
    end)
end

local function run_lock_test()
    assert(shady.spawn(string.format("%q", lock_probe)), "failed to spawn lock probe")
    wait_for(lock_status, "LOCKED=PASS", 30, function()
        wait_for(lock_status, "SURFACE_CONFIGURED=PASS", 30, function()
            assert(not shady.automation.key("Super+b"),
                "Astral Loom key handler ran while session was locked")
            mark("LOCK_BLOCK=PASS")
            wait_for(lock_status, "UNLOCKED=PASS", 40, function()
                assert(shady.automation.key("Super+b"),
                    "Astral Loom key handler did not recover after unlock")
                assert(shady.automation.key("Super+b"),
                    "Astral Loom pause state could not be restored after unlock")
                assert_windows_alive()
                mark("LOCK_RECOVERY=PASS")
                run_output_test()
            end)
        end)
    end)
end

local function run_reload_test()
    assert(shady.reload_plugin("astral-loom"), "Astral Loom reload was not queued")
    shady.automation.after(300, function()
        assert(module_active("astral-loom"), "Astral Loom module inactive after reload")
        assert_windows_alive()
        mark("RELOAD=PASS")
        run_lock_test()
    end)
end

local function run_fullscreen_test(w)
    assert(w:set_fullscreen(true), "fullscreen request failed")
    shady.automation.after(120, function()
        assert(w.fullscreen, "fullscreen state did not stick")
        assert(w.mapped and w.visible, "fullscreen window disappeared")
        assert(w:set_fullscreen(false), "fullscreen exit failed")
        shady.automation.after(180, function()
            assert(not w.fullscreen, "fullscreen state did not clear")
            assert_windows_alive()
            mark("FULLSCREEN=PASS")
            run_reload_test()
        end)
    end)
end

local function run_maximize_test(w)
    assert(w:maximize(true), "maximize request failed")
    shady.automation.after(120, function()
        assert(w.maximized, "maximize state did not stick")
        assert(w.mapped and w.visible, "maximized window disappeared")
        assert(w:maximize(false), "unmaximize request failed")
        shady.automation.after(180, function()
            assert(not w.maximized, "maximize state did not clear")
            assert_windows_alive()
            mark("MAXIMIZE=PASS")
            run_fullscreen_test(w)
        end)
    end)
end

shady.on("window.mapped", function(w)
    if not w.app_id:match("^astral%-daily%-") then return end
    mapped[w.app_id] = true
    if started or not mapped["astral-daily-1"] or
            not mapped["astral-daily-2"] or not mapped["astral-daily-3"] then
        return
    end
    started = true

    shady.automation.after(650, function()
        assert(module_active("astral-loom"), "Astral Loom module is not active")
        assert_windows_alive()

        local a = assert(find("astral-daily-1"))
        local b = assert(find("astral-daily-2"))
        assert(math.abs(a.z - b.z) > 0.05 or math.abs(a.x - b.x) > 10,
            "Astral Loom layout did not separate windows")
        assert(a:focus(), "focus request failed")
        shady.automation.after(100, function()
            local focused = assert(shady.focused_window(), "no focused window")
            assert(focused.app_id == "astral-daily-1", "focus did not move")
            mark("BASELINE=PASS")
            run_maximize_test(a)
        end)
    end)
end)

for i = 1, 3 do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=astral-daily-%d SHADY_AUTOMATION_PROBE_COLOR=0xff%02x5070 %q",
        i, 32 + i * 24, probe))
end
