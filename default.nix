{ stdenv, cmake, lib, lksctp-tools }:

stdenv.mkDerivation rec {
  name = "cannelloni";
  version = "1.1.0";

  src = lib.cleanSource ./.;

  propagatedBuildInputs = [ cmake lksctp-tools ];
  meta = with lib; {
    description = "A SocketCAN over Ethernet Tunnel";
    homepage = https://github.com/mguentner/cannelloni;
    platforms = platforms.linux;
  };
}
