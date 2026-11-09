{
  pkgs ? import <nixpkgs> { },
}:

pkgs.mkShell {
  name = "pinentry-dmenu-dev";

  nativeBuildInputs = with pkgs; [
    pkg-config
    gnumake
    gcc
    bear
    clang-tools
  ];

  buildInputs = with pkgs; [
    fontconfig
    freetype
    libX11
    libXinerama
    libXft
    libconfig
    libassuan
    gpgme
    libgpg-error
  ];
}
