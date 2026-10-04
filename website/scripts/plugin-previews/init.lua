local name = assert(os.getenv("SHADY_CAPTURE_PLUGIN"))
local target = assert(os.getenv("SHADY_CAPTURE_TARGET"))
local root = assert(os.getenv("SHADY_ROOT"))
local after = shady.automation.after
local windows = {}
local started = false
local count = name == "obj-loader" and 1 or 3
local function capture()
    shady.automation.screenshot(target, function(ok)
        assert(ok, "plugin screenshot failed")
        shady.log("plugin-preview: captured " .. name)
        shady.quit()
    end)
end
local function demonstrate()
    assert(windows[1]:focus())
    local shortcut = ({["spatial-overview"]="Super+o", ["window-constellation"]="Super+c",
        ["window-portal"]="Super+p", ["border-accent"]="Super+b",
        ["frozen-window"]="Super+Shift+z"})[name]
    if shortcut then assert(shady.automation.key(shortcut)) end
    if name:match("^fps%-") then
        shady.camera("distance", 0.75)
        shady.camera("target_x", 0.10)
        shady.toggle_fps()
        shady.fold_all()
        shady.camera("yaw", 0.18)
        shady.camera("pitch", 0.25)
        after(900, capture)
    elseif name == "black-hole" then
        assert(shady.automation.key("Super+h"))
        after(650, capture)
    elseif name == "close-burn" or name == "close-slide-fade" then
        assert(windows[1]:close())
        after(name == "close-burn" and 330 or 100, capture)
    elseif name == "window-motion" then
        local w = windows[1]
        shady.automation.drag(w.x + 60, w.y - 14, w.x + 190, w.y + 45, "left", 12)
        after(40, capture)
    elseif name == "obj-loader" then
        shady.camera("yaw", 0.38)
        shady.camera("pitch", 0.22)
        after(500, capture)
    else
        after(1700, capture)
    end
end
shady.on("window.mapped", function(w)
    if not w.app_id:match("^plugin%-preview%-%d$") then return end
    windows[#windows+1] = w
    if started or #windows < count then return end
    started = true
    after(450, function()
        -- Arrange windows with the public pointer API before demonstrating effects.
        -- Position-writing plugins arrange their own scene.
        if name ~= "orbit-layout" and name ~= "astral-loom" and name ~= "magnetic-windows" then
            local slots = name:match("^fps%-") and {{180, 270}, {440, 275}, {700, 280}}
                or {{175, 150}, {710, 185}, {455, 415}}
            for i, win in ipairs(windows) do
                local dest = slots[i]
                shady.automation.drag(win.x+60, win.y-14, dest[1]+60, dest[2]-14, "left", 12)
            end
        end
        after(400, demonstrate)
    end)
end)
if name == "counter" then
    after(150, function() assert(shady.plugins.reload("counter")) end)
end
for i=1,count do
    after(250+(i-1)*220, function()
        shady.spawn(string.format("foot --config=%q --app-id=plugin-preview-%d --title=%q --window-size-pixels=420x240 python3 %q %q %d",
            root .. "/website/scripts/plugin-previews/foot.ini", i, name .. " / " .. ({"Live surface", "Native plugin", "Wayland client"})[i],
            root .. "/website/scripts/plugin-previews/client.py", name, i))
    end)
end
after(12000, function() error("plugin preview timed out: " .. name) end)
