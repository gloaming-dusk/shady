local old_counter = nil
for _, module in ipairs(shady.modules()) do
    if module.name == "counter-plugin" then
        old_counter = module
        break
    end
end

if old_counter == nil then
    error("counter-plugin was not registered")
end

if not shady.reload_plugin("counter-plugin") then
    error("counter-plugin reload failed")
end

local old_label = tostring(old_counter)
if old_label ~= "Module<dead>" then
    error("stale module handle remained live after reload: " .. old_label)
end

local found_new = false
for _, module in ipairs(shady.modules()) do
    if module.name == "counter-plugin" and module.active then
        found_new = true
        break
    end
end

if not found_new then
    error("reloaded counter-plugin is not active")
end

shady.log("plugin-reload-test: PASS stale=" .. old_label)
shady.quit()
