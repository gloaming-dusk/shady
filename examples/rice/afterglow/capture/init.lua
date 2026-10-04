-- Driver for capture.sh: the real Afterglow runtime layer, the rice's
-- shell, three terminals, then either stills (one per hour) or the hero
-- loop (hour changes timed to the website's chips).
local root = assert(os.getenv("SHADY_ROOT"))
dofile(root .. "/examples/rice/afterglow/init.lua")

local mode = assert(os.getenv("CAPTURE_MODE"))
local out = assert(os.getenv("CAPTURE_OUT"))
local status = assert(os.getenv("CAPTURE_STATUS"))
local after = shady.automation.after

local function mark(line)
    local f = assert(io.open(status, "a"))
    f:write(line, "\n")
    f:close()
end
local function wait_for(token, fn)
    local function poll()
        local f = io.open(status, "r")
        local text = f and f:read("a") or ""
        if f then f:close() end
        if text:find(token, 1, true) then fn() else after(20, poll) end
    end
    poll()
end

local terminals = {
    { app = "capture-htop", title = "Terminal — htop",
      cmd = "ls -l --color=always " .. root .. "/src " .. root .. "/examples/plugins" },
    { app = "capture-notes", title = "Notes — README.md",
      cmd = "sed -n '/^## Highlights/,/^## Controls/p' " .. root .. "/README.md | cut -c1-110" },
    { app = "capture-shell", title = "Shell", cmd = "printf 'hello shady\\n'" },
}
local mapped = 0
shady.on("window.mapped", function(window)
    if not window.app_id:match("^capture%-") then return end
    mapped = mapped + 1
    if mapped == #terminals then
        -- Let the shell, the glass and the windows settle.
        after(2500, function() mark("READY") end)
    end
end)

shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
for i, t in ipairs(terminals) do
    after(400 + 350 * i, function()
        shady.spawn(string.format("foot --config=%q --app-id=%s --title=%q --window-size-pixels=400x240 sh -c %q",
            os.getenv("CAPTURE_FOOT_INI"), t.app, t.title, t.cmd .. "; sleep 3600"))
    end)
end

local function screenshot(name, k)
    shady.automation.screenshot(out .. "/" .. name .. ".png", function(ok)
        assert(ok, "screenshot failed: " .. name)
        k()
    end)
end

if mode == "stills" then
    -- Starts at golden hour; each Super+T cross-fades for 2.4 s.
    local hours = { "golden-hour", "afterglow", "blue-hour", "night" }
    local function shoot(i)
        screenshot("hour-" .. hours[i], function()
            if i == #hours then
                mark("DONE")
                shady.quit()
                return
            end
            assert(shady.automation.key("Super+t"))
            after(3200, function() shoot(i + 1) end)
        end)
    end
    wait_for("READY", function() shoot(1) end)
else
    -- The website's hour chips switch at 2.3, 6.0, 10.2 and 14.3 s, the
    -- midpoints of the cross-fades, so press 1.2 s earlier.
    wait_for("RECORDING", function()
        for _, t in ipairs({ 1100, 4800, 9000, 13100 }) do
            after(t, function() assert(shady.automation.key("Super+t")) end)
        end
        after(17600, function()
            mark("DONE")
            after(500, function() shady.quit() end)
        end)
    end)
end
after(60000, function() shady.quit() end)
