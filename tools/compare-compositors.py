#!/usr/bin/env python3
"""Measure Shady's default desktop against other compositors, nested.

Each compositor runs in a window of the current Wayland session, so the outer
compositor gives them all the same size and frame rate, with the same clients:

  idle     three terminals, nothing changing
  video    one window animating every frame beside two terminals
  scroll   the focus moved back and forth along the strip (scrolling
           compositors only)

For each it reports the compositor process's CPU, its GPU time (from the DRM
fdinfo of the process, so the outer compositor's work is not counted) and its
resident memory. Shady's shell is a separate process and is counted apart.

    nix shell nixpkgs#foot nixpkgs#glmark2 nixpkgs#labwc -c \\
        ./tools/compare-compositors.py [--seconds 8] [--json out.json] [name...]

Names: tide, tide-light, niri, labwc (default: all that are installed).
"""

import argparse
import glob
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TICK = os.sysconf("SC_CLK_TCK")

NIRI_CONFIG = """
hotkey-overlay { skip-at-startup; }
prefer-no-csd
layout {
    gaps 10
    default-column-width { proportion 0.5; }
    focus-ring { width 2; }
}
window-rule {
    geometry-corner-radius 12
    clip-to-geometry true
}
"""


def sockets(runtime):
    return set(glob.glob(os.path.join(runtime, "wayland-*")))


def cpu_seconds(pid):
    with open(f"/proc/{pid}/stat") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / TICK


def rss_mib(pid):
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith("VmRSS:"):
                return int(line.split()[1]) / 1024
    return 0.0


def gpu_counters(pid):
    """busy and total per DRM client and engine; total is None for ns counters."""
    clients = {}
    for path in glob.glob(f"/proc/{pid}/fdinfo/*"):
        try:
            with open(path) as f:
                text = f.read()
        except OSError:
            continue
        client = re.search(r"^drm-client-id:\s*(\d+)", text, re.M)
        if not client:
            continue
        counters = {}
        for name, value in re.findall(r"^drm-cycles-(\w+):\s*(\d+)", text, re.M):
            total = re.search(rf"^drm-total-cycles-{name}:\s*(\d+)", text, re.M)
            if total:
                counters[name] = (int(value), int(total.group(1)))
        for name, value in re.findall(r"^drm-engine-(\w+):\s*(\d+) ns", text, re.M):
            counters[name] = (int(value), None)
        clients[client.group(1)] = counters
    return clients


def gpu_percent(before, after, seconds):
    """The busiest engine's share of one engine, over the interval."""
    engines = {}
    for client, counters in after.items():
        for name, (value, total) in counters.items():
            start = before.get(client, {}).get(name)
            if start is None:
                continue
            busy = value - start[0]
            if total is None:
                span = seconds * 1e9
            else:
                span = total - start[1]
            if span > 0:
                engines[name] = engines.get(name, 0.0) + 100.0 * busy / span
    return max(engines.values(), default=None)


class Sampler:
    def __init__(self, pid):
        self.pid = pid

    def begin(self):
        self.t = time.monotonic()
        self.cpu = cpu_seconds(self.pid)
        self.gpu = gpu_counters(self.pid)

    def end(self):
        seconds = time.monotonic() - self.t
        return {
            "cpu": 100.0 * (cpu_seconds(self.pid) - self.cpu) / seconds,
            "gpu": gpu_percent(self.gpu, gpu_counters(self.pid), seconds),
            "rss": rss_mib(self.pid),
        }


