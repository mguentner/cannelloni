{ config, pkgs, lib, ... }:
{
  options.services.dump_can = with lib; {
    enable = mkEnableOption "enable the can dump service";
  };

  config = {
    systemd.services.setup_can = {
      wantedBy = [ "multi-user.target" ];
      before = [ "cannelloni.service" ];
      script = ''
        ${pkgs.kmod}/bin/modprobe vcan
        ${pkgs.iproute2}/bin/ip link add name vcan0 type vcan
        ${pkgs.iproute2}/bin/ip link set dev vcan0 up mtu 16
      '';
      serviceConfig = {
        Type = "oneshot";
        RemainAfterExit = true;
      };
    };
    systemd.services.dump_can = lib.mkIf config.services.dump_can.enable {
      wantedBy = [ "multi-user.target" ];
      before = [ "cannelloni.service" ];
      after = [ "setup_can.service" ];
      wants = [ "setup_can.service" ];
      script = ''
        ${pkgs.can-utils}/bin/candump vcan0 > /tmp/vcan0.dump
      '';
    };
  };
}
