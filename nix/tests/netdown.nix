{ testers, pkgs }:
testers.nixosTest {
  name = "netdown";

  nodes = {
    node_a =
      { ... }:
      {
        imports = [
          ../module.nix
          ./common.nix
        ];
        networking.firewall.enable = false;
        services.cannelloni = {
          enable = true;
          transport = "udp";
          ipProtocol = "ipv4";
          remoteAddress = "node_b";
          localPort = 10000;
          canInterface = "vcan0";
        };
      };

    node_b =
      { ... }:
      {
        imports = [
          ../module.nix
          ./common.nix
        ];
        networking.firewall.enable = false;
        services.cannelloni = {
          enable = true;
          transport = "udp";
          ipProtocol = "ipv4";
          remoteAddress = "node_a";
          localPort = 10000;
          canInterface = "vcan0";
        };
      };
  };

  testScript = ''
    start_all()
    node_a.wait_for_unit("cannelloni")
    node_b.wait_for_unit("cannelloni")
    node_a.wait_until_succeeds("journalctl | grep 'UDPThread up and running'")
    node_b.wait_until_succeeds("journalctl | grep 'UDPThread up and running'")

    # Interface down: frames must be dropped, not buffered forever
    node_b.succeed("${pkgs.iproute2}/bin/ip link set vcan0 down")
    #node_b.succeed("sleep 1")

    # Generate frames on node_a. They travel over UDP to node_b, whose CANThread
    # attempts to write them to the down vcan0 and must drop them (ENETDOWN),
    # rather than buffering and retrying forever.
    node_a.succeed("${pkgs.can-utils}/bin/cangen vcan0 -n 10 -D 11223344DEADBEEF -L 8 -g 5")
    node_b.succeed("sleep 1")

    # Interface back up: transmission must resume
    node_b.succeed("${pkgs.iproute2}/bin/ip link set vcan0 up")

    # Capture whatever reaches node_b's CAN bus from now on.
    node_b.succeed("${pkgs.can-utils}/bin/candump -T 10000 vcan0 > /tmp/resume.dump 2>&1 &")
    node_b.succeed("sleep 1")

    # Send frames with a distinct payload; with the interface back up they must
    # now be delivered end-to-end.
    node_a.succeed("${pkgs.can-utils}/bin/cangen vcan0 -n 5 -D AABBCCDDEEFF0011 -L 8 -g 5")
    node_b.wait_until_succeeds("grep 'AA BB CC DD EE FF 00 11' /tmp/resume.dump")

    # The frames dropped while the interface was down must NOT reappear: they
    # were dropped, not buffered, so they must be absent from the post-resume dump.
    node_b.fail("grep '11 22 33 44 DE AD BE EF' /tmp/resume.dump")

    # Shutdown summary must still show the earlier drops
    node_b.succeed("systemctl stop cannelloni")
    node_b.wait_until_succeeds(
        "journalctl -u cannelloni | grep -E 'CAN Transmission Summary.*DROP: [1-9]'"
    )
  '';
}
