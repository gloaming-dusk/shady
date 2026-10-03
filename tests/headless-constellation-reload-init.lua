local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local windows = {}
local started = false

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
        assert(shady.automation.key("Super+c"))
        shady.automation.after(700, function()
            local before = {pos(windows[2])}
            assert(shady.reload_plugin("window-constellation"),
                "failed to reload window-constellation")
            shady.automation.after(850, function()
                local after = {pos(windows[2])}
                assert(distance3(after, before) > 25,
                    "constellation orbit stopped after reload")
                shady.log("constellation-reload: PASS")
                assert(shady.automation.key("Super+c"))
                shady.automation.after(3300, function()
                    shady.quit()
                end)
            end)
        end)
    end)
end

shady.on("window.mapped", function(window)
    if window.app_id ~= "constellation-reload-a" and
            window.app_id ~= "constellation-reload-b" and
            window.app_id ~= "constellation-reload-c" then return end
    windows[#windows + 1] = window
    maybe_start()
end)

for _, id in ipairs({
    "constellation-reload-a",
    "constellation-reload-b",
    "constellation-reload-c",
}) do
    shady.spawn(string.format(
        "SHADY_AUTOMATION_PROBE_APP_ID=%s SHADY_AUTOMATION_PROBE_TITLE=%q %q",
        id, id, probe))
end
