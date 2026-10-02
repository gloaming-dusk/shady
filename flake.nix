{
  description = "Shady Wayland compositor development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
    in {
      devShells = nixpkgs.lib.genAttrs systems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            strictDeps = true;
            # Follow the selected wlroots package’s dependency set.
            inputsFrom = [ pkgs.wlroots ];
            nativeBuildInputs = with pkgs; [ pkg-config meson ninja wayland-scanner wlr-protocols ];
            buildInputs = with pkgs; [
              wlroots wayland wayland-protocols libxkbcommon pixman libdrm
              mesa libglvnd libffi libxau libxdmcp lua5_4
              cairo cairo.dev pango pango.dev
            ];
            packages = with pkgs; [ stdenv.cc foot seatd wlr-randr ];
            shellHook = ''
              export PKG_CONFIG_PATH=${pkgs.cairo.dev}/lib/pkgconfig:${pkgs.pango.dev}/lib/pkgconfig:${pkgs.wlr-protocols}/share/pkgconfig:$PKG_CONFIG_PATH
            '';
          };
        });
    };
}
