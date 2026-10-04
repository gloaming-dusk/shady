#!/usr/bin/env python3
"""Regenerate genuine plugin screenshots with Shady's headless Automation API.

Run from the repository root:
    nix develop -c python3 website/scripts/capture-plugin-previews.py [plugin ...]
Build the compositor, plugins, and headless client tools first (see README).
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SCENE = ROOT / "website/scripts/plugin-previews"
OUTPUT = ROOT / "website/src/assets/plugins"
NAMES = re.findall(r"name: '([^']+)'", (ROOT / "website/src/lib/plugins.ts").read_text())
selected = sys.argv[1:] or NAMES
if set(selected) - set(NAMES):
    raise SystemExit("Unknown plugin name")
OUTPUT.mkdir(parents=True, exist_ok=True)
for name in selected:
    with tempfile.TemporaryDirectory(prefix="shady-preview-") as temp:
        runtime = Path(temp) / "runtime"
        runtime.mkdir(mode=0o700)
        log_path = Path(temp) / "compositor.log"
        target = OUTPUT / f"{name}.png"
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), WLR_BACKENDS="headless",
                   WLR_HEADLESS_OUTPUTS="1", WLR_RENDERER="gles2", LIBGL_ALWAYS_SOFTWARE="1",
                   SHADY_ROOT=str(ROOT), SHADY_LUA_INIT=str(SCENE / "init.lua"),
                   SHADY_CAPTURE_PLUGIN=name, SHADY_CAPTURE_TARGET=str(target),
                   SHADY_CAPTURE_LOG=str(log_path))
        # Do not inherit test/rice settings from an interactive session.
        env.pop("WAYLAND_DISPLAY", None)
        env.pop("SHADY_PLUGIN_PATH", None)
        with log_path.open("w") as log:
            try:
                result = subprocess.run([str(ROOT / "build/shady"), "-c", str(SCENE / "config.lua")],
                                        cwd=ROOT, env=env, stdout=log, stderr=log, timeout=18)
            except subprocess.TimeoutExpired:
                raise SystemExit(f"{name}: timed out\n{log_path.read_text()[-6000:]}")
        text = log_path.read_text()
        if result.returncode or f"plugin-preview: captured {name}" not in text or not target.exists():
            raise SystemExit(f"{name}: capture failed\n{text[-6000:]}")
        print(f"Captured {name}: {target.stat().st_size // 1024} KiB", flush=True)
