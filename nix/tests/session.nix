# Boot a machine with programs.shady, log in through greetd, and check that
# the compositor, shell, IPC and D-Bus activation environment come up.
self:
{
  name = "shady-session";

  nodes.machine = { lib, pkgs, ... }: {
    imports = [ self.nixosModules.default ];

    # What the installer ISO base sets; shady-shell cannot draw without it.
    fonts.fontconfig.enable = lib.mkOverride 500 false;

    programs.shady.enable = true;

    users.users.alice = {
      isNormalUser = true;
      uid = 1000;
    };

    services.greetd = {
      enable = true;
      settings.default_session.command = "${pkgs.greetd}/bin/agreety --cmd shady-session";
      settings.initial_session = {
        user = "alice";
        command = "shady-session";
      };
    };

    # QEMU has no GPU; let wlroots render with llvmpipe on virtio-gpu.
    environment.sessionVariables.WLR_RENDERER_ALLOW_SOFTWARE = "1";
    virtualisation.qemu.options = [ "-vga none" "-device virtio-gpu-pci" ];
    virtualisation.memorySize = 2048;
    # Real machines are multi-core; the shell bar once failed to show only there.
    virtualisation.cores = 4;
  };

  testScript = ''
    from datetime import timedelta

    def as_alice(cmd):
        return machine.succeed(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 " + cmd + "'"
        )

    def bar_visible():
        # The Afterglow bar starts with an orange "S" badge in the top-left
        # corner; the sky behind it has no orange that high up. Read what is
        # scanned out (QEMU's screendump), not the compositor's own frame:
        # with llvmpipe the two once differed and only the display lost the bar.
        import os, re, tempfile
        path = os.path.join(tempfile.mkdtemp(), "screen.ppm")
        machine.send_monitor_command(f"screendump {path}")
        with open(path, "rb") as f:
            ppm = f.read()
        header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", ppm)
        assert header, ppm[:32]
        width = int(header[1])
        pixels = ppm[header.end():]
        orange = sum(
            1 for y in range(38) for x in range(64)
            for i in [(y * width + x) * 3]
            if pixels[i] > 200 and 90 < pixels[i + 1] < 200 and pixels[i + 2] < 150
        )
        return orange > 40

    def wait_for_bar():
        retry(lambda _: bar_visible(), timeout=timedelta(seconds=30))

    machine.wait_for_unit("greetd.service")
    machine.wait_until_succeeds("pgrep -u alice -x shady", timeout=60)
    machine.wait_until_succeeds("pgrep -u alice -x shady-shell", timeout=60)
    machine.wait_for_file("/run/user/1000/shady-wayland-0.sock")

    with subtest("the shell bar is on screen"):
        wait_for_bar()

    with subtest("rice and plugins loaded"):
        plugins = as_alice("shadyctl plugins")
        for name in ["afterglow", "spatial-overview", "fps-folded-paper"]:
            assert name in plugins, f"{name} missing from: {plugins}"

    with subtest("session is visible to D-Bus activated services"):
        machine.wait_until_succeeds(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 "
            "systemctl --user show-environment' | grep -q '^XDG_CURRENT_DESKTOP=shady$'",
            timeout=30,
        )
        env = as_alice("systemctl --user show-environment")
        assert "WAYLAND_DISPLAY=wayland-" in env, env

    with subtest("clients can open windows"):
        display = machine.succeed("ls /run/user/1000 | grep -m1 -E '^wayland-[0-9]+$'").strip()
        machine.succeed(
            f"su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY={display} foot >/dev/null 2>&1 &'"
        )
        machine.wait_until_succeeds(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 shadyctl windows' | grep -q foot",
            timeout=30,
        )

    machine.sleep(duration=timedelta(seconds=3))
    machine.screenshot("shady-session")

    with subtest("Ctrl+Alt+F2 switches to another VT"):
        assert machine.succeed("fgconsole").strip() == "1"
        machine.send_key("ctrl-alt-f2")
        machine.wait_until_succeeds("[ $(fgconsole) = 2 ]", timeout=10)
        machine.send_key("ctrl-alt-f1")
        machine.wait_until_succeeds("[ $(fgconsole) = 1 ]", timeout=10)

    with subtest("the shell bar comes back after a VT round trip"):
        machine.sleep(duration=timedelta(seconds=3))
        session_log = machine.succeed("journalctl -b -t shady -o cat")
        print("\n".join(l for l in session_log.splitlines()
                        if "output" in l or "shady-shell" in l or "layer" in l))
        bars = session_log.count("shady-shell: shady-shell on ")
        removed = session_log.count("shady-shell: shady-shell removed from ")
        assert bars > removed, f"bar shown {bars} times, removed {removed} times"
        assert "Fontconfig error" not in session_log, "shady-shell has no fontconfig"
        wait_for_bar()
        machine.screenshot("after-vt-switch")
  '';
}