class Compositor:
    name = ""
    scrolling = True
    needs = ()

    def __init__(self, workdir):
        self.workdir = workdir
        self.runtime = os.environ["XDG_RUNTIME_DIR"]
        self.env = dict(os.environ)
        for key in ("SHADY_SOCKET", "NIRI_SOCKET", "SHADY_RICE"):
            self.env.pop(key, None)
        self.shell = None
        self.clients = []

    def available(self):
        return all(shutil.which(n) or Path(n).exists() for n in self.needs)

    def command(self):
        raise NotImplementedError

    def client_env(self):
        env = dict(self.env)
        env["WAYLAND_DISPLAY"] = self.display
        return env

    def start(self):
        before = sockets(self.runtime)
        self.log = open(os.path.join(self.workdir, self.name + ".log"), "w")
        self.process = subprocess.Popen(self.command(), env=self.env,
            stdout=self.log, stderr=self.log, cwd=ROOT)
        deadline = time.monotonic() + 15
        new = set()
        while time.monotonic() < deadline and self.process.poll() is None:
            new = {s for s in sockets(self.runtime) - before if not s.endswith(".lock")}
            if new:
                break
            time.sleep(0.05)
        if not new:
            raise RuntimeError(f"{self.name}: no Wayland socket (see {self.log.name})")
        self.display = os.path.basename(sorted(new)[0])
        time.sleep(1.0)
        self.after_start()

    def after_start(self):
        pass

    def spawn(self, *argv):
        process = subprocess.Popen(argv, env=self.client_env(),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.clients.append(process)
        return process

    def terminal(self):
        return self.spawn("foot", "--config=/dev/null", "sh", "-c", "sleep 3600")

    def scroll(self, direction):
        raise NotImplementedError

    def stop(self):
        for process in self.clients + ([self.shell] if self.shell else []):
            if process.poll() is None:
                process.terminate()
        for process in self.clients + ([self.shell] if self.shell else []):
            try:
                process.wait(3)
            except subprocess.TimeoutExpired:
                process.kill()
        self.clients = []
        self.process.send_signal(signal.SIGTERM)
        try:
            self.process.wait(5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.log.close()


class Tide(Compositor):
    name = "tide"
    needs = (str(ROOT / "build/shady"), str(ROOT / "build/shady-shell"), "foot")
    light = False

    def __init__(self, workdir):
        super().__init__(workdir)
        rice = ROOT / "rices/tide"
        self.env.update({
            "SHADY_ROOT": str(ROOT),
            "SHADY_LUA_INIT": str(rice / "init.lua"),
            "SHADY_SHELL_CONFIG": str(rice / "shell.lua"),
            "WLR_BACKENDS": "wayland",
            "TIDE_IDLE_LOCK": "0",
            "TIDE_IDLE_SCREEN_OFF": "0",
            "TIDE_LIGHT": "1" if self.light else "0",
        })
        for line in (rice / "theme.sh").read_text().splitlines():
            match = re.match(r'export (\w+)="\$\{\w+:-(#\w+)\}"', line)
            if match:
                self.env[match.group(1)] = match.group(2)

    def command(self):
        return [str(ROOT / "build/shady"), "-c", str(ROOT / "rices/tide/config.lua")]

    def after_start(self):
        self.shell = subprocess.Popen([str(ROOT / "build/shady-shell")], env=self.client_env(),
            stdout=self.log, stderr=self.log)
        time.sleep(1.5)

    def scroll(self, direction):
        subprocess.run([str(ROOT / "build/shadyctl"), "lua", f'shady.layout.focus("{direction}")'],
            env=self.client_env(), stdout=subprocess.DEVNULL, check=False)


class TideLight(Tide):
    name = "tide-light"
    light = True


class Niri(Compositor):
    name = "niri"
    needs = ("niri", "foot")

    def command(self):
        path = os.path.join(self.workdir, "niri.kdl")
        Path(path).write_text(NIRI_CONFIG)
        return ["niri", "-c", path]

    def scroll(self, direction):
        found = glob.glob(os.path.join(self.runtime, f"niri.{self.display}.*.sock"))
        env = self.client_env()
        env["NIRI_SOCKET"] = found[0]
        subprocess.run(["niri", "msg", "action", f"focus-column-{direction}"],
            env=env, stdout=subprocess.DEVNULL, check=False)


class Labwc(Compositor):
    name = "labwc"
    scrolling = False
    needs = ("labwc", "foot")

    def __init__(self, workdir):
        super().__init__(workdir)
        self.env["WLR_BACKENDS"] = "wayland"

    def command(self):
        path = os.path.join(self.workdir, "labwc")
        os.makedirs(path, exist_ok=True)
        return ["labwc", "-C", path]


ALL = [Tide, TideLight, Niri, Labwc]


def frames_per_second(output):
    rates = re.findall(r"FPS: ([\d.]+)", output)
    return float(rates[-1]) if rates else None


def measure(compositor, seconds):
    results = {}
    compositor.start()
    sampler = Sampler(compositor.process.pid)
    shell = Sampler(compositor.shell.pid) if compositor.shell else None

    def sample(action=None):
        sampler.begin()
        if shell:
            shell.begin()
        end = time.monotonic() + seconds
        direction = "left"
        step = 0
        while time.monotonic() < end:
            if action:
                action(direction)
                step += 1
                if step % 2 == 0:
                    direction = "right" if direction == "left" else "left"
                time.sleep(0.25)
            else:
                time.sleep(0.2)
        result = sampler.end()
        if shell:
            result["shell"] = shell.end()
        return result

    try:
        for _ in range(3):
            compositor.terminal()
            time.sleep(0.5)
        time.sleep(2.5)
        results["idle"] = sample()
        animated = compositor.spawn("glmark2-es2-wayland", "--swap-mode", "fifo",
            "-b", f"build:duration={seconds + 3:.0f}")
        time.sleep(2.0)
        results["video"] = sample()
        output, _ = animated.communicate(timeout=10)
        results["video"]["client_fps"] = frames_per_second(output or "")
        time.sleep(1.5)
        if compositor.scrolling:
            results["scroll"] = sample(compositor.scroll)
    finally:
        compositor.stop()
    return results


def cell(value, suffix=""):
    return "n/a" if value is None else f"{value:.1f}{suffix}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*")
    parser.add_argument("--seconds", type=float, default=8.0)
    parser.add_argument("--json")
    args = parser.parse_args()

    if not os.environ.get("WAYLAND_DISPLAY"):
        sys.exit("compare-compositors: run this inside a Wayland session")
    wanted = args.names or [c.name for c in ALL]
    unknown = set(wanted) - {c.name for c in ALL}
    if unknown:
        sys.exit(f"compare-compositors: unknown: {', '.join(sorted(unknown))}")

    results = {}
    with tempfile.TemporaryDirectory(prefix="compare-compositors-") as workdir:
        for kind in ALL:
            if kind.name not in wanted:
                continue
            compositor = kind(workdir)
            if not compositor.available():
                print(f"{kind.name}: skipped, not installed", file=sys.stderr)
                continue
            print(f"{kind.name}: measuring", file=sys.stderr)
            try:
                results[kind.name] = measure(compositor, args.seconds)
            except Exception as error:
                print(f"{kind.name}: failed: {error}", file=sys.stderr)
                log = Path(workdir) / f"{kind.name}.log"
                if log.exists():
                    print(log.read_text()[-2000:], file=sys.stderr)

    print(f"{'':12}{'workload':10}{'CPU %':>8}{'GPU %':>8}{'RSS MiB':>9}{'shell CPU %':>13}{'client fps':>12}")
    for name, workloads in results.items():
        for workload, r in workloads.items():
            shell = r.get("shell", {})
            print(f"{name:12}{workload:10}{cell(r['cpu']):>8}{cell(r['gpu']):>8}{cell(r['rss']):>9}"
                f"{cell(shell.get('cpu')):>13}{cell(r.get('client_fps')):>12}")
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
