-- Bootstrap half of the plugin manager test (see headless-plugin-manager.sh).
shady.set("spatial_mode", true)
shady.set("physics_enabled", false)
shady.set("fps_mode", false)
shady.set("sky", false)
shady.set("environment", false)
shady.modules({ ["physics"] = false, ["fps"] = false, ["lua"] = true })

shady.plugins.path(assert(os.getenv("SHADY_TEST_PLUGIN_DIR")))

-- A default plugin can be turned off before it is ever loaded, through the
-- new API or the legacy module toggle.
assert(shady.has_module("window-motion"))
shady.plugins.disable("window-motion")
shady.modules({ ["window-motion"] = false })

-- Environment loaders are not defaults: a config loads one by name, or Shady
-- loads it for a configured environment (headless-environment.sh).
assert(not pcall(shady.plugins.disable, "obj-loader"), "obj-loader is not a default plugin")
assert(shady.plugins.load("obj-loader"))

-- Load by name through the search path; repeats are no-ops.
assert(shady.plugins.load("counter"))
assert(shady.plugins.load("counter"))

assert(not pcall(shady.plugins.load, "does-not-exist"))
assert(not pcall(shady.plugins.load, "bad name!"))
assert(not pcall(shady.plugins.load, "./no/such/plugin.so"))
assert(not pcall(shady.plugins.disable, "never-heard-of-it"))

for _, p in ipairs(shady.plugins.list()) do
    shady.log("plugin-manager-test: bootstrap " .. p.name .. "=" .. p.state)
end
