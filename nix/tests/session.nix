# Boot a machine with programs.shady, log in through greetd, and check that
# the compositor, shell, IPC and D-Bus activation environment come up.
self:
{
  name = "shady-session";

  nodes.machine = { pkgs, ... }: {
    imports = [ self.nixosModules.default ];

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
  };

  testScript = ''
    def as_alice(cmd):
        return machine.succeed(
            "su - alice -c 'XDG_RUNTIME_DIR=/run/user/1000 " + cmd + "'"
        )

    machine.wait_for_unit("greetd.service")
    machine.wait_until_succeeds("pgrep -u alice -x shady", timeout=60)
    machine.wait_until_succeeds("pgrep -u alice -x shady-shell", timeout=60)
    machine.wait_for_file("/run/user/1000/shady-wayland-0.sock")

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

    machine.sleep(duration=3)
    machine.screenshot("shady-session")
  '';
}
