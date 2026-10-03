local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_OVERVIEW_STATUS"))
local mapped = {}
local started = false
local initial = {}

local ids = {
    "overview-a",
    "overview-b",
    "overview-c",
}

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function find(id)
    for _, w in ipairs(shady.windows()) do
        if w.app_id == id then return w end
    end
end

local function save_initial()
    for _, id in ipairs(ids) do
        local w = assert(find(id))
        initial[id] = { x = w.x, y = w.y, z = w.z }
    end
end

local function dist2(a, b)
    local dx = a.x - b.x
    local dy = a.y - b.y
    return dx * dx + dy * dy
end

local function verify_overview()
    local changed = 0
    local positions = {}
    for _, id in ipairs(ids) do
        local w = assert(find(id))
        assert(w.z ~= nil, "overview requires spatial z")
        positions[id] = { x = w.x, y = w.y, z = w.z }
        if dist2(positions[id], initial[id]) > 900 or math.abs(w.z - initial[id].z) > 0.08 then
            changed = changed + 1
        end
        assert(w.z < -0.25, "overview did not push window into depth: " .. id)
    end
    assert(changed == #ids, "not all windows moved into overview")

    assert(dist2(positions["overview-a"], positions["overview-b"]) > 4000,
        "overview a/b are not sufficiently separated")
    assert(dist2(positions["overview-b"], positions["overview-c"]) > 4000,
        "overview b/c are not sufficiently separated")
    mark("OVERVIEW=PASS")
end

local function verify_restored()
    for _, id in ipairs(ids) do
        local w = assert(find(id))
        local home = initial[id]
        assert(math.abs(w.x - home.x) <= 2,
            string.format("%s x did not restore: got=%d home=%d", id, w.x, home.x))
        assert(math.abs(w.y - home.y) <= 2,
            string.format("%s y did not restore: got=%d home=%d", id, w.y, home.y))
        assert(math.abs(w.z - home.z) <= 0.005, id .. " z did not restore")
    end
    mark("RESTORE=PASS")
end

shady.on("window.mapped", function(window)
    local recognized = false
    for _, id in ipairs(ids) do
        if window.app_id == id then recognized = true break end
    end
    if not recognized then return end

    mapped[window.app_id] = true

    if window.app_id == "overview-a" and not mapped["overview-b"] then
        shady.automation.after(70, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=overview-b SHADY_AUTOMATION_PROBE_COLOR=0xff305070 %q",
                probe))
        end)
        return
    end
    if window.app_id == "overview-b" and not mapped["overview-c"] then
        shady.automation.after(70, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=overview-c SHADY_AUTOMATION_PROBE_COLOR=0xff703050 %q",
                probe))
        end)
        return
    end

    if started or not mapped["overview-a"] or not mapped["overview-b"] or not mapped["overview-c"] then
        return
    end
    started = true

    shady.automation.after(350, function()
        save_initial()
        assert(shady.automation.key("Super+o"), "Super+O was not handled by overview plugin")
        shady.automation.after(900, function()
            verify_overview()
            assert(shady.automation.key("Super+o"), "second Super+O was not handled")
            shady.automation.after(900, function()
                verify_restored()
                shady.log("spatial-overview-test: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=overview-a SHADY_AUTOMATION_PROBE_COLOR=0xff507030 %q",
    probe))
