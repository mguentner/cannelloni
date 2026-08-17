{
  stdenv,
  cmake,
  lib,
  lksctp-tools,
}:
let
  version = "2.1.1";
in
stdenv.mkDerivation {
  name = "cannelloni";
  inherit version;

  src = builtins.filterSource (
    path: type: !(lib.strings.hasSuffix "nix" path || lib.strings.hasSuffix "flake.lock" path)
  ) (lib.cleanSource ../.);

  propagatedBuildInputs = [
    cmake
    lksctp-tools
  ];

  doCheck = true;

  checkPhase = ''
    CMAKE_PROJECT_VERSION=$(grep CMAKE_PROJECT_VERSION: "$PWD/CMakeCache.txt" | cut -d= -f2)
    if [ ${version} != $CMAKE_PROJECT_VERSION ]; then
      echo "nix derivation version does not match CMAKE_PROJECT_VERSION"
      exit 1
    fi
  '';

  meta = with lib; {
    description = "A SocketCAN over Ethernet Tunnel";
    homepage = "https://github.com/mguentner/cannelloni";
    platforms = platforms.linux;
  };
}
