#!/usr/bin/env python3
"""Drive a headless Shady the way a computer-use agent does, over IPC.

Keys and text reach the focused window (the probe decodes them with the
keymap it was sent, Hangul included), the pointer clicks, scrolls and drags
in layout coordinates, `wait` settles, screenshots answer once written with
the geometry that maps their pixels back, answers stay in request order, and
a client that leaves mid-drag leaves nothing pressed.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent


class Fail(Exception):
    pass


def check(cond, message):
    if not cond:
        raise Fail(message)


class Ipc:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.sock.settimeout(10)
        self.buf = b""
        self.next_id = 1

    def send(self, cmd, **args):
        rid = self.next_id
        self.next_id += 1
        self.sock.sendall((json.dumps({"id": rid, "cmd": cmd, **args}) + "\n").encode())
        return rid

    def read(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise Fail("compositor closed the socket")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        msg = json.loads(line)
        return self.read() if "event" in msg else msg

    def call(self, cmd, **args):
        rid = self.send(cmd, **args)
        reply = self.read()
        check(reply.get("id") == rid, f"{cmd}: answer for {reply.get('id')}, expected {rid}")
        return reply

    def ok(self, cmd, **args):
        reply = self.call(cmd, **args)
        check(reply["ok"], f"{cmd} {args}: {reply.get('error')}")
        return reply.get("data")

    def err(self, cmd, **args):
        reply = self.call(cmd, **args)
        check(not reply["ok"], f"{cmd} {args} should fail: {reply}")
        return reply["error"]

    def close(self):
        self.sock.close()


def png_size(path):
    with open(path, "rb") as f:
        head = f.read(24)
    check(head[:8] == b"\x89PNG\r\n\x1a\n", f"{path} is not a PNG")
    return struct.unpack(">II", head[16:24])


def status_lines(path):
    return Path(path).read_text().splitlines() if Path(path).exists() else []


def wait_for(pred, what, timeout=5.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if pred():
            return
        time.sleep(0.02)
    raise Fail(f"timed out waiting for {what}")


def scenario(ipc, status, shots):
    windows = ipc.ok("windows")
    probe = next(w for w in windows if w["app_id"] == "computer-use-probe")
    wid, wx, wy, ww, wh = probe["id"], probe["x"], probe["y"], probe["width"], probe["height"]
    check(probe["focused"], "probe is not focused")
    cx, cy = wx + ww / 2, wy + wh / 2

    # A shortcut is the compositor's: handled, never sent to the window.
    check(ipc.ok("key", keys="Ctrl+F12") == {"handled": True, "sent": False}, "Ctrl+F12")

    # Text in the keymap, Shift where it needs it; then text that is not.
    ascii_text = "Hello, World 1+1=2"
    check(ipc.ok("type", text=ascii_text) == {"typed": len(ascii_text)}, "type ascii")
    korean = "안녕 Shady! ✓"
    check(ipc.ok("type", text=korean) == {"typed": len(korean)}, "type unicode")
    want = (ascii_text + korean).encode().hex()
    wait_for(lambda: f"TEXT={want}" in status_lines(status), "typed text")
    # After a text keymap, the configured one is back: Shift works again.
    ipc.ok("type", text="A")
    want = (ascii_text + korean + "A").encode().hex()
    wait_for(lambda: f"TEXT={want}" in status_lines(status), "text after keymap")

    # An unbound shortcut goes to the window, with its modifiers.
    check(ipc.ok("key", keys="Ctrl+a") == {"handled": False, "sent": True}, "Ctrl+a")
    wait_for(lambda: "KEYSYM=a MODS=ctrl," in status_lines(status), "Ctrl+a in the window")
    ipc.ok("key", keys="Return")
    wait_for(lambda: "KEYSYM=Return MODS=" in status_lines(status), "Return")
    # Held keys: Shift down, a key, Shift up.
    ipc.ok("key.down", keys="Shift")
    ipc.ok("key", keys="F5")
    ipc.ok("key.up", keys="Shift")
    wait_for(lambda: "KEYSYM=F5 MODS=shift," in status_lines(status), "Shift held for F5")

    # Clicks land at the layout point, in window coordinates.
    ipc.ok("pointer.click", x=cx, y=cy)
    wait_for(lambda: f"BUTTON=272 {ww / 2:.3f} {wh / 2:.3f}" in status_lines(status), "left click")
    ipc.ok("pointer.click", x=wx + 10, y=wy + 20, button="right", count=2)
    wait_for(lambda: status_lines(status).count("BUTTON=273 10.000 20.000") == 2, "double right click")
    pointer = ipc.ok("pointer")
    check((pointer["x"], pointer["y"], pointer["window"]) == (wx + 10, wy + 20, wid), f"pointer {pointer}")
    check(ipc.ok("window.at", x=cx, y=cy)["id"] == wid, "window.at the probe")
    check(ipc.ok("window.at", x=wx + ww + 300, y=wy + wh + 300) is None, "window.at nothing")

    ipc.ok("pointer.scroll", x=cx, y=cy, dy=2)
    wait_for(lambda: "SCROLL=PASS" in status_lines(status), "scroll")

    # A drag over time answers once it lets go.
    t0 = time.monotonic()
    ipc.ok("pointer.drag", x1=wx + 30, y1=wy + 30, x2=wx + 150, y2=wy + 90, duration=200, steps=5)
    check(time.monotonic() - t0 >= 0.15, "drag answered before it was done")
    wait_for(lambda: "DRAG=PASS" in status_lines(status), "drag")

    # Settling: nothing changes, so it settles; at least `ms` passes.
    t0 = time.monotonic()
    settled = ipc.ok("wait", ms=100, idle=150, timeout=3000)
    check(settled["settled"] and time.monotonic() - t0 >= 0.14, f"wait {settled}")

    # Answers stay in order: a wait, then a query sent right behind it.
    first = ipc.send("wait", ms=150)
    second = ipc.send("version")
    a, b = ipc.read(), ipc.read()
    check((a["id"], b["id"]) == (first, second), f"answers out of order: {a} {b}")

    # Screenshots answer when written, scaled down, and say where they lie.
    outputs = ipc.ok("outputs")
    ow, oh = outputs[0]["width"], outputs[0]["height"]
    full = ipc.ok("screenshot", path=str(shots / "full.png"), wait=True, max_width=ow // 2)
    check(full["width"] == ow // 2 and full["height"] == oh // 2, f"scaled screenshot {full}")
    check(abs(full["scale"] - 0.5) < 1e-6 and full["x"] == 0 and full["y"] == 0, f"geometry {full}")
    check(png_size(full["path"]) == (ow // 2, oh // 2), "scaled PNG size")
    win = ipc.ok("screenshot", path=str(shots / "window.png"), window=wid, wait=True)
    check((win["width"], win["height"], win["x"], win["y"]) == (ww, wh, wx, wy), f"window shot {win}")
    check(png_size(win["path"]) == (ww, wh), "window PNG size")
    region = ipc.ok("screenshot", path=str(shots / "region.png"), wait=True,
                    x=wx + 10, y=wy + 10, width=40, height=30)
    check((region["width"], region["height"]) == (40, 30), f"region shot {region}")
    # Without wait, the old answer: the path only.
    plain = ipc.ok("screenshot", path=str(shots / "plain.png"))
    check(plain == {"path": str(shots / "plain.png")}, f"plain screenshot {plain}")
    ipc.ok("wait", ms=100)

    # Bad requests fail and the connection goes on.
    ipc.err("pointer.move", x=10)
    ipc.err("pointer.click", button="thumb")
    ipc.err("pointer.scroll")
    ipc.err("screenshot", x=1, y=1, width=10)
    ipc.err("screenshot", output="NOPE-1", wait=True)
    ipc.err("wait", ms=-1)
    ipc.err("key", keys="Ctrl+NoSuchKey")
    check(ipc.ok("version")["protocol"] == 1, "still answering")

    # A client that leaves mid-drag: the drag lets go, the next click works.
    gone = Ipc(ipc.sock.getpeername())
    gone.send("pointer.drag", x1=wx + 5, y1=wy + 5, x2=wx + 100, y2=wy + 100, duration=3000)
    time.sleep(0.2)
    gone.close()
    ipc.ok("wait", ms=150)
    before = status_lines(status).count("BUTTON=272 50.000 60.000")
    ipc.ok("pointer.click", x=wx + 50, y=wy + 60)
    wait_for(lambda: status_lines(status).count("BUTTON=272 50.000 60.000") == before + 1,
             "a click after an abandoned drag")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", default=os.environ.get("SHADY_DAILY_BUILD_DIR", "build-daily-driver"))
    args = parser.parse_args()
    build = Path(args.build_dir)
    if not build.is_absolute():
        build = ROOT / build
    for name in ("shady", "headless-automation-probe"):
        if not (build / name).is_file():
            parser.error(f"missing {build / name}")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        runtime = tmp / "runtime"
        runtime.mkdir(mode=0o700)
        status = tmp / "probe.status"
        status.touch()
        env = {k: v for k, v in os.environ.items()
               if k not in ("WAYLAND_DISPLAY", "WAYLAND_SOCKET", "SHADY_SOCKET")}
        env.update({
            "XDG_RUNTIME_DIR": str(runtime),
            "WLR_BACKENDS": "headless",
            "WLR_HEADLESS_OUTPUTS": "1",
            "WLR_RENDERER": "pixman",
            "XKB_DEFAULT_LAYOUT": "us",
            "SHADY_LUA_INIT": str(ROOT / "tests/headless-computer-use-init.lua"),
            "SHADY_AUTOMATION_PROBE": str(build / "headless-automation-probe"),
            "SHADY_AUTOMATION_PROBE_APP_ID": "computer-use-probe",
            "SHADY_AUTOMATION_PROBE_STATUS": str(status),
            "SHADY_AUTOMATION_PROBE_TEXT": "1",
            "SHADY_AUTOMATION_POINTER_TEST": "1",
            "ASAN_OPTIONS": os.environ.get("ASAN_OPTIONS", "abort_on_error=1:halt_on_error=1:detect_leaks=0"),
            "UBSAN_OPTIONS": os.environ.get("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1"),
        })
        log_path = tmp / "compositor.log"
        with open(log_path, "w") as log:
            comp = subprocess.Popen([str(build / "shady"), "--safe", "-c",
                                     str(ROOT / "tests/headless-automation-config.lua")],
                                    env=env, stdout=log, stderr=subprocess.STDOUT)
        ok = False
        try:
            wait_for(lambda: "computer-use: ready" in log_path.read_text() or comp.poll() is not None,
                     "the probe window", timeout=10)
            check(comp.poll() is None, "compositor exited early")
            time.sleep(0.2)  # the probe's first frame
            ipc = Ipc(str(runtime / "shady-wayland-0.sock"))
            scenario(ipc, status, tmp)
            ipc.ok("quit")
            ipc.close()
            check(comp.wait(timeout=10) == 0, f"compositor exited with {comp.returncode}")
            text = log_path.read_text()
            check("computer-use: shortcut" in text, "Ctrl+F12 binding never ran")
            for bad in ("AddressSanitizer", "runtime error:", "UndefinedBehaviorSanitizer"):
                check(bad not in text, f"sanitizer: {bad}")
            ok = True
        except Fail as e:
            print(f"headless-computer-use: FAIL: {e}", file=sys.stderr)
        finally:
            if comp.poll() is None:
                comp.kill()
                comp.wait()
            if not ok:
                sys.stderr.write(log_path.read_text()[-6000:])
                sys.stderr.write("\n--- probe status ---\n" + status.read_text()[-3000:])
    if not ok:
        return 1
    print("headless-computer-use: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
