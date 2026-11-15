{
  description = "Neko Launcher — a native Qt Minecraft Java Edition launcher";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    systems.url = "github:nix-systems/default-linux";
  };

  outputs = {
    self,
    nixpkgs,
    systems,
  }: let
    forAllSystems = nixpkgs.lib.genAttrs (import systems);
  in {
    packages = forAllSystems (system: let
      pkgs = import nixpkgs {inherit system;};
      neko-launcher = pkgs.callPackage ./nix/package.nix {src = self;};
    in {
      inherit neko-launcher;
      default = neko-launcher;
    });

    apps = forAllSystems (system: {
      default = {
        type = "app";
        program = "${self.packages.${system}.default}/bin/neko-launcher";
        meta.description = "Launch Neko Launcher";
      };
    });

    formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.alejandra);
  };
}
