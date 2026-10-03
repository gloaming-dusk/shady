dofile(assert(os.getenv("SHADY_ROOT")) .. "/examples/rice/astral-loom/init.lua")

local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status_path = assert(os.getenv("SHADY_ASTRAL_INPUT_STATUS"))
local rounds = tonumber(os.getenv("SHADY_ASTRAL_INPUT_ROUNDS") or "8")
local per_round = 4

local round = 0
local mapped = 0
local destroyed = 0
local closing = false
local awaiting_focus_destroy = false
local closing_focus = nil
local windows = {}
local totals = {
    tab = 0,
    workspace = 0,
    fps = 0,
    capture = 0,
    refocus = 0,
    destroyed = 0,
}

local function mark(line)
    local f = assert(io.open(status_path, "a"))
    f:write(line, "\n")
    f:close()
end

local function id_for(r, i)
    return string.format("astral-input-%d-%d", r, i)
end

local function stress_id(id)
    return id and id:match("^astral%-input%-%d+%-%d+$") ~= nil
end

local function assert_live_focus(workspace)
    local focused = assert(shady.focused_window(), "focus disappeared")
    assert(focused.mapped and focused.visible, "focused window is not live/visible")
    assert(focused.workspace == workspace,
        "focus escaped current workspace: " .. tostring(focused.workspace))
    return focused
end

local function cycle_focus(times, workspace)
    local seen = {}
    for _ = 1, times do
        assert(shady.automation.key("Super+Tab"), "Super+Tab was not handled")
        local focused = assert_live_focus(workspace)
        seen[focused.app_id] = true
        totals.tab = totals.tab + 1
    end
    local unique = 0
    for _ in pairs(seen) do unique = unique + 1 end
    assert(unique >= 2, "focus cycling did not rotate between windows")
end

local start_round

local function finish_round()
    shady.automation.after(100, function()
        for i = 1, per_round do
            local w = windows[i]
            assert(tostring(w) == "Window<dead>",
                "destroyed focus-stress handle remained live")
            assert(w.app_id == nil and w.workspace == nil,
                "stale focus-stress handle exposed state")
        end
        assert(#shady.windows() == 0, "windows leaked between input-stress rounds")
        closing = false
        shady.automation.after(40, start_round)
    end)
end

local function close_all()
    closing = true
    destroyed = 0
    assert(shady.workspace("main"))
    for i = 1, per_round do
        assert(windows[i]:close(), "failed to close focus-stress window")
    end
end

local function exercise_round()
    assert(shady.workspace("main"))
    assert(shady.current_workspace() == "main")

    -- Astral rice starts in FPS mode with capture active.
    assert(shady.automation.key("w"), "FPS movement input was not captured")
    totals.capture = totals.capture + 1

    cycle_focus(per_round + 2, "main")

    -- Release capture without depending on a camera-center hit, then prove
    -- ordinary compositor shortcuts still work while client interaction is open.
    shady.expand_all()
    assert(not shady.automation.key("w"),
        "FPS movement remained captured after expand_all")
    cycle_focus(3, "main")
    shady.fold_all()
    assert(shady.automation.key("w"),
        "FPS movement did not resume after fold_all")
    totals.capture = totals.capture + 2

    -- Exit FPS entirely and re-enter. Super+Tab must keep working in both modes.
    assert(shady.automation.key("Super+f"), "failed to leave FPS mode")
    assert(not shady.automation.key("w"),
        "FPS movement was handled while FPS mode was off")
    cycle_focus(3, "main")
    assert(shady.automation.key("Super+f"), "failed to re-enter FPS mode")
    assert(shady.automation.key("w"), "FPS movement did not recover")
    totals.fps = totals.fps + 2

    -- Split the live set across workspaces and exercise keyboard focus only among
    -- visible windows. Workspace switches also force refocus while Astral keeps
    -- moving representation-backed windows.
    assert(windows[1]:move_to_workspace("input-alt"))
    assert(windows[2]:move_to_workspace("input-alt"))
    assert(not windows[1].visible and not windows[2].visible,
        "moved windows remained visible on main")

    assert(shady.workspace("input-alt"))
    assert(shady.current_workspace() == "input-alt")
    assert_live_focus("input-alt")
    cycle_focus(4, "input-alt")
    totals.workspace = totals.workspace + 1

    assert(shady.workspace("main"))
    assert(shady.current_workspace() == "main")
    local focused = assert_live_focus("main")
    cycle_focus(4, "main")
    totals.workspace = totals.workspace + 1

    -- Close the focused main-workspace window. Continue from the actual destroy
    -- event rather than a fixed delay because the rice uses close animation.
    closing_focus = assert(shady.focused_window())
    awaiting_focus_destroy = true
    assert(closing_focus:close(), "failed to close focused window")
end

start_round = function()
    round = round + 1
    if round > rounds then
        assert(shady.workspace("main"))
        assert(#shady.windows() == 0, "input-stress windows leaked")
        assert(totals.destroyed == rounds * per_round, "destroy count mismatch")
        mark(string.format("ROUNDS=PASS(%d)", rounds))
        mark(string.format("WINDOWS=PASS(%d)", totals.destroyed))
        mark(string.format("TAB_CYCLES=PASS(%d)", totals.tab))
        mark("WORKSPACE_FOCUS=PASS")
        mark("FPS_MODE_INPUT=PASS")
        mark("CAPTURE_RELEASE=PASS")
        mark("DESTROY_REFOCUS=PASS")
        shady.log(string.format(
            "astral-input-focus-stress: PASS rounds=%d windows=%d tabs=%d",
            rounds, totals.destroyed, totals.tab))
        shady.quit()
        return
    end

    mapped = 0
    destroyed = 0
    closing = false
    awaiting_focus_destroy = false
    closing_focus = nil
    windows = {}

    for i = 1, per_round do
        shady.spawn(string.format(
            "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_COLOR=0xff%02x%02x60 %q",
            id_for(round, i), 25 + round * 9, 35 + i * 25, probe))
    end
end

shady.on("window.mapped", function(w)
    if not stress_id(w.app_id) then return end
    local r, i = w.app_id:match("^astral%-input%-(%d+)%-(%d+)$")
    r, i = tonumber(r), tonumber(i)
    if r ~= round then return end
    windows[i] = w
    mapped = mapped + 1
    if mapped == per_round then
        shady.automation.after(180, exercise_round)
    end
end)

shady.on("window.destroyed", function(_w)
    if awaiting_focus_destroy then
        awaiting_focus_destroy = false
        destroyed = 1
        totals.destroyed = totals.destroyed + 1
        shady.automation.after(40, function()
            assert(tostring(closing_focus) == "Window<dead>",
                "focused window handle did not become stale")
            local survivor = assert_live_focus("main")
            assert(shady.automation.key("Super+Tab"),
                "Super+Tab failed after focused-window destruction")
            assert_live_focus("main")
            totals.refocus = totals.refocus + 1

            closing = true
            for i = 1, per_round do
                if tostring(windows[i]) ~= "Window<dead>" then
                    assert(windows[i]:close(), "failed to close remaining window")
                end
            end
        end)
        return
    end

    if not closing then return end
    destroyed = destroyed + 1
    totals.destroyed = totals.destroyed + 1
    if destroyed == per_round then
        closing = false
        finish_round()
    end
end)

shady.automation.after(80, start_round)
