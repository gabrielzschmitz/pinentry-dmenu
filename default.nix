{
  lib,
  stdenv,
  pkg-config,
  libX11,
  libXinerama,
  libXft,
  imlib2,
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

  postUnpack = ''
    cp ${./pinentry-dmenu.svg} $sourceRoot/pinentry-dmenu.svg
  '';

  nativeBuildInputs = [ pkg-config ];
  buildInputs = [
    libX11
    libXinerama
    libXft
    imlib2
    fontconfig
    freetype
    libconfig
    libassuan
    gpgme
    libgpg-error
  ];

  makeFlags = [ "ICONPATH=${placeholder "out"}/share/icons/hicolor/256x256/apps/pinentry-dmenu.png" ];

  installPhase = ''
    runHook preInstall
    install -Dm755 pinentry-dmenu $out/bin/pinentry-dmenu
    install -Dm644 pinentry-dmenu.png $out/share/icons/hicolor/256x256/apps/pinentry-dmenu.png
    install -Dm644 pinentry-dmenu.svg $out/share/icons/hicolor/scalable/apps/pinentry-dmenu.svg
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
