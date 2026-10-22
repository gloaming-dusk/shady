-- Capture once from Lua and once over IPC (with a delay), then quit.
local dir = assert(os.getenv("SHOT_DIR"))
local tag = assert(os.getenv("SHOT_TAG"))
local ctl = assert(os.getenv("SHADY_CTL"))
local A = shady.automation

local function exists(path)
    local f = io.open(path, "rb")
    if f then f:close() return true end
    return false
end

shady.on("window.mapped", function()
    A.after(500, function()
        local lua_path = dir .. "/" .. tag .. "-lua.png"
        assert(shady.screenshot(lua_path) == lua_path, "screenshot did not return its path")
        assert(shady.screenshot(lua_path) == nil, "a second capture was accepted while one is pending")
        A.after(800, function()
            assert(exists(lua_path), "Lua capture was not written")
            assert(shady.value("screenshot.saved") == lua_path, "screenshot.saved not published")
            local ipc_path = dir .. "/" .. tag .. "-ipc.png"
            shady.spawn(string.format("%q --raw '{\"cmd\":\"screenshot\",\"path\":\"%s\",\"delay\":200}'", ctl, ipc_path))
            A.after(1500, function()
                assert(exists(ipc_path), "IPC capture was not written")
                shady.log("screenshot-test: PASS")
                shady.quit()
            end)
        end)
    end)
end)

shady.spawn(string.format("SHADY_AUTOMATION_PROBE_APP_ID=shot %q", os.getenv("SHADY_AUTOMATION_PROBE")))
