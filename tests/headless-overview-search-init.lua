-- Typing in the overview searches, with the real Tide runtime and shell. Every
-- key arrives while the launcher is still opening: a lost first letter would
-- launch "eta" instead of "zeta".
dofile(assert(os.getenv("SHADY_ROOT")) .. "/rices/tide/init.lua")
local after = shady.automation.after
local L = shady.layout
local probe_log = assert(os.getenv("OVERVIEW_IME_LOG"))
local ime_probe = assert(os.getenv("OVERVIEW_IME_PROBE"))

local function fail(message)
    shady.log("overview-search: " .. message)
    shady.quit()
end
local function tap()
    shady.automation.key_down("Super_L")
    shady.automation.key_up("Super_L")
end

local launches = 0

local function with_input_method()
    shady.spawn(string.format("IME_PROBE_GRAB=1 %q > %q 2>&1", ime_probe, probe_log))
    after(800, function()
        tap()
        if not L.state().overview then return fail("the tap did not open the overview") end
        shady.spawn("wtype zeta -k Return -s 1500")
    end)
end

local function finish()
    local f = assert(io.open(probe_log, "r"))
    local text = f:read("a")
    f:close()
    -- Each key once: the input method, then the launcher.
    local _, downs = text:gsub("ime: key %d+ down", "")
    if downs ~= 5 then
        return fail(string.format("the input method got %d of 5 keys:\n%s", downs, text))
    end
    shady.log("overview-search: PASS")
    shady.quit()
end

shady.on("window.mapped", function(window)
    if window.app_id == "launched-eta" then return fail("the first letter was lost") end
    if window.app_id ~= "launched-zeta" then return end
    launches = launches + 1
    -- The window closes the overview.
    after(300, function()
        if L.state().overview then return fail("the new window did not close the overview") end
        if launches == 2 then return finish() end
        window:close()
        after(500, with_input_method)
    end)
end)

-- Long enough for the shell to be up under a sanitizer.
after(3000, function()
    tap()
    if not L.state().overview then return fail("the tap did not open the overview") end
    shady.spawn("wtype zeta -k Return -s 1500")
end)
after(25000, function() fail("timed out") end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
