local attempted = false
local verified = false

local function counter_active()
    for _, module in ipairs(shady.modules()) do
        if module.name == "counter-plugin" then
            return module.active
        end
    end
    return false
end

shady.on("module.started", function(module)
    if module.name ~= "counter-plugin" then
        return
    end

    if not attempted then
        attempted = true
        local plugin_path = assert(os.getenv("SHADY_TEST_PLUGIN_PATH"))
        local bad_path = assert(os.getenv("SHADY_TEST_BAD_PLUGIN_PATH"))
        local rc = os.execute(string.format("cp %q %q", bad_path, plugin_path))
        if rc ~= true and rc ~= 0 then
            error("failed to stage bad plugin")
        end

        if not shady.reload_plugin("counter-plugin") then
            error("failed to queue counter-plugin reload")
        end
        return
    end

    if verified then
        return
    end
    verified = true

    if not counter_active() then
        error("counter-plugin was not restored active after rollback")
    end

    shady.log("plugin-rollback-test: PASS")
    shady.quit()
end)
