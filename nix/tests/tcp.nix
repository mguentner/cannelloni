{ nixosTest, pkgs }:
nixosTest {
  name = "tcp";

  nodes = {
    node_a = { ... }: {
      imports = [
        ../module.nix
        ./common.nix
      ];
      networking.firewall.enable = false;
      services.cannelloni = {
        enable = true;
        transport = "tcp";
        ipProtocol = "ipv4";
        localPort = 10000;
        canInterface = "vcan0";
      };
    };

    node_b = { ... }: {
      imports = [
        ../module.nix
        ./common.nix
      ];
      networking.firewall.enable = false;
      services.cannelloni = {
        enable = true;
        transport = "tcp";
        ipProtocol = "ipv4";
        remoteAddress = "node_a";
        localPort = 10000;
        canInterface = "vcan0";
        mode = "client";
      };
      
      services.dump_can.enable = true;
    };
  };

  testScript = ''
    start_all()
    node_a.wait_for_unit("cannelloni")
    node_a.wait_for_open_port(10000)
    node_b.wait_for_unit("cannelloni")
    node_b.wait_until_succeeds("journalctl | grep 'attempt_connect:Connected'")
    
    node_a.succeed("${pkgs.can-utils}/bin/cangen vcan0 -n 1 -D 11223344DEADBEEF -L 8")
    node_b.wait_until_succeeds("cat /tmp/vcan0.dump | grep '11 22 33 44 DE AD BE EF'")
  '';
}
