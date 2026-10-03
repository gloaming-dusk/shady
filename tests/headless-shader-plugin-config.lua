shady.set("spatial_mode", true)
shady.plugin("./build/libshady-plugin-shader-overlay.so")
shady.modules({
    ["window-motion"] = true,
    ["physics"] = false,
    ["fps"] = false,
    ["close-animation"] = false,
    ["scene-effects"] = true,
    ["lua"] = true,
    ["shader-overlay"] = true,
})
