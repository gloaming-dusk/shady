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
    programs.shady.softwareCursor = true;
    environment.systemPackages = [ pkgs.firefox ];

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
    # Real machines are multi-core; the shell once failed to show only there.
    virtualisation.cores = 4;
  };

  testScript = ''

    def as_alice(cmd):
        return machine.succeed(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 " + cmd + "'"
        )

    def shell_visible():
        # The default rice (Horizon) writes the time in large white serif
        # numerals across the dark top-left sky, which holds nothing that
        # bright but a few one-pixel stars. Read what is scanned out (QEMU's
        # screendump), not the compositor's own frame: with llvmpipe the two
        # once differed and only the display lost the shell.
        import os, re, tempfile
        path = os.path.join(tempfile.mkdtemp(), "screen.ppm")
        machine.send_monitor_command(f"screendump {path}")
        with open(path, "rb") as f:
            ppm = f.read()
        header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", ppm)
        assert header, ppm[:32]
        width = int(header[1])
        pixels = ppm[header.end():]
        white = sum(
            1 for y in range(25, 175) for x in range(50, 340)
            for i in [(y * width + x) * 3]
            if pixels[i] > 220 and pixels[i + 1] > 220 and pixels[i + 2] > 220
        )
        return white > 500

    def wait_for_shell():
        retry(lambda _: shell_visible(), 30)

    machine.wait_for_unit("greetd.service")
    machine.wait_until_succeeds("pgrep -u alice -x shady", timeout=60)
    machine.wait_until_succeeds("pgrep -u alice -x shady-shell", timeout=60)
    machine.wait_for_file("/run/user/1000/shady-wayland-0.sock")

    with subtest("the shell is on screen"):
        wait_for_shell()

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

    with subtest("Firefox opens a native Wayland window"):
        machine.succeed(
            f"su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY={display} "
            "MOZ_ENABLE_WAYLAND=1 firefox --no-remote --new-instance about:blank "
            ">/tmp/firefox-session.log 2>&1 &'"
        )
        machine.wait_until_succeeds(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 shadyctl windows' | grep -qi 'Mozilla Firefox'",
            timeout=90,
        )
        import json
        windows = json.loads(as_alice("shadyctl windows"))
        browser = next(w for w in windows if w["app_id"] in ["firefox", "org.mozilla.firefox"])
        workspaces = json.loads(as_alice("shadyctl workspaces"))
        current = workspaces["current"]
        assert browser["workspace"] == current, browser
        assert browser["focused"], browser

    machine.sleep(3)
    machine.screenshot("shady-session")

    with subtest("Ctrl+Alt+F2 switches to another VT"):
        assert machine.succeed("fgconsole").strip() == "1"
        machine.send_key("ctrl-alt-f2")
        machine.wait_until_succeeds("[ $(fgconsole) = 2 ]", timeout=10)
        machine.send_key("ctrl-alt-f1")
        machine.wait_until_succeeds("[ $(fgconsole) = 1 ]", timeout=10)

    with subtest("the shell comes back after a VT round trip"):
        machine.sleep(3)
        session_log = machine.succeed("journalctl -b -t shady -o cat")
        print("\n".join(l for l in session_log.splitlines()
                        if "output" in l or "shady-shell" in l or "layer" in l))
        # Horizon's desk (clock, horizon, reflections) is on every output.
        shown = session_log.count("shady-shell: horizon-desk on ")
        removed = session_log.count("shady-shell: horizon-desk removed from ")
        assert shown > removed, f"desk shown {shown} times, removed {removed} times"
        assert "Fontconfig error" not in session_log, "shady-shell has no fontconfig"
        wait_for_shell()
        machine.screenshot("after-vt-switch")
  '';
}
