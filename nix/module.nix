{
  config,
  lib,
  pkgs,
  ...
}:
let
  cfg = config.services.cannelloni;
in
{
  options.services.cannelloni = with lib; {
    enable = mkEnableOption "enable cannelloni service";
    canInterface = mkOption {
      type = types.str;
      default = "can0";
      description = "cannelloni will run on this CAN interface";
    };
    remoteAddress = mkOption {
      type = types.nullOr types.str;
      default = null;
      description = "IP Address of the remote cannelloni instance. Optional for UDP";
    };
    remotePort = mkOption {
      type = types.int;
      default = 10000;
      description = "Port of the remote cannelloni instance";
    };
    localAddress = mkOption {
      type = types.str;
      default = "0.0.0.0";
      description = "IP address of the local cannelloni instance";
    };
    localPort = mkOption {
      type = types.int;
      default = 10000;
      description = "Port of the local cannelloni instance";
    };
    transport = mkOption {
      type = types.enum [ "udp" "tcp" "sctp" ];
      default = "tcp";
      description = "which transport to use";
    };
    ipProtocol = mkOption {
      type = types.enum [ "ipv4" "ipv6" ];
      default = "ipv4";
      description = "which IP protocol to use";
    };
    mode = mkOption {
      type = types.enum [ "server" "client" ];
      default = "server";
      description = "which mode to run in (server or client)";
    };
  };

  config = lib.mkIf cfg.enable {
    systemd.services.cannelloni =
    let
      mode = if cfg.mode == "server" then "s" else "c";
      transportAndMode = if cfg.transport != "udp" then (if cfg.transport == "tcp" then "-C ${mode}" else "-S ${mode}") else "";
      remoteAddress = if cfg.remoteAddress != null then "-R ${cfg.remoteAddress}" else "-p";
    in {
      description = "cannelloni";
      after = [ "network.target" ];
      wantedBy = [ "multi-user.target" ];

      serviceConfig = {
        ExecStart = "${pkgs.cannelloni}/bin/cannelloni ${transportAndMode} -I ${cfg.canInterface} -l ${builtins.toString cfg.localPort} -L ${cfg.localAddress} -r ${builtins.toString cfg.remotePort} ${remoteAddress}";
        User="cannelloni";
        DynamicUser=true;
       };
    };
  };
}