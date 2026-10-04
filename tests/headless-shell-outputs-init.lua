-- Start the shell; tests/headless-shell-outputs.sh changes outputs with
-- wlr-randr and ends the session through the IPC socket.
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
shady.automation.after(25000, function()
    shady.log("shell-outputs: timed out")
    shady.quit()
end)
