# NixOS module: `programs.shady.enable = true;` adds a Shady login session.
self:
{ config, lib, pkgs, ... }:

let
  cfg = config.programs.shady;
in
{
  options.programs.shady = {
    enable = lib.mkEnableOption "the Shady 3D Wayland compositor session";

    package = lib.mkOption {
      type = lib.types.package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
      defaultText = lib.literalExpression "shady.packages.\${system}.default";
      description = "The shady package to use.";
    };

    rice = lib.mkOption {
      type = lib.types.str;
      default = "afterglow";
      example = "neon-transit";
      description = ''
        Rice from `share/shady/rices` used for whatever a user has not
        configured in `~/.config/shady/{config,init,shell}.lua`.
      '';
    };

    extraPackages = lib.mkOption {
      type = lib.types.listOf lib.types.package;
      default = with pkgs; [ foot grim slurp wl-clipboard wlr-randr ];
      defaultText = lib.literalExpression "with pkgs; [ foot grim slurp wl-clipboard wlr-randr ]";
      description = "Extra packages installed alongside the session.";
    };

    greeter.enable = lib.mkEnableOption "the Shady greeter, a login card over the Afterglow sky";

    greeter.hint = lib.mkOption {
      type = lib.types.str;
      default = "";
      example = "Live session: the nixos user has no password, press Enter.";
      description = "A line shown under the login card, e.g. how to log in to a live system.";
    };
  };

  config = lib.mkMerge [ (lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package ] ++ cfg.extraPackages;
    environment.sessionVariables.SHADY_RICE = cfg.rice;

    services.displayManager.sessionPackages = [ cfg.package ];

    hardware.graphics.enable = lib.mkDefault true;
    security.polkit.enable = lib.mkDefault true;
    programs.dconf.enable = lib.mkDefault true;
    fonts.enableDefaultPackages = lib.mkDefault true;
    # shady-shell lays out text with Pango and draws nothing without
    # fontconfig; the installer ISO base turns it off (mkOverride 500).
    fonts.fontconfig.enable = true;

    xdg.portal = {
      enable = lib.mkDefault true;
      wlr.enable = lib.mkDefault true;
      extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
      config.shady.default = lib.mkDefault [ "wlr" "gtk" ];
    };
  })

  (lib.mkIf (cfg.enable && cfg.greeter.enable) {
    services.greetd = {
      enable = true;
      settings.default_session = {
        user = "greeter";
        # greetd does not pass its own environment on to the greeter.
        command = lib.concatStringsSep " " ([
          "env"
          "SHADY_GREETER_SESSIONS=${config.services.displayManager.sessionData.desktops}/share/wayland-sessions"
          "SHADY_GREETER_STATE=/var/lib/shady-greeter/last"
        ] ++ lib.optional (cfg.greeter.hint != "")
          "SHADY_GREETER_HINT=${lib.escapeShellArg cfg.greeter.hint}"
        ++ [
          "${cfg.package}/bin/shady-greeter"
        ]);
      };
    };
    # The last user and session, so the next login starts from them.
    systemd.tmpfiles.rules = [ "d /var/lib/shady-greeter 0700 greeter greeter -" ];
  })
  ];
}
