{ testers, pkgs, lib }:
let
  clientNodes = [ "client1" "client2" "client3" "client4" ];
  # cannelloni instances per client node, each on its own vcan interface
  interfaces = [ "vcan0" "vcan1" "vcan2" "vcan3" ];
  totalClients = builtins.length clientNodes * builtins.length interfaces;

  clientNode =
    { ... }:
    {
      networking.firewall.enable = false;

      systemd.services =
        {
          setup_can = {
            wantedBy = [ "multi-user.target" ];
            script = ''
              ${pkgs.kmod}/bin/modprobe vcan
            '' + lib.concatMapStrings (interface: ''
              ${pkgs.iproute2}/bin/ip link add name ${interface} type vcan
              ${pkgs.iproute2}/bin/ip link set dev ${interface} up mtu 16
            '') interfaces;
            serviceConfig = {
              Type = "oneshot";
              RemainAfterExit = true;
            };
          };
        }
        // lib.listToAttrs (lib.concatMap (interface: [
          (lib.nameValuePair "cannelloni-${interface}" {
            wantedBy = [ "multi-user.target" ];
            after = [ "network.target" "setup_can.service" "dump-${interface}.service" ];
            wants = [ "setup_can.service" ];
            serviceConfig = {
              ExecStart = "${pkgs.cannelloni}/bin/cannelloni -C c -I ${interface} -R server -r 10000";
              DynamicUser = true;
            };
          })
          (lib.nameValuePair "dump-${interface}" {
            wantedBy = [ "multi-user.target" ];
            after = [ "setup_can.service" ];
            wants = [ "setup_can.service" ];
            script = ''
              ${pkgs.can-utils}/bin/candump ${interface} > /tmp/${interface}.dump
            '';
          })
        ]) interfaces);
    };
in
testers.nixosTest {
  name = "tcp-multiple-clients";

  nodes = {
    server =
      { ... }:
      {
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
          maxClients = totalClients;
        };
        services.dump_can.enable = true;
      };
  } // lib.genAttrs clientNodes (_: clientNode);

  testScript = ''
    import time

    clients = [${lib.concatMapStringsSep ", " (node: node) clientNodes}]
    interfaces = [${lib.concatMapStringsSep ", " (interface: "\"${interface}\"") interfaces}]
    cangen = "${pkgs.can-utils}/bin/cangen"

    def count(node, interface, payload):
        return int(node.succeed(f"grep -c '{payload}' /tmp/{interface}.dump || true").strip())

    def wait_for_frame(node, interface, payload):
        node.wait_until_succeeds(f"grep -q '{payload}' /tmp/{interface}.dump", timeout=30)

    start_all()
    server.wait_for_unit("cannelloni")
    for client in clients:
        for interface in interfaces:
            client.wait_for_unit(f"cannelloni-{interface}")

    server.wait_until_succeeds(
        "test $(journalctl -u cannelloni | grep -c 'Got a connection from') -eq ${toString totalClients}",
        timeout=120,
    )

    # Frames only go to negotiated connections. Send probes until all clients see one.
    for attempt in range(30):
        server.succeed(f"{cangen} vcan0 -n 1 -I 100 -D 0102030405060708 -L 8")
        time.sleep(1)
        if all(count(client, interface, "01 02 03 04 05 06 07 08") > 0
               for client in clients for interface in interfaces):
            break
    else:
        raise Exception("not all clients received the probe frame")

    with subtest("frames from the server CAN bus reach every client exactly once"):
        server.succeed(f"{cangen} vcan0 -n 1 -I 200 -D 11223344DEADBEEF -L 8")
        for client in clients:
            for interface in interfaces:
                wait_for_frame(client, interface, "11 22 33 44 DE AD BE EF")
        time.sleep(1)
        for client in clients:
            for interface in interfaces:
                assert count(client, interface, "11 22 33 44 DE AD BE EF") == 1, f"{client.name} {interface}"

    with subtest("frames from a client reach the server and every other client exactly once"):
        # The local candump on client1 vcan0 sees the frame once itself.
        # A second copy would mean the server echoed it back to its sender.
        client1.succeed(f"{cangen} vcan0 -n 1 -I 300 -D AABBCCDDEEFF0011 -L 8")
        wait_for_frame(server, "vcan0", "AA BB CC DD EE FF 00 11")
        for client in clients:
            for interface in interfaces:
                wait_for_frame(client, interface, "AA BB CC DD EE FF 00 11")
        time.sleep(1)
        assert count(server, "vcan0", "AA BB CC DD EE FF 00 11") == 1
        for client in clients:
            for interface in interfaces:
                assert count(client, interface, "AA BB CC DD EE FF 00 11") == 1, f"{client.name} {interface}"

    with subtest("no client was disconnected"):
        server.fail("journalctl -u cannelloni | grep 'Connection to .* closed'")
  '';
}
