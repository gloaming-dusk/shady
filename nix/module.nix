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
      default = "horizon";
      example = "objects";
      description = ''
        Rice from `share/shady/rices` used for whatever a user has not
        configured in `~/.config/shady/{config,init,shell}.lua`.
      '';
    };

    extraPackages = lib.mkOption {
      type = lib.types.listOf lib.types.package;
      default = with pkgs; [ foot grim slurp wl-clipboard wlr-randr playerctl libnotify ];
      defaultText = lib.literalExpression
        "with pkgs; [ foot grim slurp wl-clipboard wlr-randr playerctl libnotify ]";
      description = ''
        Extra packages installed alongside the session: a terminal, and the
        tools the shells use for screenshots, the clipboard, media keys and
        notifications.
      '';
    };

    lock.fingerprint = lib.mkOption {
      type = lib.types.bool;
      default = config.services.fprintd.enable;
      defaultText = lib.literalExpression "config.services.fprintd.enable";
      description = ''
        Let the shell's lock screen (Tide) unlock with a fingerprint, while
        the password field stays usable: the reader is checked through its
        own PAM service, `shady-lock-fingerprint`, next to `shady-lock`.
      '';
    };

    greeter.enable = lib.mkEnableOption "the Shady greeter, a login card over the Afterglow sky";

    softwareCursor = lib.mkEnableOption "software cursors for virtual GPUs with unreliable cursor planes";

    greeter.theme = lib.mkOption {
      type = lib.types.enum [ "afterglow" "tide" ];
      default = "afterglow";
      example = "tide";
      description = "The greeter's look: Afterglow's sunset sky, or Tide's night sea.";
    };

    greeter.hint = lib.mkOption {
      type = lib.types.str;
      default = "";
      example = "Live session: the nixos user has no password, press Enter.";
      description = "A line shown under the login card, e.g. how to log in to a live system.";
    };
  };

  config = lib.mkMerge [ (lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package pkgs.adwaita-icon-theme ] ++ cfg.extraPackages;
    environment.sessionVariables.SHADY_RICE = cfg.rice;
    environment.sessionVariables = {
      SHADY_SOFTWARE_CURSORS = if cfg.softwareCursor then "1" else "0";
      XCURSOR_THEME = lib.mkDefault "Adwaita";
      XCURSOR_SIZE = lib.mkDefault "24";
      XCURSOR_PATH = [ "${pkgs.adwaita-icon-theme}/share/icons" ];
    };

    services.displayManager.sessionPackages = [ cfg.package ];
    # shady-session.target, bound to graphical-session.target.
    systemd.packages = [ cfg.package ];

    # The lock screen checks passwords through shady-auth with these. The
    # password service never waits on a fingerprint reader.
    security.pam.services.shady-lock = {
      fprintAuth = false;
    };
    security.pam.services.shady-lock-fingerprint = lib.mkIf cfg.lock.fingerprint {
      text = ''
        auth required ${config.services.fprintd.package}/lib/security/pam_fprintd.so
        account required ${pkgs.linux-pam}/lib/security/pam_unix.so
      '';
    };

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
          "SHADY_SOFTWARE_CURSORS=${if cfg.softwareCursor then "1" else "0"}"
          "XCURSOR_THEME=${lib.escapeShellArg config.environment.sessionVariables.XCURSOR_THEME}"
          "XCURSOR_SIZE=${lib.escapeShellArg config.environment.sessionVariables.XCURSOR_SIZE}"
          "XCURSOR_PATH=${lib.escapeShellArg config.environment.sessionVariables.XCURSOR_PATH}"
          "WLR_RENDERER_ALLOW_SOFTWARE=${lib.escapeShellArg (config.environment.sessionVariables.WLR_RENDERER_ALLOW_SOFTWARE or "0")}"
          "SHADY_GREETER_SESSIONS=${config.services.displayManager.sessionData.desktops}/share/wayland-sessions"
          "SHADY_GREETER_STATE=/var/lib/shady-greeter/last"
          "SHADY_GREETER_THEME=${cfg.greeter.theme}"
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
