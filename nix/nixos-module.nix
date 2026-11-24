{ config, lib, ... }:
let
  cfg = config.programs.luciole;
in
{
  options.programs.luciole = {
    enable = lib.mkEnableOption "Luciole";
    package = lib.mkOption {
      type = lib.types.nullOr lib.types.package;
      default = null;
      description = "Luciole package to install.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = lib.optional (cfg.package != null) cfg.package;
  };
}
