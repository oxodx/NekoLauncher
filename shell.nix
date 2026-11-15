{ pkgs ? import <nixpkgs> {} }:
pkgs.mkShell {
  packages = with pkgs; [ cmake ninja pkg-config qt6.qtbase qt6.qtimageformats qt6Packages.qtkeychain libarchive wayland libxkbcommon libglvnd libXxf86vm vulkan-loader libpulseaudio pipewire alsa-lib ];
  nativeBuildInputs = [ pkgs.qt6.wrapQtAppsHook ];
  shellHook = ''
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [ pkgs.libglvnd pkgs.vulkan-loader pkgs.openal pkgs.SDL2 pkgs.wayland pkgs.libxkbcommon pkgs.libpulseaudio pkgs.pipewire pkgs.alsa-lib ]}:/run/opengl-driver/lib:''${LD_LIBRARY_PATH:-}"
  '';
}
