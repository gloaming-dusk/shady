-- Make lifecycle progress visible to the smoke-test harness.
local function window_label(window)
    if window == nil then return "<nil>" end
    return (window.app_id or "") .. " / " .. (window.title or "")
end

local stale_window = nil
local stale_output = nil
local stale_window_checked = false
local stale_output_checked = false

local function append_status(line)
    local path = os.getenv("SHADY_TEST_STATUS")
    if path == nil or path == "" then return end
    local file = assert(io.open(path, "a"))
    file:write(line, "\n")
    file:close()
end

local function check_stale_handles()
    if stale_window ~= nil and not stale_window_checked then
        local label = tostring(stale_window)
        if label ~= "Window<dead>" then
            error("destroyed window handle remained live: " .. label)
        end
        if stale_window.title ~= nil then
            error("destroyed window handle still exposes properties")
        end
        stale_window_checked = true
        append_status("LUA_STALE_WINDOW=PASS")
        shady.log("daily-driver-test: stale-window PASS")
    end

    if stale_output ~= nil and not stale_output_checked then
        local label = tostring(stale_output)
        if label ~= "Output<dead>" then
            error("removed output handle remained live: " .. label)
        end
        if stale_output.name ~= nil then
            error("removed output handle still exposes properties")
        end
        stale_output_checked = true
        append_status("LUA_STALE_OUTPUT=PASS")
        shady.log("daily-driver-test: stale-output PASS")
    end
end

shady.on("window.mapped", function(window)
    check_stale_handles()
    shady.log("daily-driver-test: mapped " .. window_label(window))
end)

shady.on("window.unmapped", function(window)
    shady.log("daily-driver-test: unmapped " .. window_label(window))
end)

shady.on("window.destroyed", function(window)
    if stale_window == nil then
        stale_window = window
    end
    shady.log("daily-driver-test: destroyed " .. window_label(window))
end)

shady.on("output.added", function(output)
    shady.log("daily-driver-test: output-added " .. (output.name or ""))
end)

shady.on("output.removed", function(output)
    if stale_output == nil then
        stale_output = output
    end
    shady.log("daily-driver-test: output-removed " .. (output.name or ""))
end)
