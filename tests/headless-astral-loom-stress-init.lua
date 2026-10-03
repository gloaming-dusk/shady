dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/astral-loom/init.lua")

local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status_path = assert(os.getenv("SHADY_ASTRAL_STRESS_STATUS"))
local rounds = tonumber(os.getenv("SHADY_ASTRAL_STRESS_ROUNDS") or "10")
local per_round = 4

local current_round = 0
local round_windows = {}
local round_mapped = 0
local round_destroyed = 0
local stale_handles = {}
local close_requested = false
local totals = {
    mapped = 0,
    destroyed = 0,
    stale = 0,
    focus = 0,
    workspace = 0,
    fps = 0,
    astral = 0,
}

local function mark(line)
    local f = assert(io.open(status_path, "a"))
    f:write(line, "\n")
    f:close()
end

local function is_stress_id(id)
    return id and id:match("^astral%-stress%-%d+%-%d+$") ~= nil
end

local function id_for(round, index)
    return string.format("astral-stress-%d-%d", round, index)
end

local function find(id)
    for _, w in ipairs(shady.windows()) do
        if w.app_id == id then return w end
    end
end

local function assert_current_round_alive()
    for i = 1, per_round do
        local w = assert(find(id_for(current_round, i)),
            "stress window disappeared before close")
        assert(w.mapped, "stress window became unmapped unexpectedly")
        assert(w.z ~= nil, "stress window lost spatial state")
    end
end

local function verify_stale(callback)
    shady.automation.after(80, function()
        for _, w in ipairs(stale_handles) do
            assert(tostring(w) == "Window<dead>",
                "destroyed stress window handle remained live")
            assert(w.app_id == nil and w.z == nil,
                "destroyed stress window still exposes properties")
            totals.stale = totals.stale + 1
        end
        stale_handles = {}
        callback()
    end)
end

local start_round

local function close_round()
    assert(shady.workspace("main"), "failed to return to main workspace")
    stale_handles = {}
    for i = 1, per_round do
        local w = assert(round_windows[i], "missing round window handle")
        stale_handles[#stale_handles + 1] = w
    end
    close_requested = true
    for i = 1, per_round do
        assert(round_windows[i]:close(), "failed to close stress window")
    end
end

local function exercise_round()
    assert_current_round_alive()

    -- Rapid focus churn. The last request must win.
    for i = 1, per_round do
        assert(round_windows[i]:focus(), "focus request failed")
        totals.focus = totals.focus + 1
    end

    shady.automation.after(40, function()
        local focused = assert(shady.focused_window(), "no focused stress window")
        assert(focused.app_id == id_for(current_round, per_round),
            "last focus request did not win")

        -- Create and reuse workspaces dynamically. Move windows away and switch
        -- rapidly enough to exercise scene visibility and Astral provider attach/detach.
        assert(round_windows[1]:move_to_workspace("stress-a"))
        assert(round_windows[2]:move_to_workspace("stress-b"))
        assert(round_windows[3]:move_to_workspace("stress-a"))
        totals.workspace = totals.workspace + 3

        assert(shady.workspace("stress-a"))
        assert(shady.current_workspace() == "stress-a")
        assert(round_windows[1].visible and round_windows[3].visible,
            "stress-a windows were not visible")
        assert(not round_windows[2].visible and not round_windows[4].visible,
            "non-current workspace windows remained visible")

        assert(shady.workspace("stress-b"))
        assert(shady.current_workspace() == "stress-b")
        assert(round_windows[2].visible, "stress-b window was not visible")

        assert(shady.workspace("main"))
        assert(shady.current_workspace() == "main")
        assert(round_windows[4].visible, "main workspace window was not visible")
        totals.workspace = totals.workspace + 3

        -- The rice starts in FPS mode. Exercise capture/release state as well
        -- as full FPS -> orbit -> FPS transitions while windows are live.
        shady.expand_all()
        shady.fold_all()
        shady.toggle_fps()
        shady.toggle_fps()
        totals.fps = totals.fps + 4

        -- Exercise Astral's own live layout state while provider-backed windows
        -- are being created and destroyed.
        assert(shady.automation.key("Super+Shift+j"), "Astral layout toggle failed")
        assert(shady.automation.key("Super+b"), "Astral pause toggle failed")
        assert(shady.automation.key("Super+b"), "Astral resume toggle failed")
        totals.astral = totals.astral + 3

        shady.automation.after(60, function()
            assert_current_round_alive()
            close_round()
        end)
    end)
end

start_round = function()
    current_round = current_round + 1
    if current_round > rounds then
        assert(shady.workspace("main"), "failed to finish on main workspace")
        assert(#shady.windows() == 0, "stress windows leaked after final round")
        assert(totals.mapped == rounds * per_round, "mapped count mismatch")
        assert(totals.destroyed == rounds * per_round, "destroyed count mismatch")
        assert(totals.stale == rounds * per_round, "stale-handle count mismatch")
        assert(totals.focus == rounds * per_round, "focus operation count mismatch")
        assert(totals.workspace == rounds * 6, "workspace operation count mismatch")
        assert(totals.fps == rounds * 4, "FPS operation count mismatch")
        assert(totals.astral == rounds * 3, "Astral operation count mismatch")

        mark(string.format("ROUNDS=PASS(%d)", rounds))
        mark(string.format("WINDOW_CHURN=PASS(%d)", totals.destroyed))
        mark(string.format("STALE_HANDLES=PASS(%d)", totals.stale))
        mark("FOCUS_CHURN=PASS")
        mark("WORKSPACE_CHURN=PASS")
        mark("FPS_CAPTURE_RELEASE=PASS")
        mark("ASTRAL_STATE_CHURN=PASS")
        shady.log(string.format(
            "astral-loom-stress: PASS rounds=%d windows=%d focus=%d workspace=%d fps=%d astral=%d",
            rounds, totals.destroyed, totals.focus, totals.workspace,
            totals.fps, totals.astral))
        shady.quit()
        return
    end

    round_windows = {}
    round_mapped = 0
    round_destroyed = 0
    stale_handles = {}
    close_requested = false

    for i = 1, per_round do
        local color = 0xff202020 + current_round * 0x00090000 + i * 0x00001107
        shady.spawn(string.format(
            "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_COLOR=0x%08x %q",
            id_for(current_round, i), color, probe))
    end
end

shady.on("window.mapped", function(w)
    if not is_stress_id(w.app_id) then return end
    local round, index = w.app_id:match("^astral%-stress%-(%d+)%-(%d+)$")
    round, index = tonumber(round), tonumber(index)
    if round ~= current_round then return end
    assert(not round_windows[index], "duplicate stress map")
    round_windows[index] = w
    round_mapped = round_mapped + 1
    totals.mapped = totals.mapped + 1

    if round_mapped == per_round then
        shady.automation.after(100, exercise_round)
    end
end)

shady.on("window.destroyed", function(_w)
    -- Destroy events intentionally expose stale-safe handles and may no longer
    -- carry app_id. During this phase the only close requests are the four
    -- stress windows, so count completions and validate the original handles.
    if not close_requested or round_destroyed >= per_round then return end
    round_destroyed = round_destroyed + 1
    totals.destroyed = totals.destroyed + 1
    if round_destroyed == per_round then
        close_requested = false
        verify_stale(function()
            shady.automation.after(40, start_round)
        end)
    end
end)

shady.automation.after(80, start_round)
