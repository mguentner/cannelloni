{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }: let
    systems = [ "x86_64-linux" "aarch64-linux" ];
    overlay = final: prev: {
      cannelloni = final.callPackage ./default.nix {};
    };
    forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f system);
    forAllPkgs = f:
      forAllSystems (system: let
        pkgs = import nixpkgs {
          inherit system;
          overlays = [ overlay ];
        };
      in
        f pkgs);
  in {
    overlays.default = overlay;

    packages = forAllPkgs (pkgs: {
      default = pkgs.cannelloni;
    });

    devShells = forAllPkgs (pkgs: {
      default = import ./shell.nix { inherit pkgs; };
    });

    # nixosModules.default = import ./nixos-module.nix; TODO

    checks = forAllPkgs (pkgs: {
      # TODO
    });
  };
}
