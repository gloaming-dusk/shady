-- An application made with shady-shell --app is an ordinary window: tiled,
-- sized by the compositor, clicked and typed into, closed; and it ends then.
local auto = shady.automation
local function fail(message)
    shady.log("window-test: FAIL: " .. message)
    shady.quit()
end
local app
shady.on("window.mapped", function(w)
    app = w
    if w.app_id ~= "window-test" then return fail("app_id is " .. tostring(w.app_id)) end
    if w.title ~= "Window test" then return fail("title is " .. tostring(w.title)) end
    if not w.tiled then return fail("the window is not tiled") end
    auto.after(300, function()
        local x = app.x
        if x ~= 10 then return fail("not at the gap: " .. x) end
        -- Settings that apply while the compositor runs.
        shady.set("layout_gap", 30)
        if app.x ~= 30 then return fail("layout_gap did not apply: " .. app.x) end
        shady.set("layout_gap", 10)
        auto.move_pointer(app.x + app.geometry_width / 2, app.y + app.geometry_height / 2)
        auto.click("left")
        auto.after(300, function() assert(auto.type_text("x")) end)
        auto.after(900, function() app:close() end)
    end)
end)
shady.on("window.unmapped", function()
    auto.after(500, function() shady.log("window-test: finished"); shady.quit() end)
end)
shady.spawn([[sh -c '"$SHADY_SHELL_BIN" --app "$SHADY_WINDOW_APP"; echo "window-test: the application ended"']])
auto.after(10000, function() fail("timed out") end)
