# Shady targets wlroots 0.20.2. While the pinned NixOS release ships an older
# 0.20.x, build 0.20.2 from the same recipe; once it catches up, use its own.
{ lib, wlroots_0_20, fetchFromGitLab }:

if lib.versionAtLeast wlroots_0_20.version "0.20.2" then
  wlroots_0_20
else
  wlroots_0_20.overrideAttrs {
    version = "0.20.2";
    src = fetchFromGitLab {
      domain = "gitlab.freedesktop.org";
      owner = "wlroots";
      repo = "wlroots";
      rev = "0.20.2";
      hash = "sha256-VdYymvzYp6/R255AK20j4xTd+JbCZgNiRfgeRJD+UZY=";
    };
  }
