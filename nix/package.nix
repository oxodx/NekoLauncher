{
  lib,
  stdenv,
  src,
  cmake,
  ninja,
  pkg-config,
  qt6,
  qt6Packages,
  libarchive,
  wayland,
  libxkbcommon,
  libglvnd,
  libX11,
  libXcursor,
  libXrandr,
  libXi,
  libXfixes,
  libXxf86vm,
  vulkan-loader,
  libpulseaudio,
  pipewire,
  alsa-lib,
}:
stdenv.mkDerivation {
  pname = "neko-launcher";
  version = "0.1.0-unstable-2026-11-15";
  inherit src;

  nativeBuildInputs = [
    cmake
    ninja
    pkg-config
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    qt6.qtimageformats
    qt6Packages.qtkeychain
    libarchive
    wayland
    libxkbcommon
    libglvnd
    libX11
    libXcursor
    libXrandr
    libXi
    libXfixes
    libXxf86vm
    vulkan-loader
    libpulseaudio
    pipewire
    alsa-lib
  ];

  cmakeFlags = ["-DBUILD_TESTING=OFF"];

  preFixup = let
    runtimeLibraries = lib.makeLibraryPath [
      wayland
      libxkbcommon
      libglvnd
      libX11
      libXcursor
      libXrandr
      libXi
      libXfixes
      libXxf86vm
      vulkan-loader
      libpulseaudio
      pipewire
      alsa-lib
    ];
  in ''
    qtWrapperArgs+=(--prefix LD_LIBRARY_PATH : "${runtimeLibraries}")
  '';

  meta = {
    description = "Native Qt Minecraft Java Edition launcher";
    homepage = "https://git.oxod.nl/oxod/NekoLauncher";
    license = lib.licenses.mit;
    mainProgram = "neko-launcher";
    platforms = lib.platforms.linux;
  };
}
