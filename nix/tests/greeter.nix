# Boot to the Shady greeter, fail one login, then log in with the right
# password and check that the user's Shady session starts.
self:
{
  name = "shady-greeter";

  nodes.machine = { lib, ... }: {
    imports = [ self.nixosModules.default ];

    programs.shady.enable = true;
    programs.shady.greeter.enable = true;

    users.users.alice = {
      isNormalUser = true;
      description = "Alice Liddell";
      initialPassword = "wonderland";
    };

    # QEMU has no GPU; let wlroots render with llvmpipe on virtio-gpu.
    environment.sessionVariables.WLR_RENDERER_ALLOW_SOFTWARE = "1";
    virtualisation.qemu.options = [ "-vga none" "-device virtio-gpu-pci" ];
    virtualisation.memorySize = 2048;
    virtualisation.cores = 4;
  };

  testScript = ''
    import os, re, tempfile

    def screen():
        path = os.path.join(tempfile.mkdtemp(), "screen.ppm")
        machine.send_monitor_command(f"screendump {path}")
        with open(path, "rb") as f:
            ppm = f.read()
        header = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", ppm)
        assert header, ppm[:32]
        return int(header[1]), int(header[2]), ppm[header.end():]

    def red_in(width, pixels, x0, y0, w, h):
        return sum(
            1 for y in range(y0, y0 + h) for x in range(x0, x0 + w)
            for i in [(y * width + x) * 3]
            if pixels[i] > 200 and pixels[i + 1] < 140 and pixels[i] - pixels[i + 1] > 100
        )

    def dark_fraction(width, pixels, x0, y0, w, h):
        dark = sum(
            1 for y in range(y0, y0 + h) for x in range(x0, x0 + w)
            for i in [(y * width + x) * 3]
            if pixels[i] + pixels[i + 1] + pixels[i + 2] < 200
        )
        return dark / (w * h)

    def error_visible():
        # "Wrong password" in the danger colour on the card, under the field.
        width, height, pixels = screen()
        box = (width // 2 - 120, height // 2 + 95, 240, 30)
        return dark_fraction(width, pixels, *box) > 0.5 and red_in(width, pixels, *box) > 30

    def card_visible():
        # The password field covers the bright horizon in the middle of the
        # screen with a dark box.
        width, height, pixels = screen()
        field = dark_fraction(width, pixels, width // 2 - 140, height // 2 + 55, 280, 25)
        # The horizon left of the card glows; it is dark only before the first frame.
        sky = dark_fraction(width, pixels, 60, height * 57 // 100, 200, 12)
        return field > 0.8 and sky < 0.2

    machine.wait_for_unit("greetd.service")
    machine.wait_until_succeeds("pgrep -u greeter -x shady-shell", timeout=60)

    with subtest("the login card is on screen"):
        retry(lambda _: card_visible(), 30)
        machine.screenshot("greeter")

    with subtest("a wrong password keeps the greeter"):
        machine.send_chars("not the password\n")
        retry(lambda _: error_visible(), 30)
        machine.fail("pgrep -u alice -x shady")
        machine.succeed("pgrep -u greeter -x shady")
        machine.screenshot("greeter-wrong-password")

    with subtest("the right password starts the Shady session"):
        machine.send_chars("wonderland\n")
        machine.wait_until_succeeds("pgrep -u alice -x shady", timeout=60)
        machine.wait_until_succeeds("pgrep -u alice -x shady-shell", timeout=60)
        machine.wait_until_fails("pgrep -u greeter -x shady", timeout=30)
        assert machine.succeed("cat /var/lib/shady-greeter/last").split() == ["alice", "shady"]
        machine.sleep(5)
        machine.screenshot("session")
  '';
}
