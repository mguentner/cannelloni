{ pkgs ? import <nixpkgs> {}}:
with pkgs;
mkShell {
  buildInputs = [
    bashInteractive cmake lksctp-tools
  ];
}
