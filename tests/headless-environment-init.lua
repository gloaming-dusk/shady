-- Drive the environment through loader reload and runtime path changes.
-- Each step waits long enough for a frame, where the host syncs the config.
local obj = assert(os.getenv("SHADY_ENVIRONMENT_OBJ"))
local after = shady.automation.after

after(300, function()
    shady.log("environment-test: reload")
    assert(shady.reload_plugin("obj-loader"))
    after(200, function()
        shady.log("environment-test: unhandled")
        shady.config("environment_path", "/nonexistent/scene.gltf")
        after(200, function()
            shady.log("environment-test: restore")
            shady.config("environment_path", obj)
            after(200, function()
                shady.log("environment-test: unload")
                assert(shady.unload_plugin("obj-loader"))
                after(200, function() shady.quit() end)
            end)
        end)
    end)
end)
