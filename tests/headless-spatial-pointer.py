#!/usr/bin/env python3
"""Check spatial hit testing, client clicks, overlay priority and cursor pixels."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = Path(args.build_dir).resolve()
    grim = shutil.which("grim")
    if not grim:
        parser.error("grim must be on PATH")
    for name in ("shady", "shady-shell", "headless-automation-probe"):
        if not (build / name).is_file():
            parser.error(f"missing {build / name}")

    with tempfile.TemporaryDirectory(prefix="shady-spatial-pointer-") as directory:
        tmp = Path(directory)
        runtime = tmp / "runtime"
        runtime.mkdir(mode=0o700)
        status = tmp / "status"
        status.touch()
        (tmp / "shell.lua").write_text('''
shell.popup {
    name = "pointer-overlay", layer = "overlay",
    anchor = { "top", "left" }, width = 200, height = 20,
    view = function()
        return shell.box { width = 200, height = 20, background = "#00ff00",
            on_click = function()
                local f = assert(io.open(os.getenv("POINTER_STATUS"), "a"))
                f:write("OVERLAY=PASS\\n"); f:close()
            end }
    end,
}
shell.open("pointer-overlay")
''')
        (tmp / "init.lua").write_text('''
local after = shady.automation.after
local status = assert(os.getenv("POINTER_STATUS"))
local function mark(text)
    local f = assert(io.open(status, "a")); f:write(text, "\\n"); f:close()
end
local function wait_for(token, callback)
    local function poll()
        local f = assert(io.open(status)); local text = f:read("a"); f:close()
        if text:find(token, 1, true) then callback() else after(30, poll) end
    end
    poll()
end
shady.on("window.mapped", function(window)
    if window.app_id ~= "pointer-probe" then return end
    after(300, function()
        -- Move the visible window offscreen without changing its flat rect.
        shady.camera("target_x", 2.0)
        after(150, function()
            local x, y = window.x, window.y
            shady.automation.move_pointer(x + 80, y + 80)
            shady.automation.click("left")
            -- The unprojected titlebar must not start an invisible drag.
            shady.automation.drag(x + 80, y - 14, x + 130, y + 6, "left", 4)
            assert(window.x == x and window.y == y, "ghost titlebar dragged the window")
            after(150, function() mark("GHOST=PASS") end)
            wait_for("NEXT", function()
                assert(window:maximize(true))
                shady.camera("target_x", 0.0)
                after(250, function()
                    shady.automation.move_pointer(window.x + 40, window.y + 50)
                    after(250, function()
                        mark("CURSOR=" .. (window.x + 40) .. "," .. (window.y + 50))
                    end)
                    wait_for("LEFT", function() shady.automation.click("left") end)
                    wait_for("RIGHT", function() shady.automation.click("right") end)
                    wait_for("BAR", function()
                        shady.automation.move_pointer(100, 8)
                        shady.automation.click("left")
                    end)
                end)
            end)
        end)
    end)
end)
shady.spawn(string.format("%q", assert(os.getenv("SHADY_SHELL_BIN"))))
shady.spawn(string.format("%q", assert(os.getenv("SHADY_AUTOMATION_PROBE"))))
''')
        env = os.environ.copy()
        for key in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "SHADY_SOCKET"):
            env.pop(key, None)
        env.update(
            XDG_RUNTIME_DIR=str(runtime), WLR_BACKENDS="headless",
            WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="gles2",
            LIBGL_ALWAYS_SOFTWARE="1", WLR_RENDERER_ALLOW_SOFTWARE="1",
            SHADY_SOFTWARE_CURSORS="1", SHADY_ROOT=str(root),
            SHADY_LUA_INIT=str(tmp / "init.lua"),
            SHADY_SHELL_BIN=str(build / "shady-shell"),
            SHADY_SHELL_CONFIG=str(tmp / "shell.lua"), SHADY_SHELL_RENDERER="shm",
            SHADY_AUTOMATION_PROBE=str(build / "headless-automation-probe"),
            SHADY_AUTOMATION_PROBE_APP_ID="pointer-probe",
            SHADY_AUTOMATION_PROBE_STATUS=str(status),
            SHADY_AUTOMATION_POINTER_TEST="1", POINTER_STATUS=str(status),
        )
        with (tmp / "log").open("w") as log:
            comp = subprocess.Popen(
                [str(build / "shady"), "-c", str(root / "tests/headless-titlebar-drag-spatial-config.lua")],
                env=env, stdout=log, stderr=subprocess.STDOUT,
            )

            def wait_for(pattern):
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    match = re.search(pattern, status.read_text(), re.MULTILINE)
                    if match:
                        return match
                    if comp.poll() is not None:
                        raise AssertionError("compositor exited")
                    time.sleep(0.03)
                raise AssertionError(f"timed out waiting for {pattern}")

            def send(token):
                with status.open("a") as stream:
                    stream.write(token + "\n")

            try:
                wait_for(r"^GHOST=PASS$")
                assert "BUTTON=" not in status.read_text(), "invisible flat window received a click"
                send("NEXT")
                match = wait_for(r"^CURSOR=(\d+),(\d+)$")
                x, y = map(int, match.groups())
                capture_env = env | {"WAYLAND_DISPLAY": "wayland-0"}
                ppm = subprocess.check_output([grim, "-c", "-t", "ppm", "-"], env=capture_env, timeout=10)
                header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", ppm)
                assert header, "invalid screenshot"
                width = int(header[1])
                pixels = ppm[header.end():]

                def pixel(px, py):
                    offset = (py * width + px) * 3
                    return tuple(pixels[offset:offset + 3])

                assert pixel(x + 3, y - 2) == (255, 0, 0), "cursor top/hotspot is incorrect"
                assert pixel(x + 3, y + 10) == (0, 0, 255), "cursor bottom is inverted or displaced"
                for token, button in (("LEFT", 272), ("RIGHT", 273)):
                    send(token)
                    match = wait_for(rf"^BUTTON={button} ([\d.]+) ([\d.]+)$")
                    sx, sy = map(float, match.groups())
                    assert abs(sx - 40) < 0.01 and abs(sy - 50) < 0.01, (token, sx, sy)
                send("BAR")
                wait_for(r"^OVERLAY=PASS$")
                print("spatial-pointer: PASS cursor orientation/hotspot, click coordinates, right-click, ghost misses, overlay priority")
            except Exception:
                print((tmp / "log").read_text())
                print(status.read_text())
                raise
            finally:
                comp.terminate()
                comp.wait(timeout=5)


if __name__ == "__main__":
    main()
