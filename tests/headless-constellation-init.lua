local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_CONSTELLATION_STATUS"))
local windows = {}
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function pos(window)
    return assert(window.x), assert(window.y), assert(window.z)
end

local function distance3(a, b)
    local dx = a[1] - b[1]
    local dy = a[2] - b[2]
    local dz = (a[3] - b[3]) * 700
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

local function maybe_start()
    if started or #windows < 3 then return end
    started = true

    shady.automation.after(180, function()
        assert(windows[1]:focus())
        local originals = {}
        for idx, window in ipairs(windows) do
            originals[idx] = {pos(window)}
        end

        assert(shady.automation.key("Super+c"))
        shady.automation.after(1600, function()
            local orbit = {}
            for idx, window in ipairs(windows) do
                orbit[idx] = {pos(window)}
            end

            local moved2 = distance3(orbit[2], originals[2])
            local moved3 = distance3(orbit[3], originals[3])
            local zspread = math.abs(orbit[2][3] - orbit[3][3])
            mark(string.format("ORBIT_MOVED_2=%.3f", moved2))
            mark(string.format("ORBIT_MOVED_3=%.3f", moved3))
            mark(string.format("ORBIT_Z_SPREAD=%.5f", zspread))
            if moved2 > 40 and moved3 > 40 then mark("ORBIT=PASS") end
            if zspread > 0.02 then mark("DEPTH=PASS") end

            assert(shady.automation.key("Super+c"))
            shady.automation.after(7000, function()
                local restored = true
                local max_error = 0
                for idx, window in ipairs(windows) do
                    local now = {pos(window)}
                    local error = distance3(now, originals[idx])
                    if error > max_error then max_error = error end
                    if error > 8 then restored = false end
                end
                mark(string.format("RESTORE_MAX_ERROR=%.3f", max_error))
                if restored then mark("RESTORE=PASS") end
                shady.quit()
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "constellation-a" and
            window.app_id ~= "constellation-b" and
            window.app_id ~= "constellation-c" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

for _, id in ipairs({"constellation-a", "constellation-b", "constellation-c"}) do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_TITLE=%q %q",
        id, id, probe))
end
