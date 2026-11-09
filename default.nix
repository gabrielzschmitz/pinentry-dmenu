{
  lib,
  stdenv,
  pkg-config,
  libX11,
  libXinerama,
  libXft,
  fontconfig,
  freetype,
  libconfig,
  libassuan,
  gpgme,
  libgpg-error,
}:

stdenv.mkDerivation {
  pname = "pinentry-dmenu";
  version = "0.1";
  src = ./src;

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    libX11
    libXinerama
    libXft
    fontconfig
    freetype
    libconfig
    libassuan
    gpgme
    libgpg-error
  ];

  installPhase = ''
    runHook preInstall
    install -Dm755 pinentry-dmenu $out/bin/pinentry-dmenu
    install -Dm644 pinentry-dmenu.1 $out/share/man/man1/pinentry-dmenu.1
    runHook postInstall
  '';

  meta = {
    mainProgram = "pinentry-dmenu";
    description = "dmenu-based pinentry for GnuPG";
    homepage = "https://github.com/gabrielzschmitz/pinentry-dmenu";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
  };
}
