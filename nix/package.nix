{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  wlroots,
  wayland,
  wayland-protocols,
  wlr-protocols,
  libxkbcommon,
  libinput,
  pixman,
  libdrm,
  libGL,
  lua5_4,
  cairo,
  pango,
  fontconfig,
}:

stdenv.mkDerivation {
  pname = "shady";
  version = "0.1.0";

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.difference
      (lib.fileset.unions [
        ../meson.build
        ../meson_options.txt
        ../assets
        ../compositor
        ../data
        ../examples
        ../greeter
        ../include
        ../plugins
        ../rices
        ../shell
        ../tests
      ])
      # Media and retired rices the build never reads.
      (lib.fileset.unions [ ../assets/screenshots ../assets/test-videos ../rices/archive ]);
  };

  strictDeps = true;
  depsBuildBuild = [ pkg-config ];
  nativeBuildInputs = [ meson ninja pkg-config wayland-scanner ];
  buildInputs = [
    wlroots wayland wayland-protocols wlr-protocols libxkbcommon libinput
    pixman libdrm libGL lua5_4 cairo pango fontconfig
  ];

  mesonBuildType = "release";
  mesonFlags = [ (lib.mesonBool "installed_data" true) ];

  doCheck = true;

  passthru.providedSessions = [ "shady" "shady-safe" ];

  meta = {
    description = "Experimental 3D Wayland compositor";
    license = lib.licenses.asl20;
    platforms = lib.platforms.linux;
    mainProgram = "shady";
  };
}
