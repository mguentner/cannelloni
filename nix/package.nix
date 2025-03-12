{ stdenv, cmake, lib, lksctp-tools }:
stdenv.mkDerivation {
  name = "cannelloni";
  version = "1.1.0";

  src = builtins.filterSource (path: type: !(lib.strings.hasSuffix "nix" path || lib.strings.hasSuffix "flake.lock" path)) ( lib.cleanSource ../.);

  propagatedBuildInputs = [ cmake lksctp-tools ];
  meta = with lib; {
    description = "A SocketCAN over Ethernet Tunnel";
    homepage = "https://github.com/mguentner/cannelloni";
    platforms = platforms.linux;
  };
}
