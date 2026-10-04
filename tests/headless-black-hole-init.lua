-- Open and close the black hole without windows, press again while busy,
-- then hot-reload and unload. Shaders are compiled at plugin init, so a GLSL
-- error fails the load before any of this runs.
local after = shady.automation.after
after(300, function()
    assert(shady.automation.key("Super+h"))
    after(300, function()
        assert(shady.automation.key("Super+h")) -- still opening: ignored
        after(2500, function()
            shady.log("black-hole-test: cycle done")
            assert(shady.plugins.reload("black-hole"))
            after(200, function()
                assert(shady.plugins.unload("black-hole"))
                shady.log("black-hole-test: PASS")
                shady.quit()
            end)
        end)
    end)
end)
