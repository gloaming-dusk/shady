-- Start the shell with the config from SHADY_SHELL_CONFIG; the test script
-- rewrites that file to exercise hot reload and ends via shadyctl.
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
shady.automation.after(25000, function()
    shady.log("shell-lua: timed out")
    shady.quit()
end)
