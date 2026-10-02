local plugin = os.getenv("SHADY_TEST_PLUGIN_PATH")
if plugin == nil or plugin == "" then
    error("SHADY_TEST_PLUGIN_PATH is required")
end

shady.set("spatial_mode", false)
shady.plugin(plugin)
