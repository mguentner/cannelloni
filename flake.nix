{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  inputs.nix-github-actions.url = "github:nix-community/nix-github-actions";
  inputs.nix-github-actions.inputs.nixpkgs.follows = "nixpkgs";

  outputs = { self, nixpkgs, nix-github-actions, ... }: let
    overlay = import ./nix/overlay.nix;

    supportedSystems = [ "x86_64-linux" "aarch64-linux" ];

    forAllSystems = nixpkgs.lib.genAttrs supportedSystems;

    nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; overlays = [ overlay ]; });
  in {
    overlays.default = overlay;

    packages = forAllSystems (system:
    {
      inherit (nixpkgsFor.${system}) cannelloni;
    });

    defaultPackage = forAllSystems (system: (nixpkgsFor.${system}).cannelloni);

    devShells = forAllSystems (system:
    {
      default = import ./shell.nix { pkgs = (nixpkgsFor.${system}); };
    });

    nixosModules.default = import ./nix/module.nix;

    checks = forAllSystems (system: {
      sctp = nixpkgsFor.${system}.callPackage ./nix/tests/sctp.nix {};
      tcp = nixpkgsFor.${system}.callPackage ./nix/tests/tcp.nix {};
      udp = nixpkgsFor.${system}.callPackage ./nix/tests/udp.nix {};
    });

    githubActions = nix-github-actions.lib.mkGithubMatrix {
      checks = nixpkgs.lib.getAttrs [ "x86_64-linux" ] self.checks;
    };
  };
}
