local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_MAGNETIC_STATUS"))
local windows = {}
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function geometry(a, b)
    local ax = assert(a.x) + assert(a.width) * 0.5
    local ay = assert(a.y) + assert(a.height) * 0.5
    local bx = assert(b.x) + assert(b.width) * 0.5
    local by = assert(b.y) + assert(b.height) * 0.5
    local dx, dy = bx - ax, by - ay
    return dx, dy, math.sqrt(dx * dx + dy * dy)
end

local function maybe_start()
    if started or #windows < 2 then return end
    started = true
    shady.automation.after(120, function()
        local _, _, before = geometry(windows[1], windows[2])
        mark(string.format("BEFORE=%.3f", before))

        assert(shady.automation.key("Super+m"))
        shady.automation.after(120, function()
            assert(shady.automation.key("Super+m"))
            shady.automation.after(1800, function()
                local dx, dy, after = geometry(windows[1], windows[2])
                local horizontal_target =
                    (assert(windows[1].width) + assert(windows[2].width)) * 0.5 + 28
                local vertical_target =
                    (assert(windows[1].height) + assert(windows[2].height)) * 0.5 + 28
                local target = math.abs(after - horizontal_target) <
                    math.abs(after - vertical_target) and horizontal_target or vertical_target
                mark(string.format("AFTER=%.3f", after))
                mark(string.format("TARGET=%.3f", target))
                if math.abs(after - target) < 12.0 then mark("DOCKED=PASS") end
                if math.abs(after - before) > 1.0 then mark("MOVED=PASS") end
                shady.quit()
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "magnetic-probe-a" and
            window.app_id ~= "magnetic-probe-b" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=magnetic-probe-a SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Magnetic A", probe))
shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=magnetic-probe-b SHADY_AUTOMATION_PROBE_TITLE=%q %q",
    "Magnetic B", probe))
