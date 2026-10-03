local probe = assert(os.getenv("SHADY_AUTOMATION_PROBE"))
local status = assert(os.getenv("SHADY_NEON_OVERVIEW_STATUS"))
local mapped = {}
local focused = ""
local started = false

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end

local function find(id)
    for _, window in ipairs(shady.windows()) do
        if window.app_id == id then return window end
    end
end

shady.on("window.focused", function(window)
    focused = window.app_id or ""
end)

shady.on("window.mapped", function(window)
    local id = window.app_id
    if id ~= "neon-overview-a" and id ~= "neon-overview-b" then return end
    mapped[id] = true

    if id == "neon-overview-a" and not mapped["neon-overview-b"] then
        shady.automation.after(80, function()
            shady.spawn(string.format(
                "SHADY_AUTOMATION_PROBE_APP_ID=neon-overview-b SHADY_AUTOMATION_PROBE_COLOR=0xff6d284f %q",
                probe))
        end)
        return
    end

    if started or not mapped["neon-overview-a"] or not mapped["neon-overview-b"] then
        return
    end
    started = true

    shady.automation.after(350, function()
        local a = assert(find("neon-overview-a"))
        assert(a:focus(), "failed to establish deterministic focus")
        shady.automation.after(120, function()
            assert(focused == "neon-overview-a", "expected A focused before overview")
            assert(shady.automation.key("Super+o"), "Neon overview did not handle Super+O")

            shady.automation.after(700, function()
                a = assert(find("neon-overview-a"))
                local b = assert(find("neon-overview-b"))

                local selected, other
                if a.z > b.z then selected, other = a, b else selected, other = b, a end
                assert(math.abs(a.z - b.z) > 0.08,
                    "Neon overview selection highlight was not visible in Z")

                local key = selected.x < other.x and "Right" or "Left"
                local expected = other.app_id
                assert(shady.automation.key(key),
                    "Neon overview did not handle " .. key)

                shady.automation.after(260, function()
                    assert(shady.automation.key("Return"),
                        "Neon overview did not handle Enter")
                    shady.automation.after(700, function()
                        assert(focused == expected,
                            "Neon overview selected wrong window: expected=" ..
                            expected .. " got=" .. focused)
                        mark("OVERVIEW_SELECT=PASS")
                        shady.log("neon-overview-test: PASS")
                        shady.quit()
                    end)
                end)
            end)
        end)
    end)
end)

shady.spawn(string.format(
    "SHADY_AUTOMATION_PROBE_APP_ID=neon-overview-a SHADY_AUTOMATION_PROBE_COLOR=0xff264d70 %q",
    probe))
