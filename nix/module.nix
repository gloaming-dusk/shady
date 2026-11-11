# NixOS module: `programs.shady.enable = true;` adds a Shady login session.
# The shell (shady-shell), the login screen (shady-greeter) and the whole
# desktop (Gloam) have modules of their own.
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
      default = "tide";
      example = "horizon";
      description = ''
        Rice from `share/shady/rices` used for whatever a user has not
        configured in `~/.config/shady/{config,init,shell}.lua`. Shady has
        Tide's compositor side; the rest comes from
        {option}`programs.shady.rices` and the shell's package.
      '';
    };

    rices = lib.mkOption {
      type = lib.types.listOf lib.types.package;
      default = [ ];
      example = lib.literalExpression "[ shady-rice-afterglow.packages.\${pkgs.stdenv.hostPlatform.system}.default ]";
      description = ''
        Packages with rices in `share/shady/rices`, such as
        <https://github.com/gloaming-dusk/shady-rice-afterglow>. They are
        installed so that Shady and the shell find them on `XDG_DATA_DIRS`.
      '';
    };

    plugins = lib.mkOption {
      type = lib.types.listOf lib.types.package;
      default = [ ];
      example = lib.literalExpression "[ shady-plugins.packages.\${pkgs.stdenv.hostPlatform.system}.black-hole ]";
      description = ''
        Plugins to make available, from
        <https://github.com/gloaming-dusk/shady-plugins>. They are added to
        `SHADY_PLUGIN_PATH`; load one with `shady.plugins.load("name")` in
        `config.lua`. A plugin runs inside the compositor, with access to the
        screen and the keyboard.
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

    softwareCursor = lib.mkEnableOption "software cursors for virtual GPUs with unreliable cursor planes";
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [ cfg.package pkgs.adwaita-icon-theme ] ++ cfg.rices ++ cfg.extraPackages;
    environment.pathsToLink = [ "/share/shady" ];
    environment.sessionVariables.SHADY_RICE = cfg.rice;
    environment.sessionVariables.SHADY_PLUGIN_PATH =
      lib.mkIf (cfg.plugins != [ ]) (lib.makeSearchPath "lib/shady/plugins" cfg.plugins);
    environment.sessionVariables = {
      SHADY_SOFTWARE_CURSORS = if cfg.softwareCursor then "1" else "0";
      XCURSOR_THEME = lib.mkDefault "Adwaita";
      XCURSOR_SIZE = lib.mkDefault "24";
      XCURSOR_PATH = [ "${pkgs.adwaita-icon-theme}/share/icons" ];
    };

    services.displayManager.sessionPackages = [ cfg.package ];
    # shady-session.target, bound to graphical-session.target.
    systemd.packages = [ cfg.package ];

    hardware.graphics.enable = lib.mkDefault true;
    security.polkit.enable = lib.mkDefault true;
    programs.dconf.enable = lib.mkDefault true;
    fonts.enableDefaultPackages = lib.mkDefault true;
    # Title bars, and shells, lay out text with Pango through fontconfig;
    # the installer ISO base turns it off (mkOverride 500).
    fonts.fontconfig.enable = true;

    xdg.portal = {
      enable = lib.mkDefault true;
      wlr.enable = lib.mkDefault true;
      extraPortals = [ pkgs.xdg-desktop-portal-gtk ];
      config.shady.default = lib.mkDefault [ "wlr" "gtk" ];
    };
  };
}
